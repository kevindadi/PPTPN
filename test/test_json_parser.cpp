#include <gtest/gtest.h>
#include "tdg/tdg.h"

using tdg::JsonTDGParser;
using tdg::JsonNode;

class JsonParserTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

TEST_F(JsonParserTest, ValidJsonParsing) {
  std::string json = R"({
    "graph": {"name": "TestGraph"},
    "configuration": {"num_cpus": 2, "cores_per_cpu": 4, "shared_locks": ["lock1", "lock2"]},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100], "locks": ["lock1"]},
      {"id": "TaskB", "type": "aperiodic", "priority": 98, "core": 1, "time": [[3, 5]], "locks": []}
    ],
    "edges": [
      {"source": "TaskA", "target": "TaskB"}
    ]
  })";

  JsonTDGParser parser;
  auto result = parser.parse_string(json);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(parser.get_graph_name(), "TestGraph");
  EXPECT_EQ(parser.get_num_cpus(), 2);
  EXPECT_EQ(parser.get_cores_per_cpu(), 4);
  EXPECT_EQ(parser.get_nodes().size(), 2);
  EXPECT_EQ(parser.get_edges().size(), 1);
}

TEST_F(JsonParserTest, InvalidJsonErrorHandling) {
  std::string invalid_json = "{ invalid json }";

  JsonTDGParser parser;
  auto result = parser.parse_string(invalid_json);

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

TEST_F(JsonParserTest, NodeTypeConversion) {
  JsonNode node;
  node.id = "TaskA";
  node.type = "periodic";
  node.priority = 97;
  node.core = 0;
  node.time = {{3, 8}};
  node.period = {100, 100};
  node.locks = {"lock1"};

  NodeType node_type = node.to_node_type();

  EXPECT_TRUE(std::holds_alternative<PeriodicTask>(node_type));
}

TEST_F(JsonParserTest, DotExport) {
  std::string json = R"({
    "graph": {"name": "ExportTest"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": []},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100], "locks": []}
    ],
    "edges": []
  })";

  JsonTDGParser parser;
  auto result = parser.parse_string(json);
  ASSERT_TRUE(result.success);

  std::string dot = parser.to_dot_string();

  EXPECT_NE(dot.find("digraph ExportTest"), std::string::npos);
  EXPECT_NE(dot.find("TaskA"), std::string::npos);
}

TEST_F(JsonParserTest, ParseFileNotFound) {
  JsonTDGParser parser;
  auto result = parser.parse_file("/nonexistent/path/file.json");

  EXPECT_FALSE(result.success);
  EXPECT_NE(result.error_message.find("Failed to open file"), std::string::npos);
}