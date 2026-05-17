#include "tdg/tdg.h"

#include <spdlog/spdlog.h>
#include <set>

#include "../json/json.h"

namespace tdg {

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
    NodeType node_type = json_node.to_node_type();

    if (std::holds_alternative<PeriodicTask>(node_type)) {
      const auto& task = std::get<PeriodicTask>(node_type);
      all_task.emplace_back(node_type);
      tasks_priority.insert({task.name, task.priority});
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::TASK});
      tasks_type.insert({task.name, TaskType::PERIOD});

      for (const auto& lock : task.lock) {
        lock_set.insert(lock);
        task_locks_map[task.name].push_back(lock);
      }

      spdlog::info("[TDG] Node '{}' -> periodic (priority={}, core={})",
                   task.name, task.priority, task.core);

    } else if (std::holds_alternative<APeriodicTask>(node_type)) {
      const auto& task = std::get<APeriodicTask>(node_type);
      all_task.emplace_back(node_type);
      tasks_priority.insert({task.name, task.priority});
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::TASK});
      tasks_type.insert({task.name, TaskType::NORMAL});

      for (const auto& lock : task.lock) {
        lock_set.insert(lock);
        task_locks_map[task.name].push_back(lock);
      }

      spdlog::info("[TDG] Node '{}' -> aperiodic (priority={}, core={})",
                   task.name, task.priority, task.core);

    } else if (std::holds_alternative<ForkTask>(node_type)) {
      const auto& task = std::get<ForkTask>(node_type);
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::FORK});
      spdlog::info("[TDG] Node '{}' -> fork", task.name);

    } else if (std::holds_alternative<JoinTask>(node_type)) {
      const auto& task = std::get<JoinTask>(node_type);
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::JOIN});
      spdlog::info("[TDG] Node '{}' -> join", task.name);

    } else if (std::holds_alternative<EmptyTask>(node_type)) {
      const auto& task = std::get<EmptyTask>(node_type);
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::EMPTY});
      spdlog::info("[TDG] Node '{}' -> empty", task.name);
    }
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
    NodeType node_type = json_node.to_node_type();

    if (std::holds_alternative<PeriodicTask>(node_type)) {
      const auto& task = std::get<PeriodicTask>(node_type);
      all_task.emplace_back(node_type);
      tasks_priority.insert({task.name, task.priority});
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::TASK});
      tasks_type.insert({task.name, TaskType::PERIOD});

      for (const auto& lock : task.lock) {
        lock_set.insert(lock);
        task_locks_map[task.name].push_back(lock);
      }

    } else if (std::holds_alternative<APeriodicTask>(node_type)) {
      const auto& task = std::get<APeriodicTask>(node_type);
      all_task.emplace_back(node_type);
      tasks_priority.insert({task.name, task.priority});
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::TASK});
      tasks_type.insert({task.name, TaskType::NORMAL});

      for (const auto& lock : task.lock) {
        lock_set.insert(lock);
        task_locks_map[task.name].push_back(lock);
      }

    } else if (std::holds_alternative<ForkTask>(node_type)) {
      const auto& task = std::get<ForkTask>(node_type);
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::FORK});

    } else if (std::holds_alternative<JoinTask>(node_type)) {
      const auto& task = std::get<JoinTask>(node_type);
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::JOIN});

    } else if (std::holds_alternative<EmptyTask>(node_type)) {
      const auto& task = std::get<EmptyTask>(node_type);
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::EMPTY});
    }
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
    if (std::holds_alternative<APeriodicTask>(task)) {
      auto result = std::get<APeriodicTask>(task);
      TaskConfig tc = {result.core, result.priority, result.time, result.lock};
      tasks_config.insert({result.name, tc});
      core_task[result.core].push_back(result.name);
    } else if (std::holds_alternative<PeriodicTask>(task)) {
      auto result = std::get<PeriodicTask>(task);
      TaskConfig tc = {result.core, result.priority, result.time, result.lock};
      tasks_config.insert({result.name, tc});
      core_task[result.core].push_back(result.name);
    } else {
      continue;
    }
  }

  for (auto& [fst, snd] : core_task) {
    std::sort(snd.begin(), snd.end(), [&](const std::string& t1, const std::string& t2) {
      return tasks_priority[t1] < tasks_priority[t2];
    });
  }

  for (auto& [fst, snd] : core_task) {
    std::stringstream ss;
    ss << "[TDG] Core: " << fst << " [ ";
    for (const auto& task : snd) {
      ss << task << " < ";
    }
    ss << " ]";
    spdlog::info("{}", ss.str());
  }

  return core_task;
}

}  // namespace tdg