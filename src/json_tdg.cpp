#include "json_tdg.h"

#include <fstream>
#include <set>
#include <sstream>

namespace json_tdg {

JsonParseResult JsonTDGParser::parse_file(const std::string& file_path) {
  std::ifstream file(file_path);
  if (!file.is_open()) {
    return {false, "Failed to open file: " + file_path, 0};
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  original_json_ = buffer.str();

  return parse_string(original_json_);
}

JsonParseResult JsonTDGParser::parse_string(const std::string& json_content) {
  try {
    std::istringstream stream(json_content);
    boost::property_tree::ptree pt;
    read_json(stream, pt);

    if (pt.find("graph") != pt.not_found()) {
      auto& graph = pt.get_child("graph");
      if (graph.find("name") != graph.not_found()) {
        graph_.name = graph.get<std::string>("name");
      }
    }

    if (pt.find("configuration") != pt.not_found()) {
      auto& config = pt.get_child("configuration");
      if (config.find("num_cpus") != config.not_found()) {
        graph_.num_cpus = config.get<int>("num_cpus");
      }
      if (config.find("cores_per_cpu") != config.not_found()) {
        graph_.cores_per_cpu = config.get<int>("cores_per_cpu");
      }
    }

    if (pt.find("nodes") != pt.not_found()) {
      for (auto& node : pt.get_child("nodes")) {
        parse_node(node.second);
      }
    }

    if (pt.find("edges") != pt.not_found()) {
      for (auto& edge : pt.get_child("edges")) {
        parse_edge(edge.second);
      }
    }

    auto validation = validate();
    if (!validation.success) {
      return validation;
    }

    return {true, "", 0};

  } catch (const boost::property_tree::json_parser::json_parser_error& e) {
    return {false, e.what(), 0};
  } catch (const std::exception& e) {
    return {false, e.what(), 0};
  }
}

void JsonTDGParser::parse_node(const boost::property_tree::ptree& node) {
  JsonNode jn;

  jn.id = node.get<std::string>("id");
  jn.type = node.get<std::string>("type");

  if (node.find("priority") != node.not_found()) {
    jn.priority = node.get<int>("priority");
  }
  if (node.find("core") != node.not_found()) {
    jn.core = node.get<int>("core");
  }

  if (node.find("time") != node.not_found()) {
    for (auto& t : node.get_child("time")) {
      // t is array like [3, 8]
      int idx = 0;
      int min_val = 0, max_val = 0;
      for (auto& val : t.second) {
        if (idx == 0) min_val = val.second.get_value<int>();
        else if (idx == 1) max_val = val.second.get_value<int>();
        idx++;
      }
      jn.time.push_back({min_val, max_val});
    }
  }

  if (node.find("period") != node.not_found()) {
    auto& period = node.get_child("period");
    auto it = period.begin();
    int first = it->second.get_value<int>();
    ++it;
    int second = it->second.get_value<int>();
    jn.period = {first, second};
  }

  if (node.find("locks") != node.not_found()) {
    for (auto& lock : node.get_child("locks")) {
      jn.locks.push_back(lock.second.get_value<std::string>());
    }
  }

  graph_.nodes.push_back(jn);
}

void JsonTDGParser::parse_edge(const boost::property_tree::ptree& edge) {
  JsonEdge je;

  for (auto& item : edge) {
    if (item.first == "source") {
      je.source = item.second.data();
    }
    if (item.first == "target") {
      je.target = item.second.data();
    }
    if (item.first == "label") {
      je.label = item.second.data();
    }
    if (item.first == "style") {
      je.style = item.second.data();
    }
  }

  if (!je.source.empty() && !je.target.empty()) {
    graph_.edges.push_back(je);
  }
}

JsonParseResult JsonTDGParser::validate() const {
  validation_errors_.clear();

  std::set<std::string> node_ids;
  for (const auto& node : graph_.nodes) {
    if (node_ids.count(node.id) > 0) {
      validation_errors_.push_back("Duplicate node ID: " + node.id);
    }
    node_ids.insert(node.id);

    if (node.type != "periodic" && node.type != "aperiodic" &&
        node.type != "fork" && node.type != "join" && node.type != "empty") {
      validation_errors_.push_back("Unknown node type '" + node.type +
                                    "' for node '" + node.id + "'");
    }

    if (node.type == "periodic" && node.period.first == 0 &&
        node.period.second == 0) {
      validation_errors_.push_back("Periodic task '" + node.id +
                                    "' missing 'period' field");
    }
  }

  for (const auto& edge : graph_.edges) {
    if (node_ids.count(edge.source) == 0) {
      validation_errors_.push_back("Edge references unknown source: " + edge.source);
    }
    if (node_ids.count(edge.target) == 0) {
      validation_errors_.push_back("Edge references unknown target: " + edge.target);
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

  for (const auto& node : graph_.nodes) {
    oss << "    " << node.id << " [label = \"" << node_to_dot_label(node.to_node_type()) << "\";];\n";
  }

  for (const auto& edge : graph_.edges) {
    oss << "    " << edge.source << " -> " << edge.target;
    if (!edge.label.empty() || !edge.style.empty()) {
      oss << " [";
      if (!edge.label.empty()) oss << "xlabel = \"" << edge.label << "\"";
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