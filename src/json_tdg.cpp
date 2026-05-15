#include "json_tdg.h"

#include <boost/log/trivial.hpp>
#include <fstream>
#include <sstream>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace json_tdg {

JsonParseResult JsonTDGParser::parse_file(const std::string& file_path) {
  BOOST_LOG_TRIVIAL(info) << "[JSON] Starting JSON parsing: " << file_path;

  std::ifstream file(file_path);
  if (!file.is_open()) {
    BOOST_LOG_TRIVIAL(error) << "[JSON] Failed to open file: " << file_path;
    return {false, "Failed to open file: " + file_path, 0};
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  original_json_ = buffer.str();

  return parse_string(original_json_);
}

JsonParseResult JsonTDGParser::parse_string(const std::string& json_content) {
  BOOST_LOG_TRIVIAL(info) << "[JSON] Parsing JSON content (" << json_content.size()
                           << " characters)";

  try {
    auto j = json::parse(json_content);
    BOOST_LOG_TRIVIAL(debug) << "[JSON] JSON parsed successfully";

    // Parse graph metadata
    if (j.contains("graph")) {
      parse_graph_object(j["graph"]);
    }

    // Parse configuration
    if (j.contains("configuration")) {
      parse_configuration_object(j["configuration"]);
    }

    // Parse nodes
    if (j.contains("nodes")) {
      parse_nodes_array(j["nodes"]);
    }

    // Parse edges
    if (j.contains("edges")) {
      parse_edges_array(j["edges"]);
    }

    // Log summary
    BOOST_LOG_TRIVIAL(info) << "[JSON] Graph name: " << graph_.name;
    BOOST_LOG_TRIVIAL(info) << "[JSON] Configuration: " << graph_.num_cpus
                             << " CPUs, " << graph_.cores_per_cpu
                             << " cores per CPU";
    BOOST_LOG_TRIVIAL(info) << "[JSON] Parsed " << graph_.nodes.size()
                             << " nodes";
    BOOST_LOG_TRIVIAL(info) << "[JSON] Parsed " << graph_.edges.size()
                             << " edges";

    // Validate
    auto validation = validate();
    if (!validation.success) {
      BOOST_LOG_TRIVIAL(error) << "[JSON] Validation failed: "
                                << validation.error_message;
      return validation;
    }

    BOOST_LOG_TRIVIAL(info)
        << "[JSON] JSON parsing completed successfully";
    return {true, "", 0};

  } catch (const json::parse_error& e) {
    BOOST_LOG_TRIVIAL(error) << "[JSON] Parse error: " << e.what();
    return {false, e.what(), static_cast<int>(e.byte)};
  } catch (const std::exception& e) {
    BOOST_LOG_TRIVIAL(error) << "[JSON] Error: " << e.what();
    return {false, e.what(), 0};
  }
}

void JsonTDGParser::parse_graph_object(const nlohmann::json& graph_obj) {
  if (graph_obj.contains("name")) {
    graph_.name = graph_obj["name"].get<std::string>();
    BOOST_LOG_TRIVIAL(debug) << "[JSON] Graph name: " << graph_.name;
  }
}

void JsonTDGParser::parse_configuration_object(
    const nlohmann::json& config) {
  if (config.contains("num_cpus")) {
    graph_.num_cpus = config["num_cpus"].get<int>();
    BOOST_LOG_TRIVIAL(debug) << "[JSON] num_cpus: " << graph_.num_cpus;
  }
  if (config.contains("cores_per_cpu")) {
    graph_.cores_per_cpu = config["cores_per_cpu"].get<int>();
    BOOST_LOG_TRIVIAL(debug) << "[JSON] cores_per_cpu: " << graph_.cores_per_cpu;
  }
}

void JsonTDGParser::parse_nodes_array(const nlohmann::json& nodes_array) {
  graph_.nodes.reserve(nodes_array.size());

  for (const auto& node_obj : nodes_array) {
    try {
      auto node = parse_node_object(node_obj);
      graph_.nodes.push_back(node);

      // Log node details
      std::string node_type_name = node.type;
      std::ostringstream oss;
      oss << "[JSON] Node '" << node.id << "' -> " << node_type_name;
      if (node.type == "periodic" || node.type == "aperiodic") {
        oss << " (priority=" << node.priority << ", core=" << node.core << ")";
      }
      BOOST_LOG_TRIVIAL(debug) << oss.str();

    } catch (const std::exception& e) {
      BOOST_LOG_TRIVIAL(warning) << "[JSON] Skipping invalid node: " << e.what();
    }
  }
}

JsonNode JsonTDGParser::parse_node_object(const nlohmann::json& node_obj) {
  JsonNode node;

  // Required: id
  if (!node_obj.contains("id")) {
    throw std::runtime_error("Node missing required field 'id'");
  }
  node.id = node_obj["id"].get<std::string>();

  // Required: type
  if (!node_obj.contains("type")) {
    throw std::runtime_error("Node '" + node.id +
                              "' missing required field 'type'");
  }
  node.type = node_obj["type"].get<std::string>();

  // Optional fields for tasks
  if (node_obj.contains("priority")) {
    node.priority = node_obj["priority"].get<int>();
  }
  if (node_obj.contains("core")) {
    node.core = node_obj["core"].get<int>();
  }

  // Parse time intervals (array of [min, max] pairs)
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

  // Parse period (for periodic tasks)
  if (node_obj.contains("period")) {
    const auto& period_arr = node_obj["period"];
    if (period_arr.is_array() && period_arr.size() == 2) {
      node.period = {period_arr[0].get<int>(), period_arr[1].get<int>()};
    }
  }

  // Parse locks
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

    // Required: source and target
    if (edge_obj.contains("source")) {
      edge.source = edge_obj["source"].get<std::string>();
    }
    if (edge_obj.contains("target")) {
      edge.target = edge_obj["target"].get<std::string>();
    }

    // Optional: label and style
    if (edge_obj.contains("label")) {
      edge.label = std::to_string(edge_obj["label"].get<int>());
    }
    if (edge_obj.contains("style")) {
      edge.style = edge_obj["style"].get<std::string>();
    }

    graph_.edges.push_back(edge);

    BOOST_LOG_TRIVIAL(debug) << "[JSON] Edge: " << edge.source << " -> "
                             << edge.target << " (style=" << edge.style
                             << ", label=" << edge.label << ")";
  }
}

JsonParseResult JsonTDGParser::validate() const {
  validation_errors_.clear();

  // Check for duplicate node IDs
  std::set<std::string> node_ids;
  for (const auto& node : graph_.nodes) {
    if (node_ids.count(node.id) > 0) {
      validation_errors_.push_back("Duplicate node ID: " + node.id);
    }
    node_ids.insert(node.id);

    // Validate node type
    if (node.type != "periodic" && node.type != "aperiodic" &&
        node.type != "fork" && node.type != "join" && node.type != "empty") {
      validation_errors_.push_back("Unknown node type '" + node.type +
                                    "' for node '" + node.id + "'");
    }

    // Validate periodic task has period
    if (node.type == "periodic" && node.period.first == 0 &&
        node.period.second == 0) {
      validation_errors_.push_back("Periodic task '" + node.id +
                                    "' missing 'period' field");
    }
  }

  // Check edge references
  for (const auto& edge : graph_.edges) {
    if (node_ids.count(edge.source) == 0) {
      validation_errors_.push_back("Edge references unknown source: " +
                                     edge.source);
    }
    if (node_ids.count(edge.target) == 0) {
      validation_errors_.push_back("Edge references unknown target: " +
                                     edge.target);
    }
  }

  if (!validation_errors_.empty()) {
    std::ostringstream oss;
    for (const auto& err : validation_errors_) {
      oss << err << "; ";
    }
    return {false, oss.str(), 0};
  }

  return {true, "", 0};
}

std::string JsonTDGParser::to_dot_string() const {
  std::ostringstream oss;

  oss << "digraph " << graph_.name << " {\n";

  // Export nodes
  for (const auto& node : graph_.nodes) {
    oss << "    " << node.id << " [label = \"" << node_to_dot_label(node.to_node_type())
        << "\";];\n";
  }

  // Export edges
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
    task.locks = locks;
    task.task_type = TaskType::PERIOD;
    return task;
  } else if (type == "aperiodic") {
    APeriodicTask task;
    task.name = id;
    task.priority = priority;
    task.core = core;
    task.time = time;
    task.locks = locks;
    task.task_type = TaskType::NORMAL;
    task.is_lock = !locks.empty();
    return task;
  } else if (type == "fork") {
    ForkTask task;
    task.name = id;
    task.time = time;
    return task;
  } else if (type == "join") {
    JoinTask task;
    task.name = id;
    task.time = time;
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
    oss << "{" << task.name << ";[" << task.period_time.first << ","
        << task.period_time.second << "];" << task.priority << ";" << task.core
        << ";";
    // Time intervals
    oss << "[";
    for (size_t i = 0; i < task.time.size(); ++i) {
      if (i > 0) oss << ",";
      oss << "[" << task.time[i].first << "," << task.time[i].second << "]";
    }
    oss << "]";
    // Locks
    if (!task.locks.empty()) {
      oss << ";";
      for (size_t i = 0; i < task.locks.size(); ++i) {
        if (i > 0) oss << ",";
        oss << task.locks[i];
      }
    } else {
      oss << ";[3,3]";  // Default lock for compatibility
    }
    oss << "}";

  } else if (std::holds_alternative<APeriodicTask>(node)) {
    const auto& task = std::get<APeriodicTask>(node);
    oss << "{" << task.name << ";" << task.priority << ";" << task.core << ";";
    // Time intervals
    oss << "[";
    for (size_t i = 0; i < task.time.size(); ++i) {
      if (i > 0) oss << ",";
      oss << "[" << task.time[i].first << "," << task.time[i].second << "]";
    }
    oss << "]";
    // Locks
    if (!task.locks.empty()) {
      oss << ";";
      for (size_t i = 0; i < task.locks.size(); ++i) {
        if (i > 0) oss << ",";
        oss << task.locks[i];
      }
    }
    oss << "}";

  } else if (std::holds_alternative<ForkTask>(node)) {
    const auto& task = std::get<ForkTask>(node);
    oss << "{Fork" << task.name << ";}";

  } else if (std::holds_alternative<JoinTask>(node)) {
    const auto& task = std::get<JoinTask>(node);
    oss << "{Wait" << task.name << ";}";

  } else if (std::holds_alternative<EmptyTask>(node)) {
    const auto& task = std::get<EmptyTask>(node);
    oss << "{Empty" << task.name << ";}";
  }

  return oss.str();
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

}  // namespace json_tdg
