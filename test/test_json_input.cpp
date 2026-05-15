#include "json_tdg.h"
#include "clap.h"
#include <cassert>
#include <iostream>

using namespace std;

bool test_valid_json_parsing() {
  cout << "=== Test 1: Valid JSON Parsing ===" << endl;

  string json = R"({
    "graph": {"name": "TestGraph"},
    "configuration": {"num_cpus": 2, "cores_per_cpu": 4},
    "nodes": [
      {
        "id": "TaskA",
        "type": "periodic",
        "priority": 97,
        "core": 0,
        "time": [[3, 8]],
        "period": [100, 100],
        "locks": ["lock1"]
      },
      {
        "id": "TaskB",
        "type": "aperiodic",
        "priority": 98,
        "core": 1,
        "time": [[2, 5]],
        "locks": []
      },
      {
        "id": "Fork1",
        "type": "fork"
      },
      {
        "id": "Join1",
        "type": "join"
      }
    ],
    "edges": [
      {"source": "TaskA", "target": "TaskB"},
      {"source": "TaskB", "target": "Fork1"},
      {"source": "Fork1", "target": "Join1"},
      {"source": "TaskB", "target": "TaskA", "label": "50", "style": "dashed"}
    ]
  })";

  json_tdg::JsonTDGParser parser;
  auto result = parser.parse_string(json);

  if (!result.success) {
    cout << "Failed: " << result.error_message << endl;
    return false;
  }

  cout << "Graph name: " << parser.get_graph_name() << endl;
  cout << "Nodes: " << parser.get_nodes().size() << endl;
  cout << "Edges: " << parser.get_edges().size() << endl;

  assert(parser.get_graph_name() == "TestGraph");
  assert(parser.get_num_cpus() == 2);
  assert(parser.get_cores_per_cpu() == 4);
  assert(parser.get_nodes().size() == 4);
  assert(parser.get_edges().size() == 4);

  cout << "[PASS] Test 1" << endl;
  return true;
}

bool test_invalid_json_handling() {
  cout << "=== Test 2: Invalid JSON Error Handling ===" << endl;

  string invalid_json = "{ invalid json }";

  json_tdg::JsonTDGParser parser;
  auto result = parser.parse_string(invalid_json);

  if (result.success) {
    cout << "Should have failed parsing invalid JSON" << endl;
    return false;
  }

  cout << "Expected error: " << result.error_message << endl;
  cout << "[PASS] Test 2" << endl;
  return true;
}

bool test_validation() {
  cout << "=== Test 3: Validation (duplicate IDs) ===" << endl;

  string json_with_dupes = R"({
    "graph": {"name": "Test"},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100]},
      {"id": "TaskA", "type": "aperiodic", "priority": 98, "core": 0, "time": [[2, 5]]}
    ],
    "edges": []
  })";

  json_tdg::JsonTDGParser parser;
  auto result = parser.parse_string(json_with_dupes);
  auto validation = parser.validate();

  if (validation.success) {
    cout << "Should have failed validation for duplicate IDs" << endl;
    return false;
  }

  cout << "Validation errors: " << validation.error_message << endl;
  cout << "[PASS] Test 3" << endl;
  return true;
}

bool test_dot_export() {
  cout << "=== Test 4: DOT Export ===" << endl;

  string json = R"({
    "graph": {"name": "ExportTest"},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100]}
    ],
    "edges": []
  })";

  json_tdg::JsonTDGParser parser;
  auto result = parser.parse_string(json);
  assert(result.success);

  string dot = parser.to_dot_string();
  cout << "Exported DOT:\n" << dot << endl;

  assert(dot.find("digraph ExportTest") != string::npos);
  assert(dot.find("TaskA") != string::npos);
  assert(dot.find("label = ") != string::npos);

  cout << "[PASS] Test 4" << endl;
  return true;
}

bool test_tdg_json_parsing() {
  cout << "=== Test 5: TDG JSON Parsing Integration ===" << endl;

  string json = R"({
    "graph": {"name": "TDGTest"},
    "configuration": {"num_cpus": 2, "cores_per_cpu": 4},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100]},
      {"id": "TaskB", "type": "aperiodic", "priority": 98, "core": 0, "time": [[2, 5]]},
      {"id": "Fork1", "type": "fork"},
      {"id": "Join1", "type": "join"}
    ],
    "edges": [
      {"source": "TaskA", "target": "TaskB"}
    ]
  })";

  TDG tdg("test.json", 2, 4);
  tdg.parse_json_string(json);

  cout << "TDG nodes: " << tdg.nodes_type.size() << endl;
  assert(tdg.nodes_type.size() == 4);

  assert(tdg.vertexes_type["TaskA"] == TDGVertexType::TASK);
  assert(tdg.vertexes_type["TaskB"] == TDGVertexType::TASK);
  assert(tdg.vertexes_type["Fork1"] == TDGVertexType::FORK);
  assert(tdg.vertexes_type["Join1"] == TDGVertexType::JOIN);

  assert(tdg.tdg_edges.size() == 1);
  assert(std::get<0>(tdg.tdg_edges[0]) == "TaskA");
  assert(std::get<1>(tdg.tdg_edges[0]) == "TaskB");

  cout << "[PASS] Test 5" << endl;
  return true;
}

bool test_tdg_dot_export() {
  cout << "=== Test 6: TDG DOT Export ===" << endl;

  string json = R"({
    "graph": {"name": "DotExportTest"},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100]},
      {"id": "TaskB", "type": "aperiodic", "priority": 98, "core": 0, "time": [[2, 5]]}
    ],
    "edges": [
      {"source": "TaskA", "target": "TaskB", "style": "dashed"}
    ]
  })";

  TDG tdg("test.json", 1, 1);
  tdg.parse_json_string(json);

  string dot = tdg.to_dot_string();
  cout << "TDG DOT Export:\n" << dot << endl;

  assert(dot.find("digraph") != string::npos);
  assert(dot.find("TaskA") != string::npos);
  assert(dot.find("TaskB") != string::npos);
  assert(dot.find("->") != string::npos);

  cout << "[PASS] Test 6" << endl;
  return true;
}

bool test_format_detection() {
  cout << "=== Test 7: Input Format Detection ===" << endl;

  assert(TDG::detect_format("test.dot") == InputFormat::DOT);
  assert(TDG::detect_format("path/to/file.DOT") == InputFormat::DOT);
  assert(TDG::detect_format("test.json") == InputFormat::JSON);
  assert(TDG::detect_format("path/to/file.JSON") == InputFormat::JSON);

  cout << "[PASS] Test 7" << endl;
  return true;
}

bool test_node_type_conversion() {
  cout << "=== Test 8: Node Type Conversion ===" << endl;

  json_tdg::JsonNode node;
  node.id = "TaskA";
  node.type = "periodic";
  node.priority = 97;
  node.core = 0;
  node.time = {{3, 8}};
  node.period = {100, 100};
  node.locks = {"lock1"};

  NodeType converted = node.to_node_type();
  assert(std::holds_alternative<PeriodicTask>(converted));

  const auto& task = std::get<PeriodicTask>(converted);
  assert(task.name == "TaskA");
  assert(task.priority == 97);
  assert(task.core == 0);
  assert(task.time.size() == 1);
  assert(task.period_time.first == 100);

  cout << "[PASS] Test 8" << endl;
  return true;
}

int main() {
  cout << "==========================================" << endl;
  cout << "PTPN JSON Input Test Suite" << endl;
  cout << "==========================================" << endl;

  int passed = 0;
  int total = 0;

  auto run_test = [&](bool (*test)(), const char* name) {
    total++;
    cout << "\nRunning: " << name << endl;
    if (test()) {
      passed++;
    } else {
      cout << "[FAIL] " << name << endl;
    }
  };

  run_test(test_valid_json_parsing, "Valid JSON Parsing");
  run_test(test_invalid_json_handling, "Invalid JSON Error Handling");
  run_test(test_validation, "Validation (duplicate IDs)");
  run_test(test_dot_export, "DOT Export");
  run_test(test_tdg_json_parsing, "TDG JSON Parsing Integration");
  run_test(test_tdg_dot_export, "TDG DOT Export");
  run_test(test_format_detection, "Input Format Detection");
  run_test(test_node_type_conversion, "Node Type Conversion");

  cout << "\n==========================================" << endl;
  cout << "Test Results: " << passed << "/" << total << " passed" << endl;
  cout << "==========================================" << endl;

  return passed == total ? 0 : 1;
}