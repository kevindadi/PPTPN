//
// Created by 张凯文 on 2023/6/26.
//
#include "config.h"
#include <boost/foreach.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
using json = nlohmann::json;
using namespace nlohmann::literals;

namespace logging = boost::log;

Config::Config(std::string dag_file) {
  this->dag_file = std::move(dag_file);
  logging::core::get()->set_filter(logging::trivial::severity >=
                                   logging::trivial::info);
}

Config::Config(std::string dag_file, std::string task_file) {
  this->dag_file = std::move(dag_file);
  this->task_file = std::move(task_file);
  logging::core::get()->set_filter(logging::trivial::severity >=
                                   logging::trivial::info);
}

std::vector<std::string> Config::splitString(const std::string &str,
                                             char delimiter) {
  std::vector<std::string> tokens;
  std::istringstream iss(str);
  std::string token;
  while (std::getline(iss, token, delimiter)) {
    tokens.push_back(token);
  }
  return tokens;
}

void Config::parse_dag() {
  boost::ref_property_map<DAG *, std::string> dag_name(
      get_property(dag, boost::graph_name));
  dag_dp.property("name", dag_name);
  dag_dp.property("node_id", get(&DAGVertex::name, dag));
  dag_dp.property("label", get(&DAGVertex::label, dag));
  dag_dp.property("shape", get(&DAGVertex::shape, dag));
  dag_dp.property("label", get(&DAGEdge::label, dag));

  std::ifstream dag_f(dag_file);
  typename boost::property_map<DAG, boost::vertex_index_t>::type index =
      get(boost::vertex_index, dag);
  if (read_graphviz(dag_f, dag, dag_dp)) {
    BOOST_LOG_TRIVIAL(info)
        << "Graph Name: " << get_property(dag, boost::graph_name);
    BOOST_FOREACH (DAG::vertex_descriptor v, vertices(dag)) {
      std::string id = get("node_id", dag_dp, v);
      std::string label = get("label", dag_dp, v);
      if (label.find("Wait") == std::string::npos) {
        dag_task_name.push_back(label);
      }
      task_index.insert(std::make_pair(index[v], id));
    }
  }
}

void Config::parse_json() {
  std::ifstream file(task_file);
  json jsonData;
  file >> jsonData;

  // Parse the JSON data into your structure
  for (const auto &entry : jsonData) {
    TaskConfig data;
    std::string task_n = entry["name"];
    data.name = task_n;
    j_task_name.insert(task_n);
    data.priority = entry["priority"];
    data.core = entry["processor"];

    // Split the time1 string and store as pairs in the data structure
    std::vector<std::string> time1Strings = splitString(entry["time"], ',');
    for (const auto &time : time1Strings) {
      std::vector<std::string> units = splitString(time, ' ');
      if (units.size() == 2) {
        data.time.emplace_back(stoi(units[0]), stoi(units[1]));
      }
    }
    if (entry.find("lock") != entry.end()) {
      for (const auto &lock : entry["lock"]) {
        data.lock.push_back(lock);
        locks.insert(lock);
      }
    }
    //      for (const auto& time : entry["lock"]) {
    //        data.lock_.push_back(time);
    //      }
    // Validate the size of time1 and time2
    if (data.lock.size() + 1 != data.time.size()) {
      // TODO: QT version messagebox
      BOOST_LOG_TRIVIAL(error)
          << "Error: time1 size should be one more than time2 size";
      // Handle the error in a way that suits your needs
      // You can throw an exception, skip the data, or handle it accordingly
      continue;
    }
    tc.push_back(data);
    task.insert(std::make_pair(entry["name"], data));
  }
}

std::map<int, std::vector<TaskConfig>> Config::classify_priority() {
  std::map<int, std::vector<TaskConfig>> tp;
  for (const auto &t : tc) {
    // std::cout << "------------core index error ------------" << std::endl;
    int core_index = t.core;
    tp[core_index].push_back(t);
  }
  for (auto &it : tp) {
    std::sort(it.second.begin(), it.second.end(),
              [](const TaskConfig &task1, const TaskConfig &task2) {
                return task1.priority < task2.priority;
              });
  }
  return tp;
}
void Config::parse_sub_dag() {
  boost::ref_property_map<SubDAG *, std::string> dag_name(
      get_property(sub_dag, boost::graph_name));
  sub_dag_dp.property("name", dag_name);
  sub_dag_dp.property("node_id", get(&SubDAGVertex::name, sub_dag));
  sub_dag_dp.property("label", get(&SubDAGVertex::label, sub_dag));
  sub_dag_dp.property("shape", get(&SubDAGVertex::shape, sub_dag));
  // sub_dag_dp.property("subgraph",
  //                    boost::get(&SubDAGVertex::subgraph_id, sub_dag));
  sub_dag_dp.property("label", get(&SubDAGEdge::label, sub_dag));
  sub_dag_dp.property("style", get(&SubDAGEdge::style, sub_dag));

  std::ifstream dag_f(dag_file);
  std::map<std::string, SubDAG::vertex_descriptor> vertexMap;
  typename boost::property_map<SubDAG, boost::vertex_index_t>::type index =
      get(boost::vertex_index, sub_dag);
  typedef boost::subgraph<SubDAG> Subgraph;

  read_graphviz(dag_f, sub_dag, sub_dag_dp);
}
