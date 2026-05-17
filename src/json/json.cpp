#include "json/json.h"

#include <spdlog/spdlog.h>
#include <sstream>

using nlohmann::json;

namespace {

std::string format_range(const std::pair<int, int>& range) {
  return "[" + std::to_string(range.first) + ", " + std::to_string(range.second) + "]";
}

std::string format_time_ranges(const std::vector<std::pair<int, int>>& time_ranges) {
  std::ostringstream oss;
  for (size_t i = 0; i < time_ranges.size(); ++i) {
    if (i > 0) {
      oss << ", ";
    }
    oss << format_range(time_ranges[i]);
  }
  return oss.str();
}

std::string format_locks(const std::vector<std::string>& locks) {
  if (locks.empty()) {
    return "none";
  }

  std::ostringstream oss;
  for (size_t i = 0; i < locks.size(); ++i) {
    if (i > 0) {
      oss << ", ";
    }
    oss << locks[i];
  }
  return oss.str();
}

StartBinding parse_start_binding(const nlohmann::json& binding_obj) {
  StartBinding binding;
  if (binding_obj.is_string()) {
    binding.task = binding_obj.get<std::string>();
    return binding;
  }

  binding.task = binding_obj.value("task", "");
  if (binding_obj.contains("tokens")) {
    binding.tokens = binding_obj["tokens"].get<int>();
  }
  return binding;
}

PeriodicBinding parse_periodic_binding(const nlohmann::json& binding_obj) {
  PeriodicBinding binding;
  binding.task = binding_obj.value("task", "");
  if (binding_obj.contains("period")) {
    binding.period = binding_obj["period"].get<int>();
  }
  return binding;
}

bool is_task_type(const std::string& type) {
  return type == "task";
}

bool has_incoming_edge(const parse::JsonGraph& graph, const std::string& node_id) {
  for (const auto& edge : graph.edges) {
    if (edge.target == node_id && edge.source != node_id) {
      return true;
    }
  }
  return false;
}

bool has_outgoing_edge(const parse::JsonGraph& graph, const std::string& node_id) {
  for (const auto& edge : graph.edges) {
    if (edge.source == node_id && edge.target != node_id) {
      return true;
    }
  }
  return false;
}

bool has_self_loop_edge(const parse::JsonGraph& graph, const std::string& node_id) {
  for (const auto& edge : graph.edges) {
    if (edge.source == node_id && edge.target == node_id) {
      return true;
    }
  }
  return false;
}

std::string build_node_label(const parse::JsonNode& node) {
  std::ostringstream oss;
  oss << node.id << "\\n" << node.type;

  if (node.type == "task") {
    oss << "\\nprio=" << node.priority << " core=" << node.core;
    if (!node.time.empty()) {
      oss << "\\ntime=" << format_time_ranges(node.time);
    }
    oss << "\\nlocks=" << format_locks(node.locks);
  }

  return oss.str();
}

}  // namespace

namespace parse {

LockType get_lock_type(const std::string& lock_name) {
  if (lock_name.rfind("mutex", 0) == 0) {
    return LockType::MUTEX;
  }
  if (lock_name.rfind("spin", 0) == 0) {
    return LockType::SPIN;
  }
  return LockType::UNKNOWN;
}

std::string get_lock_type_short(const std::string& lock_name) {
  switch (get_lock_type(lock_name)) {
    case LockType::MUTEX: return "[M]";
    case LockType::SPIN: return "[S]";
    default: return "[?]";
  }
}

std::string format_locks_with_type(const std::vector<std::string>& locks) {
  if (locks.empty()) {
    return "none";
  }

  std::ostringstream oss;
  for (size_t i = 0; i < locks.size(); ++i) {
    if (i > 0) {
      oss << " ";
    }
    oss << get_lock_type_short(locks[i]) << locks[i];
  }
  return oss.str();
}

int calculate_time_interval_count(int lock_count) {
  return 2 * lock_count + 1;
}

std::string get_time_interval_label(int index, const std::vector<std::string>& locks) {
  int lock_count = static_cast<int>(locks.size());

  if (lock_count == 0) {
    return "[Exec]";
  }

  if (lock_count == 1) {
    switch (index) {
      case 0: return "[Pre]";
      case 1: return "[CS:" + locks[0] + "]";
      case 2: return "[Post]";
      default: return "[?]";
    }
  }

  // 嵌套加锁: 锁1 -> 锁2 -> ... -> 锁n -> 解锁n -> ... -> 解锁1
  if (index < lock_count) {
    // 临界区前阶段
    if (index == 0) {
      return "[Pre:" + locks[0] + "]";
    } else {
      return "[CS:" + locks[index - 1] + "]";
    }
  } else if (index == lock_count) {
    // 最后一个锁的临界区内
    return "[CS:" + locks[lock_count - 1] + "]";
  } else {
    // 解锁阶段
    int post_index = index - lock_count;
    if (post_index == 0) {
      return "[Post:" + locks[lock_count - 1] + "]";
    } else {
      return "[Post" + std::to_string(post_index) + "]";
    }
  }
}
}

namespace parse {

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

  graph_ = JsonGraph{};
  original_json_ = json_content;

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
  if (config.contains("policy")) {
    std::string policy_str = config["policy"].get<std::string>();
    graph_.policy = parse_schedule_policy(policy_str);
  }
  if (config.contains("start")) {
    for (const auto& start_obj : config["start"]) {
      graph_.start_tasks.push_back(parse_start_binding(start_obj));
    }
  }
  if (config.contains("end")) {
    graph_.end_tasks = config["end"].get<std::vector<std::string>>();
  }
  if (config.contains("periodic")) {
    for (const auto& periodic_obj : config["periodic"]) {
      graph_.periodic_tasks.push_back(parse_periodic_binding(periodic_obj));
    }
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
  std::set<std::string> valid_types = {"task", "fork", "join", "empty"};
  for (const auto& node : graph_.nodes) {
    if (valid_types.find(node.type) == valid_types.end()) {
      result.add_error("Unknown node type: " + node.type + " for node " + node.id);
    }
  }

  // Check for invalid core numbers
  for (const auto& node : graph_.nodes) {
    if (node.type == "task") {
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

  std::unordered_map<std::string, std::string> node_types;
  for (const auto& node : graph_.nodes) {
    node_types[node.id] = node.type;
  }

  for (const auto& start_task : graph_.start_tasks) {
    if (node_ids.find(start_task.task) == node_ids.end()) {
      result.add_error("Start task references unknown node: " + start_task.task);
      continue;
    }
    if (!is_task_type(node_types[start_task.task])) {
      result.add_error("Start task must reference a task node: " + start_task.task);
      continue;
    }
    if (start_task.tokens < 0) {
      result.add_error("Start task token count must be non-negative: " + start_task.task);
    }
    if (has_incoming_edge(graph_, start_task.task)) {
      result.add_warning("Start task " + start_task.task + " has predecessor edges");
    }
  }

  for (const auto& end_task : graph_.end_tasks) {
    if (node_ids.find(end_task) == node_ids.end()) {
      result.add_error("End task references unknown node: " + end_task);
      continue;
    }
    if (!is_task_type(node_types[end_task])) {
      result.add_error("End task must reference a task node: " + end_task);
      continue;
    }
    if (has_outgoing_edge(graph_, end_task)) {
      result.add_warning("End task " + end_task + " has successor edges");
    }
  }

  for (const auto& periodic_task : graph_.periodic_tasks) {
    if (node_ids.find(periodic_task.task) == node_ids.end()) {
      result.add_error("Periodic task references unknown node: " + periodic_task.task);
      continue;
    }
    if (!is_task_type(node_types[periodic_task.task])) {
      result.add_error("Periodic task must reference a task node: " + periodic_task.task);
      continue;
    }
    if (periodic_task.period <= 0) {
      result.add_error("Periodic task period must be positive: " + periodic_task.task);
    }
    if (has_self_loop_edge(graph_, periodic_task.task)) {
      result.add_warning("Periodic task " + periodic_task.task + " already has a self-loop release edge");
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

  // Check for invalid lock prefix (must start with 'mutex' or 'spin')
  for (const auto& node : graph_.nodes) {
    for (const auto& lock : node.locks) {
      LockType lock_type = get_lock_type(lock);
      if (lock_type == LockType::UNKNOWN) {
        result.add_error("Node " + node.id + " uses invalid lock prefix '" + lock +
                         "': must start with 'mutex' or 'spin'");
      }
    }
  }

  // Check time interval count matches lock count rule (2*locks+1)
  for (const auto& node : graph_.nodes) {
    if (node.type == "task") {
      int expected_count = calculate_time_interval_count(static_cast<int>(node.locks.size()));
      int actual_count = static_cast<int>(node.time.size());
      if (actual_count != expected_count) {
        result.add_error("Node " + node.id + " has " + std::to_string(node.locks.size()) +
                         " lock(s) but " + std::to_string(actual_count) +
                         " time interval(s) (expected " + std::to_string(expected_count) + ")");
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
    if (node.type == "task") {
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
    oss << "  " << node.id << " [label=\"" << build_node_label(node) << "\"];\n";
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
  if (type == "task") {
    TaskNode task;
    task.name = id;
    task.priority = priority;
    task.core = core;
    task.time = this->time;
    task.lock = locks;
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
  if (std::holds_alternative<TaskNode>(node)) {
    return "task";
  } else if (std::holds_alternative<ForkTask>(node)) {
    return "fork";
  } else if (std::holds_alternative<JoinTask>(node)) {
    return "join";
  } else {
    return "empty";
  }
}

std::string node_to_dot_label(const NodeType& node) {
  if (std::holds_alternative<TaskNode>(node)) {
    const auto& task = std::get<TaskNode>(node);
    std::ostringstream oss;
    oss << task.name << "\\ntask\\nprio=" << task.priority
        << " core=" << task.core << "\\n";

    for (size_t i = 0; i < task.time.size(); ++i) {
      if (i > 0) oss << ", ";
      oss << get_time_interval_label(static_cast<int>(i), task.lock)
          << " " << format_range(task.time[i]);
    }

    oss << "\\nlocks=" << format_locks_with_type(task.lock);
    return oss.str();
  } else if (std::holds_alternative<ForkTask>(node)) {
    const auto& task = std::get<ForkTask>(node);
    return task.name + "\\nfork";
  } else if (std::holds_alternative<JoinTask>(node)) {
    const auto& task = std::get<JoinTask>(node);
    return task.name + "\\njoin";
  } else {
    const auto& task = std::get<EmptyTask>(node);
    return task.name + "\\nempty";
  }
}

}  // namespace parse