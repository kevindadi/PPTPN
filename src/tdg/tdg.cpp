#include "tdg/tdg.h"

#include <spdlog/spdlog.h>
#include <set>

#include "../json/json.h"

namespace tdg {

namespace {

std::string format_core_priority_order(
    int core_id, const std::vector<std::string>& tasks,
    const std::unordered_map<std::string, int>& tasks_priority,
    const std::string& prefix) {
  std::ostringstream oss;
  oss << prefix << " Core " << core_id << " priority order: ";

  bool first = true;
  for (const auto& task : tasks) {
    if (!first) {
      oss << " > ";
    }
    first = false;
    oss << task << "(" << tasks_priority.at(task) << ")";
  }

  if (first) {
    oss << "(none)";
  }

  return oss.str();
}

}  // namespace

void add_parsed_node(tdg::TDG& tdg, const NodeType& node_type, bool log_node) {
  if (std::holds_alternative<TaskNode>(node_type)) {
    const auto& task = std::get<TaskNode>(node_type);
    tdg.all_task.emplace_back(node_type);
    tdg.tasks_priority.insert({task.name, task.priority});
    tdg.nodes_type.insert({task.name, node_type});
    tdg.vertexes_type.insert({task.name, TDGVertexType::TASK});
    tdg.tasks_type.insert({task.name, TaskType::NORMAL});

    for (const auto& lock : task.lock) {
      tdg.lock_set.insert(lock);
      tdg.task_locks_map[task.name].push_back(lock);
    }

    if (log_node) {
      spdlog::info("[TDG] Node '{}' -> task (priority={}, core={})",
                   task.name, task.priority, task.core);
    }
  } else if (std::holds_alternative<ForkTask>(node_type)) {
    const auto& task = std::get<ForkTask>(node_type);
    tdg.nodes_type.insert({task.name, node_type});
    tdg.vertexes_type.insert({task.name, TDGVertexType::FORK});
    if (log_node) {
      spdlog::info("[TDG] Node '{}' -> fork", task.name);
    }
  } else if (std::holds_alternative<JoinTask>(node_type)) {
    const auto& task = std::get<JoinTask>(node_type);
    tdg.nodes_type.insert({task.name, node_type});
    tdg.vertexes_type.insert({task.name, TDGVertexType::JOIN});
    if (log_node) {
      spdlog::info("[TDG] Node '{}' -> join", task.name);
    }
  } else if (std::holds_alternative<EmptyTask>(node_type)) {
    const auto& task = std::get<EmptyTask>(node_type);
    tdg.nodes_type.insert({task.name, node_type});
    tdg.vertexes_type.insert({task.name, TDGVertexType::EMPTY});
    if (log_node) {
      spdlog::info("[TDG] Node '{}' -> empty", task.name);
    }
  }
}

void TDG::parse_json(const std::string& json_file) {
  spdlog::info("[TDG] Starting JSON parsing: {}", json_file);

  parse::Parser parser;
  auto result = parser.parse_file(json_file);

  if (!result.success) {
    spdlog::error("[TDG] JSON parsing failed: {}", result.error_message);
    return;
  }

  num_cpus = parser.get_num_cpus();
  cores_per_cpu = parser.get_cores_per_cpu();
  policy = parser.get_policy();
  start_tasks = parser.get_start_tasks();
  end_tasks = parser.get_end_tasks();
  periodic_tasks = parser.get_periodic_tasks();

  spdlog::info("[TDG] Configuration: {} CPUs, {} cores per CPU", num_cpus, cores_per_cpu);

  for (const auto& json_node : parser.get_nodes()) {
    add_parsed_node(*this, json_node.to_node_type(), true);
  }

  for (const auto& edge : parser.get_edges()) {
    tdg_edges.emplace_back(edge.source, edge.target, edge.label, edge.style);
    spdlog::debug("[TDG] Edge: {} -> {} (style={})", edge.source, edge.target, edge.style);
  }

  spdlog::info("[TDG] JSON parsing completed: {} nodes, {} edges", nodes_type.size(), tdg_edges.size());
}

void TDG::parse_json_string(const std::string& json_content) {
  parse::Parser parser;
  auto result = parser.parse_string(json_content);

  if (!result.success) {
    throw std::runtime_error("JSON parsing failed: " + result.error_message);
  }

  num_cpus = parser.get_num_cpus();
  cores_per_cpu = parser.get_cores_per_cpu();
  policy = parser.get_policy();
  start_tasks = parser.get_start_tasks();
  end_tasks = parser.get_end_tasks();
  periodic_tasks = parser.get_periodic_tasks();

  for (const auto& json_node : parser.get_nodes()) {
    add_parsed_node(*this, json_node.to_node_type(), false);
  }

  for (const auto& edge : parser.get_edges()) {
    tdg_edges.emplace_back(edge.source, edge.target, edge.label, edge.style);
  }
}

std::string TDG::to_dot_string() const {
  std::ostringstream oss;

  oss << "digraph G {\n";

  for (const auto& [name, node] : nodes_type) {
    oss << "    " << name << " [label = \"" << parse::node_to_dot_label(node)
        << "\";];\n";
  }

  for (const auto& edge : tdg_edges) {
    std::string source, target, label, style;
    std::tie(source, target, label, style) = edge;

    oss << "    " << source << " -> " << target;
    if (!label.empty() || !style.empty()) {
      oss << " [";
      if (!label.empty()) {
        oss << "xlabel = \"" << label << "\"";
      }
      if (!style.empty()) {
        if (!label.empty()) oss << "; ";
        oss << "style = \"" << style << "\"";
      }
      oss << ";]";
    }
    oss << ";\n";
  }

  oss << "}\n";
  return oss.str();
}

void TDG::export_to_dot(const std::string& output_path) {
  spdlog::info("[DOT] Exporting to: {}", output_path);

  std::ofstream file(output_path);
  if (!file.is_open()) {
    spdlog::error("[DOT] Failed to create file: {}", output_path);
    return;
  }

  std::string dot_content = to_dot_string();
  file << dot_content;
  file.close();

  spdlog::info("[DOT] Exported {} nodes, {} edges to {}", nodes_type.size(), tdg_edges.size(), output_path);
}

std::unordered_map<int, std::vector<std::string>> TDG::classify_priority() {
  std::unordered_map<int, std::vector<std::string>> core_task;

  for (const auto& task : all_task) {
    if (std::holds_alternative<TaskNode>(task)) {
      auto result = std::get<TaskNode>(task);
      TaskConfig tc = {result.core, result.priority, result.time, result.lock};
      tasks_config.insert({result.name, tc});
      core_task[result.core].push_back(result.name);
    } else {
      continue;
    }
  }

  for (auto& [fst, snd] : core_task) {
    std::sort(snd.begin(), snd.end(), [&](const std::string& t1, const std::string& t2) {
      return tasks_priority[t1] > tasks_priority[t2];
    });
  }

  for (auto& [fst, snd] : core_task) {
    spdlog::info("{}",
                 format_core_priority_order(fst, snd, tasks_priority, "[TDG]"));
  }

  return core_task;
}

}  // namespace tdg