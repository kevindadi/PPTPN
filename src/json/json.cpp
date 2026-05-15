#include "json/json.h"

#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace json {

ParseResult Parser::parse_file(const std::string& file_path) {
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

ParseResult Parser::parse_string(const std::string& json_content) {
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

void Parser::parse_graph_object(const nlohmann::json& graph_obj) {
  if (graph_obj.contains("name")) {
    graph_.name = graph_obj["name"].get<std::string>();
  }
}

void Parser::parse_configuration_object(const nlohmann::json& config) {
  if (config.contains("num_cpus")) {
    graph_.num_cpus = config["num_cpus"].get<int>();
  }
  if (config.contains("cores_per_cpu")) {
    graph_.cores_per_cpu = config["cores_per_cpu"].get<int>();
  }
  if (config.contains("shared_locks")) {
    graph_.shared_locks = config["shared_locks"].get<std::vector<std::string>>();
  }
}

void Parser::parse_nodes_array(const nlohmann::json& nodes_array) {
  for (const auto& node_obj : nodes_array) {
    graph_.nodes.push_back(parse_node_object(node_obj));
  }
}

void Parser::parse_edges_array(const nlohmann::json& edges_array) {
  for (const auto& edge_obj : edges_array) {
    JsonEdge edge;
    edge.source = edge_obj.value("source", "");
    edge.target = edge_obj.value("target", "");
    edge.label = edge_obj.value("label", "");
    edge.style = edge_obj.value("style", "");
    graph_.edges.push_back(edge);
  }
}

JsonNode Parser::parse_node_object(const nlohmann::json& node_obj) {
  JsonNode node;
  node.id = node_obj.value("id", "");
  node.type = node_obj.value("type", "");

  if (node_obj.contains("priority")) {
    node.priority = node_obj["priority"].get<int>();
  }
  if (node_obj.contains("core")) {
    node.core = node_obj["core"].get<int>();
  }

  if (node_obj.contains("time")) {
    for (const auto& time_range : node_obj["time"]) {
      if (time_range.is_array() && time_range.size() == 2) {
        int start = time_range[0].get<int>();
        int end = time_range[1].get<int>();
        node.time.push_back({start, end});
      }
    }
  }

  if (node_obj.contains("period")) {
    auto& period = node_obj["period"];
    if (period.is_array() && period.size() == 2) {
      node.period.first = period[0].get<int>();
      node.period.second = period[1].get<int>();
    }
  }

  if (node_obj.contains("locks")) {
    node.locks = node_obj["locks"].get<std::vector<std::string>>();
  }

  return node;
}

ValidationResult Parser::validate() const {
  ValidationResult result;

  // Check for duplicate node IDs
  std::set<std::string> node_ids;
  for (const auto& node : graph_.nodes) {
    if (node_ids.count(node.id) > 0) {
      result.add_error("Duplicate node ID: " + node.id);
    }
    node_ids.insert(node.id);
  }

  // Check for unknown node types
  std::set<std::string> valid_types = {"periodic", "aperiodic", "fork", "join", "empty"};
  for (const auto& node : graph_.nodes) {
    if (valid_types.find(node.type) == valid_types.end()) {
      result.add_error("Unknown node type: " + node.type + " for node " + node.id);
    }
  }

  // Check for invalid core numbers
  for (const auto& node : graph_.nodes) {
    if (node.type == "periodic" || node.type == "aperiodic") {
      int max_core = graph_.num_cpus * graph_.cores_per_cpu - 1;
      if (node.core < 0 || node.core > max_core) {
        result.add_error("Invalid core number for node " + node.id + ": " +
                         std::to_string(node.core) + " (valid range: 0-" + std::to_string(max_core) + ")");
      }
    }
  }

  // Check if edges reference valid nodes
  for (const auto& edge : graph_.edges) {
    if (node_ids.find(edge.source) == node_ids.end()) {
      result.add_error("Edge references unknown source node: " + edge.source);
    }
    if (node_ids.find(edge.target) == node_ids.end()) {
      result.add_error("Edge references unknown target node: " + edge.target);
    }
  }

  // Check for missing period on periodic tasks
  for (const auto& node : graph_.nodes) {
    if (node.type == "periodic") {
      bool has_valid_period = std::all_of(node.period.begin(), node.period.end(),
                                           [](int v) { return v > 0; });
      if (!has_valid_period) {
        result.add_error("Periodic task " + node.id + " missing or invalid period");
      }
    }
  }

  // Check for undefined locks
  std::set<std::string> defined_locks(graph_.shared_locks.begin(), graph_.shared_locks.end());
  for (const auto& node : graph_.nodes) {
    for (const auto& lock : node.locks) {
      if (defined_locks.find(lock) == defined_locks.end()) {
        result.add_error("Node " + node.id + " uses undefined lock: " + lock);
      }
    }
  }

  // Check for invalid time intervals
  for (const auto& node : graph_.nodes) {
    for (const auto& time_range : node.time) {
      if (time_range.first > time_range.second) {
        result.add_error("Invalid time interval for node " + node.id +
                         ": [" + std::to_string(time_range.first) + ", " +
                         std::to_string(time_range.second) + "]");
      }
    }
  }

  // Check for task nodes (warn if none)
  bool has_task_nodes = false;
  for (const auto& node : graph_.nodes) {
    if (node.type == "periodic" || node.type == "aperiodic") {
      has_task_nodes = true;
      break;
    }
  }
  if (!has_task_nodes && !graph_.nodes.empty()) {
    result.add_warning("No task nodes found in graph");
  }

  // Check for fork/join with task attributes
  for (const auto& node : graph_.nodes) {
    if (node.type == "fork" || node.type == "join") {
      if (node.priority != 100 || node.core != 0 || !node.time.empty() || !node.locks.empty()) {
        result.add_warning("Node " + node.id + " is " + node.type + " but has task attributes (priority/core/time/locks)");
      }
    }
  }

  return result;
}

std::string Parser::to_dot_string() const {
  std::ostringstream oss;

  oss << "digraph " << graph_.name << " {\n";
  oss << "  rankdir=LR;\n";
  oss << "  node [shape=box];\n\n";

  for (const auto& node : graph_.nodes) {
    oss << "  " << node.id << " [label=\"" << node.id << "\\n(" << node.type << ")";
    if (node.type == "periodic" || node.type == "aperiodic") {
      oss << "\\nprio=" << node.priority << " core=" << node.core;
    }
    oss << "\"];\n";
  }

  oss << "\n";

  for (const auto& edge : graph_.edges) {
    oss << "  " << edge.source << " -> " << edge.target;
    if (!edge.label.empty() || !edge.style.empty()) {
      oss << " [";
      if (!edge.label.empty()) {
        oss << "xlabel=\"" << edge.label << "\"";
      }
      if (!edge.style.empty()) {
        if (!edge.label.empty()) oss << ", ";
        oss << "style=\"" << edge.style << "\"";
      }
      oss << "]";
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
    task.time = this->time;
    task.locks = locks;
    task.task_type = TaskType::PERIOD;
    task.period_time = period;
    return task;
  } else if (type == "aperiodic") {
    APeriodicTask task;
    task.name = id;
    task.priority = priority;
    task.core = core;
    task.time = this->time;
    task.locks = locks;
    task.task_type = TaskType::NORMAL;
    return task;
  } else if (type == "fork") {
    ForkTask task(id);
    return task;
  } else if (type == "join") {
    JoinTask task(id);
    return task;
  } else {
    EmptyTask task;
    task.name = id;
    return task;
  }
}

std::string node_type_to_string(const NodeType& node) {
  if (std::holds_alternative<PeriodicTask>(node)) {
    return "periodic";
  } else if (std::holds_alternative<APeriodicTask>(node)) {
    return "aperiodic";
  } else if (std::holds_alternative<ForkTask>(node)) {
    return "fork";
  } else if (std::holds_alternative<JoinTask>(node)) {
    return "join";
  } else {
    return "empty";
  }
}

std::string node_to_dot_label(const NodeType& node) {
  if (std::holds_alternative<PeriodicTask>(node)) {
    const auto& task = std::get<PeriodicTask>(node);
    return task.name + "\\n(" + std::to_string(task.priority) + ")";
  } else if (std::holds_alternative<APeriodicTask>(node)) {
    const auto& task = std::get<APeriodicTask>(node);
    return task.name + "\\n(" + std::to_string(task.priority) + ")";
  } else if (std::holds_alternative<ForkTask>(node)) {
    const auto& task = std::get<ForkTask>(node);
    return task.name;
  } else if (std::holds_alternative<JoinTask>(node)) {
    const auto& task = std::get<JoinTask>(node);
    return task.name;
  } else {
    const auto& task = std::get<EmptyTask>(node);
    return task.name;
  }
}

}  // namespace json