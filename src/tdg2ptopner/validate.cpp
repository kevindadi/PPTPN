#include <unordered_map>

#include "validate.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <variant>

namespace ptopner_export {

namespace {

bool is_dashed_edge(const std::string& style) {
  return style.find("dashed") != std::string::npos;
}

bool is_self_loop_edge(const std::string& source, const std::string& target) {
  return source == target;
}

void validate_point_intervals(const tdg::TDG& tdg,
                              PtopnerValidationResult& result) {
  for (const auto& [name, node_type] : tdg.nodes_type) {
    std::vector<std::pair<int, int>> times;
    if (std::holds_alternative<TaskNode>(node_type)) {
      times = std::get<TaskNode>(node_type).time;
    } else if (std::holds_alternative<JoinTask>(node_type)) {
      times.push_back(std::get<JoinTask>(node_type).time);
    } else if (std::holds_alternative<ForkTask>(node_type)) {
      times.push_back(std::get<ForkTask>(node_type).time);
    } else {
      continue;
    }

    for (size_t i = 0; i < times.size(); ++i) {
      if (times[i].first != times[i].second) {
        std::ostringstream oss;
        oss << "任务 " << name << " 时间区间 [" << times[i].first << ", "
            << times[i].second << "] 不是点区间,PToPNer 要求 min==max";
        result.errors.push_back(oss.str());
        result.ok = false;
      }
    }
  }
}

void validate_no_locks(const tdg::TDG& tdg, PtopnerValidationResult& result) {
  if (!tdg.lock_set.empty()) {
    std::ostringstream oss;
    oss << "检测到共享锁 [";
    bool first = true;
    for (const auto& lock : tdg.lock_set) {
      if (!first) {
        oss << ", ";
      }
      first = false;
      oss << lock;
    }
    oss << "],PToPNer 路径不支持锁建模";
    result.errors.push_back(oss.str());
    result.ok = false;
  }

  for (const auto& node : tdg.all_task) {
    if (!std::holds_alternative<TaskNode>(node)) {
      continue;
    }
    const auto& task = std::get<TaskNode>(node);
    if (!task.lock.empty()) {
      std::ostringstream oss;
      oss << "任务 " << task.name << " 使用了锁,PToPNer 路径不支持锁建模";
      result.errors.push_back(oss.str());
      result.ok = false;
    }
  }
}

size_t estimate_places(const tdg::TDG& tdg) {
  size_t places = static_cast<size_t>(tdg.num_cpus);
  for (const auto& node : tdg.all_task) {
    if (std::holds_alternative<TaskNode>(node)) {
      const auto& task = std::get<TaskNode>(node);
      places += 2 + task.time.size();
    }
  }
  for (const auto& edge : tdg.tdg_edges) {
    if (edge.is_self_loop()) {
      places += 4;
    }
  }
  places += tdg.periodic_tasks.size();
  return places;
}

size_t estimate_transitions(const tdg::TDG& tdg) {
  size_t transitions = 0;
  for (const auto& node : tdg.all_task) {
    if (std::holds_alternative<TaskNode>(node)) {
      const auto& task = std::get<TaskNode>(node);
      transitions += 1 + task.time.size();
    } else if (std::holds_alternative<JoinTask>(node) ||
               std::holds_alternative<ForkTask>(node)) {
      transitions += 1;
    }
  }

  for (const auto& edge : tdg.tdg_edges) {
    if (edge.is_self_loop()) {
      transitions += 4;
      continue;
    }
    if (edge.is_dashed()) {
      continue;
    }
    transitions += 1;
  }

  std::unordered_map<int, std::vector<std::string>> core_tasks;
  for (const auto& node : tdg.all_task) {
    if (!std::holds_alternative<TaskNode>(node)) {
      continue;
    }
    const auto& task = std::get<TaskNode>(node);
    core_tasks[task.core].push_back(task.name);
  }
  for (auto& [core_id, tasks] : core_tasks) {
    std::sort(tasks.begin(), tasks.end(), [&](const std::string& a, const std::string& b) {
      return tdg.tasks_priority.at(a) > tdg.tasks_priority.at(b);
    });
    for (size_t i = 0; i < tasks.size(); ++i) {
      for (size_t j = i + 1; j < tasks.size(); ++j) {
        transitions += 1;
      }
    }
  }

  transitions += tdg.periodic_tasks.size();

  std::set<std::string> consume_tasks(tdg.end_tasks.begin(), tdg.end_tasks.end());
  for (const auto& [vertex_name, node_type] : tdg.nodes_type) {
    if (!std::holds_alternative<TaskNode>(node_type)) {
      continue;
    }
    const bool has_successor = std::any_of(
        tdg.tdg_edges.begin(), tdg.tdg_edges.end(),
        [&](const TdgEdge& edge) { return edge.leaves(vertex_name); });
    if (!has_successor) {
      consume_tasks.insert(vertex_name);
    }
  }
  transitions += consume_tasks.size();

  return transitions;
}

void validate_size_limits(const tdg::TDG& tdg, PtopnerValidationResult& result) {
  const size_t places = estimate_places(tdg);
  const size_t transitions = estimate_transitions(tdg);
  if (places > static_cast<size_t>(kPtopnerMaxPlaces) ||
      transitions > static_cast<size_t>(kPtopnerMaxPlaces)) {
    std::ostringstream oss;
    oss << "网规模超限:预估 " << places << " 库所 + " << transitions
        << " 变迁 > PToPNer 上限 " << kPtopnerMaxPlaces;
    result.errors.push_back(oss.str());
    result.ok = false;
  }
}

void validate_warnings(const tdg::TDG& tdg, PtopnerValidationResult& result) {
  for (const auto& edge : tdg.tdg_edges) {
    if (edge.is_dashed()) {
      result.warnings.push_back("虚线边 " + edge.source + " -> " + edge.target +
                                " 将被忽略（与 tdg2pn 一致）");
    }
  }

  if (!tdg.periodic_tasks.empty()) {
    result.warnings.push_back("检测到 periodic 配置,将复用 tdg2pn 的 period release 建模");
  }
}

}  // namespace

PtopnerValidationResult validate_for_ptopner(const tdg::TDG& tdg) {
  PtopnerValidationResult result;

  if (tdg.policy != SchedulePolicy::FIXED_PRIOR_WITH_RESTART) {
    result.errors.push_back("不支持调度策略 \"" +
                            schedule_policy_to_string(tdg.policy) +
                            "\",PToPNer 路径要求 fixed_prior_with_restart");
    result.ok = false;
  }

  validate_point_intervals(tdg, result);
  validate_no_locks(tdg, result);
  validate_size_limits(tdg, result);
  validate_warnings(tdg, result);

  return result;
}

}  // namespace ptopner_export
