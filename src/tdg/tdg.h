#ifndef TDG_H
#define TDG_H

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <optional>
#include <sstream>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "../types.h"

namespace tdg {

// Forward declarations for TDG class

class TDG {
 public:
  TDG() = default;
  TDG(int num_cpus, int cores_per_cpu)
      : num_cpus(num_cpus), cores_per_cpu(cores_per_cpu) {}

 public:
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

 public:
  void parse_json(const std::string& json_file);
  void parse_json_string(const std::string& json_content);
  void export_to_dot(const std::string& output_path);
  std::string to_dot_string() const;
  std::unordered_map<int, std::vector<std::string>> classify_priority();
};

// ===== JSON Parser (from json_tdg.h) =====
struct JsonParseResult {
  bool success;
  std::string error_message;
  int error_line = 0;
};

struct ValidationResult {
  bool success = true;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;

  void add_error(const std::string& err) {
    success = false;
    errors.push_back(err);
  }

  void add_warning(const std::string& warn) {
    warnings.push_back(warn);
  }
};

struct JsonNode {
  std::string id;
  std::string type;
  int priority = 100;
  int core = 0;
  std::vector<std::pair<int, int>> time;
  std::pair<int, int> period = {0, 0};
  std::vector<std::string> locks;

  NodeType to_node_type() const;
};

struct JsonEdge {
  std::string source;
  std::string target;
  std::string label;
  std::string style;
};

struct JsonGraph {
  std::string name = "G";
  int num_cpus = 1;
  int cores_per_cpu = 1;
  std::vector<std::string> shared_locks;
  std::vector<JsonNode> nodes;
  std::vector<JsonEdge> edges;
};

class JsonTDGParser {
 public:
  JsonTDGParser() = default;

  JsonParseResult parse_file(const std::string& file_path);
  JsonParseResult parse_string(const std::string& json_content);

  std::string get_graph_name() const { return graph_.name; }
  int get_num_cpus() const { return graph_.num_cpus; }
  int get_cores_per_cpu() const { return graph_.cores_per_cpu; }
  const std::vector<JsonNode>& get_nodes() const { return graph_.nodes; }
  const std::vector<JsonEdge>& get_edges() const { return graph_.edges; }
  const std::string& get_original_json() const { return original_json_; }

  ValidationResult validate() const;
  std::string to_dot_string() const;

 private:
  void parse_graph_object(const nlohmann::json& graph_obj);
  void parse_configuration_object(const nlohmann::json& config);
  void parse_nodes_array(const nlohmann::json& nodes_array);
  void parse_edges_array(const nlohmann::json& edges_array);
  JsonNode parse_node_object(const nlohmann::json& node_obj);

  JsonGraph graph_;
  std::string original_json_;
};

// Helper functions
std::string node_type_to_string(const NodeType& node);
std::string node_to_dot_label(const NodeType& node);

}  // namespace tdg

#endif