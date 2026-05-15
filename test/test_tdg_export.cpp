#include <gtest/gtest.h>
#include <fstream>
#include "tdg/tdg.h"

using tdg::JsonTDGParser;
using tdg::TDG;

class TdgExportTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

TEST_F(TdgExportTest, TdgDotExport) {
  std::string json = R"({
    "graph": {"name": "DotExportTest"},
    "configuration": {"num_cpus": 2, "cores_per_cpu": 4, "shared_locks": ["lock1"]},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100], "locks": ["lock1"]},
      {"id": "TaskB", "type": "aperiodic", "priority": 98, "core": 1, "time": [[2, 5]], "locks": []}
    ],
    "edges": [
      {"source": "TaskA", "target": "TaskB", "label": "50", "style": "dashed"}
    ]
  })";

  JsonTDGParser parser;
  auto result = parser.parse_string(json);
  ASSERT_TRUE(result.success);

  TDG tdg(parser.get_num_cpus(), parser.get_cores_per_cpu());
  tdg.parse_json_string(json);

  std::string dot = tdg.to_dot_string();

  EXPECT_NE(dot.find("digraph G"), std::string::npos);
  EXPECT_NE(dot.find("TaskA"), std::string::npos);
  EXPECT_NE(dot.find("TaskB"), std::string::npos);
  EXPECT_NE(dot.find("TaskA -> TaskB"), std::string::npos);
}

TEST_F(TdgExportTest, TdgJsonParsingIntegration) {
  std::string json = R"({
    "graph": {"name": "TDGTest"},
    "configuration": {"num_cpus": 2, "cores_per_cpu": 4, "shared_locks": ["lock1", "lock2"]},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100], "locks": ["lock1"]},
      {"id": "TaskB", "type": "periodic", "priority": 95, "core": 1, "time": [[5, 10]], "period": [200, 200], "locks": ["lock2"]},
      {"id": "TaskC", "type": "aperiodic", "priority": 99, "core": 0, "time": [[1, 3]], "locks": []},
      {"id": "Fork1", "type": "fork"}
    ],
    "edges": [
      {"source": "TaskA", "target": "TaskB"},
      {"source": "TaskB", "target": "Fork1"}
    ]
  })";

  JsonTDGParser parser;
  auto result = parser.parse_string(json);
  ASSERT_TRUE(result.success);

  TDG tdg(parser.get_num_cpus(), parser.get_cores_per_cpu());
  tdg.parse_json_string(json);

  EXPECT_EQ(tdg.num_cpus, 2);
  EXPECT_EQ(tdg.cores_per_cpu, 4);
  EXPECT_EQ(tdg.nodes_type.size(), 4);

  auto core_task = tdg.classify_priority();
  EXPECT_FALSE(core_task.empty());
}

TEST_F(TdgExportTest, ExportToDotFile) {
  std::string json = R"({
    "graph": {"name": "FileExportTest"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": []},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100], "locks": []}
    ],
    "edges": []
  })";

  JsonTDGParser parser;
  auto result = parser.parse_string(json);
  ASSERT_TRUE(result.success);

  TDG tdg(parser.get_num_cpus(), parser.get_cores_per_cpu());
  tdg.parse_json_string(json);

  std::string temp_file = "/tmp/test_tdg_export.dot";
  tdg.export_to_dot(temp_file);

  std::ifstream file(temp_file);
  ASSERT_TRUE(file.is_open());

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();

  EXPECT_NE(content.find("digraph G"), std::string::npos);
  EXPECT_NE(content.find("TaskA"), std::string::npos);

  file.close();
  std::remove(temp_file.c_str());
}

TEST_F(TdgExportTest, ForkJoinNodes) {
  std::string json = R"({
    "graph": {"name": "ForkJoinTest"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": []},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100], "locks": []},
      {"id": "Fork1", "type": "fork"},
      {"id": "Fork2", "type": "fork"},
      {"id": "Join1", "type": "join"},
      {"id": "TaskB", "type": "aperiodic", "priority": 98, "core": 0, "time": [[2, 5]], "locks": []}
    ],
    "edges": [
      {"source": "TaskA", "target": "Fork1"},
      {"source": "Fork1", "target": "Fork2"},
      {"source": "Fork2", "target": "Join1"},
      {"source": "Join1", "target": "TaskB"}
    ]
  })";

  JsonTDGParser parser;
  auto result = parser.parse_string(json);
  ASSERT_TRUE(result.success);

  TDG tdg(parser.get_num_cpus(), parser.get_cores_per_cpu());
  tdg.parse_json_string(json);

  EXPECT_EQ(tdg.nodes_type.size(), 5);

  std::string dot = tdg.to_dot_string();
  EXPECT_NE(dot.find("ForkFork1"), std::string::npos);
  EXPECT_NE(dot.find("WaitJoin1"), std::string::npos);
}

TEST_F(TdgExportTest, EmptyNode) {
  std::string json = R"({
    "graph": {"name": "EmptyTest"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": []},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100], "locks": []},
      {"id": "Empty1", "type": "empty"},
      {"id": "TaskB", "type": "aperiodic", "priority": 98, "core": 0, "time": [[2, 5]], "locks": []}
    ],
    "edges": [
      {"source": "TaskA", "target": "Empty1"},
      {"source": "Empty1", "target": "TaskB"}
    ]
  })";

  JsonTDGParser parser;
  auto result = parser.parse_string(json);
  ASSERT_TRUE(result.success);

  TDG tdg(parser.get_num_cpus(), parser.get_cores_per_cpu());
  tdg.parse_json_string(json);

  std::string dot = tdg.to_dot_string();
  EXPECT_NE(dot.find("EmptyEmpty1"), std::string::npos);
}