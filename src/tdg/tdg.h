#ifndef TDG_H
#define TDG_H

#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "../types/types.h"

namespace tdg {

class TDG {
 public:
  TDG() = default;
  TDG(int num_cpus, int cores_per_cpu)
      : num_cpus(num_cpus), cores_per_cpu(cores_per_cpu) {}

  int num_cpus = 1;
  int cores_per_cpu = 1;

  std::vector<NodeType> all_task;
  std::unordered_map<std::string, int> tasks_priority;
  std::unordered_map<std::string, TDGVertexType> vertexes_type;
  std::unordered_map<std::string, NodeType> nodes_type;
  std::unordered_map<std::string, TaskType> tasks_type;
  std::set<std::string> lock_set;
  std::map<std::string, std::vector<std::string>> task_locks_map;
  std::unordered_map<std::string, TaskConfig> tasks_config;
  std::vector<std::tuple<std::string, std::string, std::string, std::string>> tdg_edges;

  void parse_json(const std::string& json_file);
  void parse_json_string(const std::string& json_content);
  void export_to_dot(const std::string& output_path);
  std::string to_dot_string() const;
  std::unordered_map<int, std::vector<std::string>> classify_priority();
};

}  // namespace tdg

#endif  // TDG_H