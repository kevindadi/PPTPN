//
// Created by 张凯文 on 2023/6/26.
//

#ifndef PPTPN__CONFIG_H_
#define PPTPN__CONFIG_H_

#include <vector>
#include <set>
#include <boost/graph/graphviz.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_utility.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

struct TaskConfig{
  std::string name;
  int core, priority;
  std::vector<std::pair<int, int>> time;
  std::vector<std::string> lock;
};

struct DAGVertex {
  std::string name, label, shape;
  int tokens;
};

struct DAGEdge {
  std::string label;
  int weight;
};

typedef boost::property<boost::graph_name_t, std::string> graph_p;
typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS, DAGVertex, DAGEdge, graph_p> DAG;

class Config {
 public:
  Config() = default;
  Config(std::string dag_file, std::string task_file);

  void parse_json();
  void parse_dag();

 public:
  // 所有的task属性
  std::vector<TaskConfig> tc;
  // Task Name
  std::set<std::string> j_task_name;
  std::unordered_map<std::size_t, std::string> task_index;
  std::vector<std::string> dag_task_name;
  // Task Name -> Task Config
  std::unordered_map<std::string, TaskConfig> task;
  // Task Sort Priority By Core
  std::unordered_map<std::string, int> priority_map;
  // 同一个核心上的优先级
  std::map<int, std::vector<TaskConfig>> classify_priority();
  // 统计 Locks
  std::set<std::string> locks;

 public:
  boost::dynamic_properties dag_dp;
  DAG dag;
 protected:
  static std::vector<std::string> splitString(const std::string& str, char delimiter);
 private:
  // DAG 图的描述文件
  std::string dag_file;
  // 任务分配的文件
  std::string task_file;
};

#endif //PPTPN__CONFIG_H_
