#include "json_tdg.h"
#include "clap.h"
#include <cassert>
#include <iostream>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>

using namespace std;

void setup_logging() {
  boost::log::add_console_log(std::clog);
  boost::log::add_common_attributes();
  boost::log::core::get()->set_filter(
      boost::log::trivial::severity >= boost::log::trivial::debug);
}

bool test_valid_json_parsing() {
  BOOST_LOG_TRIVIAL(info) << "\n=== Test 1: Valid JSON Parsing ===";

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
      {"source": "TaskB", "label": "50", "style": "dashed"}
    ]
  })";

  json_tdg::JsonTDGParser parser;
  auto result = parser.parse_string(json);

  if (!result.success) {
    BOOST_LOG_TRIVIAL(error) << "Failed to parse JSON: " << result.error_message;
    return false;
  }

  BOOST_LOG_TRIVIAL(info) << "Graph name: " << parser.get_graph_name();
  BOOST_LOG_TRIVIAL(info) << "Nodes: " << parser.get_nodes().size();
  BOOST_LOG_TRIVIAL(info) << "Edges: " << parser.get_edges().size();

  // Verify parsed data
  assert(parser.get_graph_name() == "TestGraph");
  assert(parser.get_num_cpus() == 2);
  assert(parser.get_cores_per_cpu() == 4);
  assert(parser.get_nodes().size() == 4);
  assert(parser.get_edges().size() == 4);

  // Check node types
  bool found_periodic = false, found_aperiodic = false;
  bool found_fork = false, found_join = false;
  for (const auto& node : parser.get_nodes()) {
    if (node.type == "periodic") found_periodic = true;
    if (node.type == "aperiodic") found_aperiodic = true;
    if (node.type == "fork") found_fork = true;
    if (node.type == "join") found_join = true;
  }
  assert(found_periodic && "Missing periodic task");
  assert(found_aperiodic && "Missing aperiodic task");
  assert(found_fork && "Missing fork node");
  assert(found_join && "Missing join node");

  BOOST_LOG_TRIVIAL(info) << "[PASS] Test 1: Valid JSON Parsing";
  return true;
}

bool test_invalid_json_handling() {
  BOOST_LOG_TRIVIAL(info) << "\n=== Test 2: Invalid JSON Error Handling ===";

  string invalid_json = "{ invalid json }";

  json_tdg::JsonTDGParser parser;
  auto result = parser.parse_string(invalid_json);

  if (result.success) {
    BOOST_LOG_TRIVIAL(error) << "Should have failed parsing invalid JSON";
    return false;
  }

  BOOST_LOG_TRIVIAL(info) << "Expected error: " << result.error_message;
  BOOST_LOG_TRIVIAL(info) << "[PASS] Test 2: Invalid JSON Error Handling";
  return true;
}

bool test_validation() {
  BOOST_LOG_TRIVIAL(info) << "\n=== Test 3: Validation (duplicate IDs) ===";

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
    BOOST_LOG_TRIVIAL(error) << "Should have failed validation for duplicate IDs";
    return false;
  }

  BOOST_LOG_TRIVIAL(info) << "Validation errors: " << validation.error_message;
  BOOST_LOG_TRIVIAL(info) << "[PASS] Test 3: Validation (duplicate IDs)";
  return true;
}

bool test_dot_export() {
  BOOST_LOG_TRIVIAL(info) << "\n=== Test 4: DOT Export ===";

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
  BOOST_LOG_TRIVIAL(debug) << "Exported DOT:\n" << dot;

  // Check that DOT contains expected elements
  assert(dot.find("digraph ExportTest") != string::npos);
  assert(dot.find("TaskA") != string::npos);
  assert(dot.find("label = ") != string::npos);

  BOOST_LOG_TRIVIAL(info) << "[PASS] Test 4: DOT Export";
  return true;
}

bool test_tdg_json_parsing() {
  BOOST_LOG_TRIVIAL(info) << "\n=== Test 5: TDG JSON Parsing Integration ===";

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

  // Check that nodes were parsed
  BOOST_LOG_TRIVIAL(info) << "TDG nodes: " << tdg.nodes_type.size();
  assert(tdg.nodes_type.size() == 4);

  // Check node types
  assert(tdg.vertexes_type["TaskA"] == TDGVertexType::TASK);
  assert(tdg.vertexes_type["TaskB"] == TDGVertexType::TASK);
  assert(tdg.vertexes_type["Fork1"] == TDGVertexType::FORK);
  assert(tdg.vertexes_type["Join1"] == TDGVertexType::JOIN);

  // Check edges
  assert(tdg.tdg_edges.size() == 1);
  assert(std::get<0>(tdg.tdg_edges[0]) == "TaskA");
  assert(std::get<1>(tdg.tdg_edges[0]) == "TaskB");

  BOOST_LOG_TRIVIAL(info) << "[PASS] Test 5: TDG JSON Parsing Integration";
  return true;
}

bool test_tdg_dot_export() {
  BOOST_LOG_TRIVIAL(info) << "\n=== Test 6: TDG DOT Export ===";

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
  BOOST_LOG_TRIVIAL(debug) << "TDG DOT Export:\n" << dot;

  // Check that DOT contains expected elements
  assert(dot.find("digraph") != string::npos);
  assert(dot.find("TaskA") != string::npos);
  assert(dot.find("TaskB") != string::npos);
  assert(dot.find("->") != string::npos);

  BOOST_LOG_TRIVIAL(info) << "[PASS] Test 6: TDG DOT Export";
  return true;
}

bool test_format_detection() {
  BOOST_LOG_TRIVIAL(info) << "\n=== Test 7: Input Format Detection ===";

  // Test DOT detection
  assert(TDG::detect_format("test.dot") == InputFormat::DOT);
  assert(TDG::detect_format("path/to/file.DOT") == InputFormat::DOT);

  // Test JSON detection
  assert(TDG::detect_format("test.json") == InputFormat::JSON);
  assert(TDG::detect_format("path/to/file.JSON") == InputFormat::JSON);

  BOOST_LOG_TRIVIAL(info) << "[PASS] Test 7: Input Format Detection";
  return true;
}

bool test_node_type_conversion() {
  BOOST_LOG_TRIVIAL(info) << "\n=== Test 8: Node Type Conversion ===";

  // Test periodic task conversion
  {
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
  }

  // Test aperiodic task conversion
  {
    json_tdg::JsonNode node;
    node.id = "TaskB";
    node.type = "aperiodic";
    node.priority = 98;
    node.core = 1;
    node.time = {{2, 5}};

    NodeType converted = node.to_node_type();
    assert(std::holds_alternative<APeriodicTask>(converted));
  }

  // Test fork node conversion
  {
    json_tdg::JsonNode node;
    node.id = "Fork1";
    node.type = "fork";

    NodeType converted = node.to_node_type();
    assert(std::holds_alternative<ForkTask>(converted));
  }

  // Test join node conversion
  {
    json_tdg::JsonNode node;
    node.id = "Join1";
    node.type = "join";

    NodeType converted = node.to_node_type();
    assert(std::holds_alternative<JoinTask>(converted));
  }

  // Test empty node conversion
  {
    json_tdg::JsonNode node;
    node.id = "Empty1";
    node.type = "empty";

    NodeType converted = node.to_node_type();
    assert(std::holds_alternative<EmptyTask>(converted));
  }

  BOOST_LOG_TRIVIAL(info) << "[PASS] Test 8: Node Type Conversion";
  return true;
}

int main() {
  setup_logging();

  BOOST_LOG_TRIVIAL(info) << "==========================================";
  BOOST_LOG_TRIVIAL(info) << "PTPN JSON Input Test Suite";
  BOOST_LOG_TRIVIAL(info) << "==========================================";

  int passed = 0;
  int total = 0;

  auto run_test = [&](bool (*test)(), const char* name) {
    total++;
    BOOST_LOG_TRIVIAL(info) << "\nRunning: " << name;
    if (test()) {
      passed++;
    } else {
      BOOST_LOG_TRIVIAL(error) << "[FAIL] " << name;
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

  BOOST_LOG_TRIVIAL(info) << "\n==========================================";
  BOOST_LOG_TRIVIAL(info) << "Test Results: " << passed << "/" << total << " passed";
  BOOST_LOG_TRIVIAL(info) << "==========================================";

  return passed == total ? 0 : 1;
}