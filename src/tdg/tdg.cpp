#include "tdg/tdg.h"

#include <spdlog/spdlog.h>
#include <set>

using json = nlohmann::json;
using namespace tdg;

// ===== TDG Implementation (from clap.cpp) =====
void tdg::TDG::parse_json(const std::string& json_file) {
  spdlog::info("[TDG] Starting JSON parsing: {}", json_file);

  tdg::JsonTDGParser parser;
  auto result = parser.parse_file(json_file);

  if (!result.success) {
    spdlog::error("[TDG] JSON parsing failed: {}", result.error_message);
    return;
  }

  num_cpus = parser.get_num_cpus();
  cores_per_cpu = parser.get_cores_per_cpu();

  spdlog::info("[TDG] Configuration: {} CPUs, {} cores per CPU", num_cpus, cores_per_cpu);

  for (const auto& json_node : parser.get_nodes()) {
    std::string id = json_node.id;
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

void tdg::TDG::parse_json_string(const std::string& json_content) {
  tdg::JsonTDGParser parser;
  auto result = parser.parse_string(json_content);

  if (!result.success) {
    throw std::runtime_error("JSON parsing failed: " + result.error_message);
  }

  num_cpus = parser.get_num_cpus();
  cores_per_cpu = parser.get_cores_per_cpu();

  for (const auto& json_node : parser.get_nodes()) {
    std::string id = json_node.id;
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

std::string tdg::TDG::to_dot_string() const {
  std::ostringstream oss;

  oss << "digraph G {\n";

  for (const auto& [name, node] : nodes_type) {
    oss << "    " << name << " [label = \"" << tdg::node_to_dot_label(node)
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

void tdg::TDG::export_to_dot(const std::string& output_path) {
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

std::unordered_map<int, std::vector<std::string>> tdg::TDG::classify_priority() {
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

// ===== JSON Parser Implementation (from json_tdg.cpp) =====
JsonParseResult tdg::JsonTDGParser::parse_file(const std::string& file_path) {
  spdlog::info("[JSON] Starting JSON parsing: {}", file_path);

  std::ifstream file(file_path);
  if (!file.is_open()) {
    spdlog::error("[JSON] Failed to open file: {}", file_path);
    return {false, "Failed to open file: " + file_path, 0};
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  original_json_ = buffer.str();

  return parse_string(original_json_);
}

JsonParseResult tdg::JsonTDGParser::parse_string(const std::string& json_content) {
  spdlog::info("[JSON] Parsing JSON content ({} characters)", json_content.size());

  try {
    auto j = json::parse(json_content);
    spdlog::debug("[JSON] JSON parsed successfully");

    if (j.contains("graph")) {
      parse_graph_object(j["graph"]);
    }

    if (j.contains("configuration")) {
      parse_configuration_object(j["configuration"]);
    }

    if (j.contains("nodes")) {
      parse_nodes_array(j["nodes"]);
    }

    if (j.contains("edges")) {
      parse_edges_array(j["edges"]);
    }

    spdlog::info("[JSON] Graph name: {}", graph_.name);
    spdlog::info("[JSON] Configuration: {} CPUs, {} cores per CPU",
                 graph_.num_cpus, graph_.cores_per_cpu);
    spdlog::info("[JSON] Parsed {} nodes, {} edges", graph_.nodes.size(), graph_.edges.size());

    spdlog::info("[JSON] JSON parsing completed successfully");
    return {true, "", 0};

  } catch (const json::parse_error& e) {
    spdlog::error("[JSON] Parse error: {}", e.what());
    return {false, e.what(), static_cast<int>(e.byte)};
  } catch (const std::exception& e) {
    spdlog::error("[JSON] Error: {}", e.what());
    return {false, e.what(), 0};
  }
}

void tdg::JsonTDGParser::parse_graph_object(const nlohmann::json& graph_obj) {
  if (graph_obj.contains("name")) {
    graph_.name = graph_obj["name"].get<std::string>();
    spdlog::debug("[JSON] Graph name: {}", graph_.name);
  }
}

void tdg::JsonTDGParser::parse_configuration_object(const nlohmann::json& config) {
  if (config.contains("num_cpus")) {
    graph_.num_cpus = config["num_cpus"].get<int>();
    spdlog::debug("[JSON] num_cpus: {}", graph_.num_cpus);
  }
  if (config.contains("cores_per_cpu")) {
    graph_.cores_per_cpu = config["cores_per_cpu"].get<int>();
    spdlog::debug("[JSON] cores_per_cpu: {}", graph_.cores_per_cpu);
  }
  if (config.contains("shared_locks")) {
    const auto& locks_arr = config["shared_locks"];
    if (locks_arr.is_array()) {
      for (const auto& lock : locks_arr) {
        graph_.shared_locks.push_back(lock.get<std::string>());
      }
    }
    spdlog::debug("[JSON] shared_locks: {}", graph_.shared_locks.size());
  }
}

void tdg::JsonTDGParser::parse_nodes_array(const nlohmann::json& nodes_array) {
  graph_.nodes.reserve(nodes_array.size());

  for (const auto& node_obj : nodes_array) {
    try {
      auto node = parse_node_object(node_obj);
      graph_.nodes.push_back(node);

      spdlog::debug("[JSON] Node '{}' -> {} (priority={}, core={})",
                    node.id, node.type, node.priority, node.core);
    } catch (const std::exception& e) {
      spdlog::warn("[JSON] Skipping invalid node: {}", e.what());
    }
  }
}

JsonNode tdg::JsonTDGParser::parse_node_object(const nlohmann::json& node_obj) {
  JsonNode node;

  if (!node_obj.contains("id")) {
    throw std::runtime_error("Node missing required field 'id'");
  }
  node.id = node_obj["id"].get<std::string>();

  if (!node_obj.contains("type")) {
    throw std::runtime_error("Node '" + node.id + "' missing required field 'type'");
  }
  node.type = node_obj["type"].get<std::string>();

  if (node_obj.contains("priority")) {
    node.priority = node_obj["priority"].get<int>();
  }
  if (node_obj.contains("core")) {
    node.core = node_obj["core"].get<int>();
  }

  if (node_obj.contains("time")) {
    const auto& time_arr = node_obj["time"];
    if (time_arr.is_array()) {
      for (const auto& interval : time_arr) {
        if (interval.is_array() && interval.size() == 2) {
          int min_val = interval[0].get<int>();
          int max_val = interval[1].get<int>();
          node.time.push_back({min_val, max_val});
        }
      }
    }
  }

  if (node_obj.contains("period")) {
    const auto& period_arr = node_obj["period"];
    if (period_arr.is_array() && period_arr.size() == 2) {
      node.period = {period_arr[0].get<int>(), period_arr[1].get<int>()};
    }
  }

  if (node_obj.contains("locks")) {
    const auto& locks_arr = node_obj["locks"];
    if (locks_arr.is_array()) {
      for (const auto& lock : locks_arr) {
        node.locks.push_back(lock.get<std::string>());
      }
    }
  }

  return node;
}

void tdg::JsonTDGParser::parse_edges_array(const nlohmann::json& edges_array) {
  graph_.edges.reserve(edges_array.size());

  for (const auto& edge_obj : edges_array) {
    JsonEdge edge;

    if (edge_obj.contains("source")) {
      edge.source = edge_obj["source"].get<std::string>();
    }
    if (edge_obj.contains("target")) {
      edge.target = edge_obj["target"].get<std::string>();
    }
    if (edge_obj.contains("label")) {
      if (edge_obj["label"].is_number()) {
        edge.label = std::to_string(edge_obj["label"].get<int>());
      } else if (edge_obj["label"].is_string()) {
        edge.label = edge_obj["label"].get<std::string>();
      }
    }
    if (edge_obj.contains("style")) {
      edge.style = edge_obj["style"].get<std::string>();
    }

    if (!edge.source.empty() && !edge.target.empty()) {
      graph_.edges.push_back(edge);
      spdlog::debug("[JSON] Edge: {} -> {} (style={}, label={})",
                   edge.source, edge.target, edge.style, edge.label);
    }
  }
}

ValidationResult tdg::JsonTDGParser::validate() const {
  ValidationResult result;
  int total_cores = graph_.num_cpus * graph_.cores_per_cpu;

  std::set<std::string> node_ids;
  std::set<std::string> task_ids;
  std::set<std::string> available_locks(graph_.shared_locks.begin(), graph_.shared_locks.end());

  // Rule 1: Node ID uniqueness
  for (const auto& node : graph_.nodes) {
    if (node_ids.count(node.id) > 0) {
      result.add_error("Duplicate node ID: " + node.id);
    }
    node_ids.insert(node.id);

    if (node.type == "periodic" || node.type == "aperiodic") {
      task_ids.insert(node.id);
    }
  }

  // Rule 2: Node type validation
  for (const auto& node : graph_.nodes) {
    if (node.type != "periodic" && node.type != "aperiodic" &&
        node.type != "fork" && node.type != "join" && node.type != "empty") {
      result.add_error("Unknown node type '" + node.type + "' for node '" + node.id + "'");
    }

    // Rule 3: Core number validity (skip for fork/join which don't need core assignment)
    if (node.type != "fork" && node.type != "join") {
      if (node.core < 0 || node.core >= total_cores) {
        result.add_error("Node '" + node.id + "' has invalid core " +
                         std::to_string(node.core) + " (valid range: 0-" +
                         std::to_string(total_cores - 1) + ")");
      }
    }

    // Rule 4: Periodic task must have non-zero period
    if (node.type == "periodic") {
      if (node.period.first == 0 && node.period.second == 0) {
        result.add_error("Periodic task '" + node.id + "' has missing or zero 'period' field");
      }
      if (node.period.first > node.period.second) {
        result.add_error("Periodic task '" + node.id + "' has invalid period [" +
                         std::to_string(node.period.first) + "," +
                         std::to_string(node.period.second) + "] (min > max)");
      }
    }

    // Rule 5: Time interval format validation
    for (const auto& time_range : node.time) {
      if (time_range.first < 0 || time_range.second < 0) {
        result.add_error("Node '" + node.id + "' has negative time value");
      }
      if (time_range.first > time_range.second) {
        result.add_error("Node '" + node.id + "' has invalid time interval [" +
                         std::to_string(time_range.first) + "," +
                         std::to_string(time_range.second) + "] (min > max)");
      }
    }

    // Rule 6: Lock resource existence
    for (const auto& lock : node.locks) {
      if (available_locks.find(lock) == available_locks.end()) {
        result.add_error("Node '" + node.id + "' references undefined lock '" + lock + "'");
      }
    }

    // Rule 7: fork/join nodes should not have task attributes (warning)
    if (node.type == "fork" || node.type == "join") {
      if (node.priority != 100) {
        result.add_warning("Node '" + node.id + "' is a " + node.type +
                          " but has custom priority (" + std::to_string(node.priority) + ")");
      }
      if (node.core != 0) {
        result.add_warning("Node '" + node.id + "' is a " + node.type +
                          " but has custom core (" + std::to_string(node.core) + ")");
      }
      if (!node.time.empty()) {
        result.add_warning("Node '" + node.id + "' is a " + node.type +
                          " but has time intervals defined");
      }
    }
  }

  // Rule 8: Edge references must exist
  for (const auto& edge : graph_.edges) {
    if (node_ids.count(edge.source) == 0) {
      result.add_error("Edge references unknown source node: " + edge.source);
    }
    if (node_ids.count(edge.target) == 0) {
      result.add_error("Edge references unknown target node: " + edge.target);
    }
  }

  // Rule 9: At least one task node (warning)
  if (task_ids.empty()) {
    result.add_warning("Graph contains no task nodes (periodic or aperiodic)");
  }

  return result;
}

std::string tdg::JsonTDGParser::to_dot_string() const {
  std::ostringstream oss;

  oss << "digraph " << graph_.name << " {\n";

  for (const auto& node : graph_.nodes) {
    oss << "    " << node.id << " [label = \"" << tdg::node_to_dot_label(node.to_node_type()) << "\";];\n";
  }

  for (const auto& edge : graph_.edges) {
    oss << "    " << edge.source << " -> " << edge.target;
    if (!edge.label.empty() || !edge.style.empty()) {
      oss << " [";
      if (!edge.label.empty()) {
        oss << "xlabel = \"" << edge.label << "\"";
      }
      if (!edge.style.empty()) {
        if (!edge.label.empty()) oss << "; ";
        oss << "style = \"" << edge.style << "\"";
      }
      oss << ";]";
    }
    oss << ";\n";
  }

  oss << "}\n";
  return oss.str();
}

NodeType tdg::JsonNode::to_node_type() const {
  if (type == "periodic") {
    PeriodicTask task;
    task.name = id;
    task.priority = priority;
    task.core = core;
    task.time = time;
    task.period_time = period;
    task.lock = locks;
    task.task_type = TaskType::PERIOD;
    return task;
  } else if (type == "aperiodic") {
    APeriodicTask task;
    task.name = id;
    task.priority = priority;
    task.core = core;
    task.time = time;
    task.lock = locks;
    task.task_type = TaskType::NORMAL;
    task.is_lock = !locks.empty();
    return task;
  } else if (type == "fork") {
    ForkTask task(id);
    if (!time.empty()) task.time = time[0];
    return task;
  } else if (type == "join") {
    JoinTask task(id);
    if (!time.empty()) task.time = time[0];
    return task;
  } else {
    EmptyTask task;
    task.name = id;
    return task;
  }
}

std::string tdg::node_to_dot_label(const NodeType& node) {
  std::ostringstream oss;

  if (std::holds_alternative<PeriodicTask>(node)) {
    const auto& task = std::get<PeriodicTask>(node);
    oss << "{" << task.name << ";[" << task.period_time.first << "," << task.period_time.second << "];" << task.priority << ";" << task.core << ";";
    oss << "[";
    for (size_t i = 0; i < task.time.size(); ++i) {
      if (i > 0) oss << ",";
      oss << "[" << task.time[i].first << "," << task.time[i].second << "]";
    }
    oss << "]";
    if (!task.lock.empty()) {
      oss << ";";
      for (size_t i = 0; i < task.lock.size(); ++i) {
        if (i > 0) oss << ",";
        oss << task.lock[i];
      }
    } else {
      oss << ";[3,3]";
    }
    oss << "}";
  } else if (std::holds_alternative<APeriodicTask>(node)) {
    const auto& task = std::get<APeriodicTask>(node);
    oss << "{" << task.name << ";" << task.priority << ";" << task.core << ";";
    oss << "[";
    for (size_t i = 0; i < task.time.size(); ++i) {
      if (i > 0) oss << ",";
      oss << "[" << task.time[i].first << "," << task.time[i].second << "]";
    }
    oss << "]";
    if (!task.lock.empty()) {
      oss << ";";
      for (size_t i = 0; i < task.lock.size(); ++i) {
        if (i > 0) oss << ",";
        oss << task.lock[i];
      }
    }
    oss << "}";
  } else if (std::holds_alternative<ForkTask>(node)) {
    oss << "{Fork" << std::get<ForkTask>(node).name << ";}";
  } else if (std::holds_alternative<JoinTask>(node)) {
    oss << "{Wait" << std::get<JoinTask>(node).name << ";}";
  } else if (std::holds_alternative<EmptyTask>(node)) {
    oss << "{Empty" << std::get<EmptyTask>(node).name << ";}";
  }

  return oss.str();
}

std::string tdg::node_type_to_string(const NodeType& node) {
  if (std::holds_alternative<PeriodicTask>(node)) return "periodic";
  if (std::holds_alternative<APeriodicTask>(node)) return "aperiodic";
  if (std::holds_alternative<ForkTask>(node)) return "fork";
  if (std::holds_alternative<JoinTask>(node)) return "join";
  return "empty";
}