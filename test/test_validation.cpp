#include <gtest/gtest.h>
#include "json/json.h"

using parse::Parser;

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

  Parser parser;
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

  Parser parser;
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

  Parser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_FALSE(validation.success);

  bool has_core_error = false;
  for (const auto& err : validation.errors) {
    if (err.find("Invalid core number") != std::string::npos) {
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

  Parser parser;
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

  Parser parser;
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
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": ["mutex1"]},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[0, 3], [3, 8], [8, 10]], "period": [100, 100], "locks": ["mutex2"]}
    ],
    "edges": []
  })";

  Parser parser;
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
    "configuration": {"num_cpus": 2, "cores_per_cpu": 4, "shared_locks": ["mutex1", "spin1"]},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[0, 3], [3, 8], [8, 10]], "period": [100, 100], "locks": ["mutex1"]},
      {"id": "TaskB", "type": "aperiodic", "priority": 98, "core": 1, "time": [[2, 5]], "locks": []}
    ],
    "edges": [
      {"source": "TaskA", "target": "TaskB"}
    ]
  })";

  Parser parser;
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

  Parser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_FALSE(validation.success);

  bool has_time_error = false;
  for (const auto& err : validation.errors) {
    if (err.find("Invalid time interval") != std::string::npos) {
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

  Parser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_TRUE(validation.success);

  bool has_no_task_warning = false;
  for (const auto& warn : validation.warnings) {
    if (warn.find("No task nodes found") != std::string::npos) {
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
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[0, 10]], "period": [100, 100], "locks": []},
      {"id": "Fork1", "type": "fork", "time": [[0, 5]]}
    ],
    "edges": [
      {"source": "TaskA", "target": "Fork1"}
    ]
  })";

  Parser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_TRUE(validation.success);
  EXPECT_FALSE(validation.warnings.empty());
}

TEST_F(ValidationTest, InvalidLockPrefix) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": ["badlock"]},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[0, 3], [3, 8], [8, 10]], "period": [100, 100], "locks": ["badlock"]}
    ],
    "edges": []
  })";

  Parser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_FALSE(validation.success);

  bool has_prefix_error = false;
  for (const auto& err : validation.errors) {
    if (err.find("invalid lock prefix") != std::string::npos) {
      has_prefix_error = true;
      break;
    }
  }
  EXPECT_TRUE(has_prefix_error);
}

TEST_F(ValidationTest, ValidMutexAndSpinLocks) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": ["mutex1", "spin1"]},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[0, 3], [3, 8], [8, 10]], "period": [100, 100], "locks": ["mutex1"]},
      {"id": "TaskB", "type": "aperiodic", "priority": 98, "core": 0, "time": [[0, 2], [2, 5], [5, 7], [7, 9], [9, 12]], "locks": ["mutex1", "spin1"]}
    ],
    "edges": []
  })";

  Parser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_TRUE(validation.success);
}

TEST_F(ValidationTest, TimeIntervalCountMismatch) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": ["mutex1"]},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[0, 10]], "period": [100, 100], "locks": ["mutex1"]}
    ],
    "edges": []
  })";

  Parser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_FALSE(validation.success);

  bool has_count_error = false;
  for (const auto& err : validation.errors) {
    if (err.find("time interval(s)") != std::string::npos) {
      has_count_error = true;
      break;
    }
  }
  EXPECT_TRUE(has_count_error);
}

TEST_F(ValidationTest, CorrectTimeIntervalCountOneLock) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": ["mutex1"]},
    "nodes": [
      {"id": "TaskA", "type": "periodic", "priority": 97, "core": 0, "time": [[0, 5], [5, 10], [10, 15]], "period": [100, 100], "locks": ["mutex1"]}
    ],
    "edges": []
  })";

  Parser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_TRUE(validation.success);
}

TEST_F(ValidationTest, CorrectTimeIntervalCountTwoLocks) {
  std::string json = R"({
    "graph": {"name": "Test"},
    "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "shared_locks": ["mutex1", "spin1"]},
    "nodes": [
      {"id": "TaskA", "type": "aperiodic", "priority": 97, "core": 0, "time": [[0, 2], [2, 5], [5, 8], [8, 10], [10, 15]], "locks": ["mutex1", "spin1"]}
    ],
    "edges": []
  })";

  Parser parser;
  auto parse_result = parser.parse_string(json);
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  EXPECT_TRUE(validation.success);
}