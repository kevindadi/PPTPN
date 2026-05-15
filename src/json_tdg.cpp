#include "json_tdg.h"

#include <spdlog/spdlog.h>
#include <fstream>
#include <set>
#include <sstream>

using json = nlohmann::json;

namespace json_tdg {

JsonParseResult JsonTDGParser::parse_file(const std::string& file_path) {
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

JsonParseResult JsonTDGParser::parse_string(const std::string& json_content) {
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

    auto validation = validate();
    if (!validation.success) {
      std::ostringstream err_msg;
      for (const auto& err : validation.errors) {
        err_msg << err << "; ";
      }
      spdlog::error("[JSON] Validation failed: {}", err_msg.str());
      return {false, err_msg.str(), 0};
    }

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

void JsonTDGParser::parse_graph_object(const nlohmann::json& graph_obj) {
  if (graph_obj.contains("name")) {
    graph_.name = graph_obj["name"].get<std::string>();
    spdlog::debug("[JSON] Graph name: {}", graph_.name);
  }
}

void JsonTDGParser::parse_configuration_object(const nlohmann::json& config) {
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

void JsonTDGParser::parse_nodes_array(const nlohmann::json& nodes_array) {
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

JsonNode JsonTDGParser::parse_node_object(const nlohmann::json& node_obj) {
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

void JsonTDGParser::parse_edges_array(const nlohmann::json& edges_array) {
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
      edge.label = std::to_string(edge_obj["label"].get<int>());
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

ValidationResult JsonTDGParser::validate() const {
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

    // Rule 3: Core number validity
    if (node.core < 0 || node.core >= total_cores) {
      result.add_error("Node '" + node.id + "' has invalid core " +
                       std::to_string(node.core) + " (valid range: 0-" +
                       std::to_string(total_cores - 1) + ")");
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

std::string JsonTDGParser::to_dot_string() const {
  std::ostringstream oss;

  oss << "digraph " << graph_.name << " {\n";

  for (const auto& node : graph_.nodes) {
    oss << "    " << node.id << " [label = \"" << node_to_dot_label(node.to_node_type()) << "\";];\n";
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

NodeType JsonNode::to_node_type() const {
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

std::string node_to_dot_label(const NodeType& node) {
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

std::string node_type_to_string(const NodeType& node) {
  if (std::holds_alternative<PeriodicTask>(node)) return "periodic";
  if (std::holds_alternative<APeriodicTask>(node)) return "aperiodic";
  if (std::holds_alternative<ForkTask>(node)) return "fork";
  if (std::holds_alternative<JoinTask>(node)) return "join";
  return "empty";
}

}  // namespace json_tdg