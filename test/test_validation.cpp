#include <gtest/gtest.h>
#include "tdg/tdg.h"

using tdg::JsonTDGParser;

class ValidationTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

TEST_F(ValidationTest, DuplicateNodeId) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": []},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100], "locks": []},
      {"id": "TaskA", "type": "periodic", "priority": 98, "core": 0, "time": [[2, 5]], "period": [200, 200], "locks": []}
    ],
    "edges": []
  })";

  JsonTDGParser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_FALSE(validation.success);

  bool has_duplicate_error = false;
  for (const auto& err : validation.errors) {
    if (err.find("Duplicate node ID") != std::string::npos) {
      has_duplicate_error = true;
      break;
    }
  }
  EXPECT_TRUE(has_duplicate_error);
}

TEST_F(ValidationTest, UnknownNodeType) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": []},
    "nodes": [
      {"id": "TaskA", "type": "unknown_type", "priority": 97, "core": 0, "time": [[3, 8]], "locks": []}
    ],
    "edges": []
  })";

  JsonTDGParser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_FALSE(validation.success);

  bool has_type_error = false;
  for (const auto& err : validation.errors) {
    if (err.find("Unknown node type") != std::string::npos) {
      has_type_error = true;
      break;
    }
  }
  EXPECT_TRUE(has_type_error);
}

TEST_F(ValidationTest, InvalidCoreNumber) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 4, "shared_locks": []},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 10, "time": [[3, 8]], "period": [100, 100], "locks": []}
    ],
    "edges": []
  })";

  JsonTDGParser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_FALSE(validation.success);

  bool has_core_error = false;
  for (const auto& err : validation.errors) {
    if (err.find("invalid core") != std::string::npos) {
      has_core_error = true;
      break;
    }
  }
  EXPECT_TRUE(has_core_error);
}

TEST_F(ValidationTest, EdgeReferencesUnknownNode) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": []},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100], "locks": []}
    ],
    "edges": [
      {"source": "TaskA", "target": "UnknownNode"}
    ]
  })";

  JsonTDGParser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_FALSE(validation.success);

  bool has_edge_error = false;
  for (const auto& err : validation.errors) {
    if (err.find("unknown target") != std::string::npos) {
      has_edge_error = true;
      break;
    }
  }
  EXPECT_TRUE(has_edge_error);
}

TEST_F(ValidationTest, PeriodicTaskMissingPeriod) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": []},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "locks": []}
    ],
    "edges": []
  })";

  JsonTDGParser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_FALSE(validation.success);

  bool has_period_error = false;
  for (const auto& err : validation.errors) {
    if (err.find("missing") != std::string::npos && err.find("period") != std::string::npos) {
      has_period_error = true;
      break;
    }
  }
  EXPECT_TRUE(has_period_error);
}

TEST_F(ValidationTest, UndefinedLock) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": ["lock1"]},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100], "locks": ["lock2"]}
    ],
    "edges": []
  })";

  JsonTDGParser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_FALSE(validation.success);

  bool has_lock_error = false;
  for (const auto& err : validation.errors) {
    if (err.find("undefined lock") != std::string::npos) {
      has_lock_error = true;
      break;
    }
  }
  EXPECT_TRUE(has_lock_error);
}

TEST_F(ValidationTest, ValidInputNoErrors) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 2, "cores_per_cpu": 4, "shared_locks": ["lock1", "lock2"]},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100], "locks": ["lock1"]},
      {"id": "TaskB", "type": "aperiodic", "priority": 98, "core": 1, "time": [[2, 5]], "locks": []}
    ],
    "edges": [
      {"source": "TaskA", "target": "TaskB"}
    ]
  })";

  JsonTDGParser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_TRUE(validation.success);
  EXPECT_TRUE(validation.errors.empty());
}

TEST_F(ValidationTest, InvalidTimeInterval) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": []},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[8, 3]], "period": [100, 100], "locks": []}
    ],
    "edges": []
  })";

  JsonTDGParser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_FALSE(validation.success);

  bool has_time_error = false;
  for (const auto& err : validation.errors) {
    if (err.find("invalid time interval") != std::string::npos) {
      has_time_error = true;
      break;
    }
  }
  EXPECT_TRUE(has_time_error);
}

TEST_F(ValidationTest, NoTaskNodesWarning) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": []},
    "nodes": [
      {"id": "Fork1", "type": "fork"},
      {"id": "Join1", "type": "join"}
    ],
    "edges": [
      {"source": "Fork1", "target": "Join1"}
    ]
  })";

  JsonTDGParser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_TRUE(validation.success);

  bool has_no_task_warning = false;
  for (const auto& warn : validation.warnings) {
    if (warn.find("no task nodes") != std::string::npos) {
      has_no_task_warning = true;
      break;
    }
  }
  EXPECT_TRUE(has_no_task_warning);
}

TEST_F(ValidationTest, ForkNodeWithTaskAttributesWarning) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": []},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[3, 8]], "period": [100, 100], "locks": []},
      {"id": "Fork1", "type": "fork", "priority": 50, "core": 1}
    ],
    "edges": [
      {"source": "TaskA", "target": "Fork1"}
    ]
  })";

  JsonTDGParser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_TRUE(validation.success);
  EXPECT_FALSE(validation.warnings.empty());
}