#ifndef DOT_TDG_H
#define DOT_TDG_H

#include <set>
#include <vector>

#include "dag.h"

struct TaskConfig {
  int core;
  int priority;
  vector<pair<int, int>> times;
  vector<string> locks;
};

enum TDGVertexType { TASK, FORK, JOIN, EMPTY };

class TDG {
 public:
  TDG() = default;
  TDG(int num_cpus, int cores_per_cpu)
      : num_cpus(num_cpus), cores_per_cpu(cores_per_cpu) {}

 public:
  int num_cpus = 1;
  int cores_per_cpu = 1;

  vector<NodeType> all_task;
  std::unordered_map<string, int> tasks_priority;
  std::unordered_map<string, TDGVertexType> vertexes_type;
  std::unordered_map<string, NodeType> nodes_type;
  std::unordered_map<string, TaskType> tasks_type;
  set<string> lock_set;
  std::map<string, vector<string>> task_locks_map;
  std::unordered_map<string, TaskConfig> tasks_config;

  std::vector<std::tuple<string, string, string, string>> tdg_edges;

 public:
  void parse_json(const std::string& json_file);
  void parse_json_string(const std::string& json_content);

  void export_to_dot(const std::string& output_path);
  std::string to_dot_string() const;

  std::unordered_map<int, vector<string>> classify_priority();
};

#endif