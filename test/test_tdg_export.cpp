#include <gtest/gtest.h>
#include <fstream>
#include "tdg/tdg.h"
#include "json/json.h"
#include "tdg2pn/tdg2pn.h"
#include "petri/petri.h"

using tdg::TDG;
using parse::Parser;

class TdgExportTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

TEST_F(TdgExportTest, TdgDotExport) {
  std::string json = R"({
    "graph": {"name": "DotExportTest"},
    "configuration": {
      "num_cpus": 2,
      "cores_per_cpu": 4,
      "shared_locks": ["lock1"],
      "periodic": [{"task": "TaskA", "period": 100}]
    },
    "nodes": [
      {"id": "TaskA", "type": "task", "priority": 97, "core": 0, "time": [[3, 8]], "locks": ["lock1"]},
      {"id": "TaskB", "type": "task", "priority": 98, "core": 1, "time": [[2, 5]], "locks": []}
    ],
    "edges": [
      {"source": "TaskA", "target": "TaskB", "label": "50", "style": "dashed"}
    ]
  })";

  Parser parser;
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
    "configuration": {
      "num_cpus": 2,
      "cores_per_cpu": 4,
      "shared_locks": ["lock1", "lock2"],
      "periodic": [{"task": "TaskA", "period": 100}, {"task": "TaskB", "period": 200}]
    },
    "nodes": [
      {"id": "TaskA", "type": "task", "priority": 97, "core": 0, "time": [[3, 8]], "locks": ["lock1"]},
      {"id": "TaskB", "type": "task", "priority": 95, "core": 1, "time": [[5, 10]], "locks": ["lock2"]},
      {"id": "TaskC", "type": "task", "priority": 99, "core": 0, "time": [[1, 3]], "locks": []},
      {"id": "Fork1", "type": "fork"}
    ],
    "edges": [
      {"source": "TaskA", "target": "TaskB"},
      {"source": "TaskB", "target": "Fork1"}
    ]
  })";

  Parser parser;
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
    "configuration": {
      "num_cpus": 1,
      "cores_per_cpu": 1,
      "shared_locks": [],
      "periodic": [{"task": "TaskA", "period": 100}]
    },
    "nodes": [
      {"id": "TaskA", "type": "task", "priority": 97, "core": 0, "time": [[3, 8]], "locks": []}
    ],
    "edges": []
  })";

  Parser parser;
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
    "configuration": {
      "num_cpus": 1,
      "cores_per_cpu": 1,
      "shared_locks": [],
      "periodic": [{"task": "TaskA", "period": 100}]
    },
    "nodes": [
      {"id": "TaskA", "type": "task", "priority": 97, "core": 0, "time": [[3, 8]], "locks": []},
      {"id": "Fork1", "type": "fork"},
      {"id": "Fork2", "type": "fork"},
      {"id": "Join1", "type": "join"},
      {"id": "TaskB", "type": "task", "priority": 98, "core": 0, "time": [[2, 5]], "locks": []}
    ],
    "edges": [
      {"source": "TaskA", "target": "Fork1"},
      {"source": "Fork1", "target": "Fork2"},
      {"source": "Fork2", "target": "Join1"},
      {"source": "Join1", "target": "TaskB"}
    ]
  })";

  Parser parser;
  auto result = parser.parse_string(json);
  ASSERT_TRUE(result.success);

  TDG tdg(parser.get_num_cpus(), parser.get_cores_per_cpu());
  tdg.parse_json_string(json);

  EXPECT_EQ(tdg.nodes_type.size(), 5);

  std::string dot = tdg.to_dot_string();
  EXPECT_NE(dot.find("Fork1"), std::string::npos);
  EXPECT_NE(dot.find("Join1"), std::string::npos);
}

TEST_F(TdgExportTest, EmptyNode) {
  std::string json = R"({
    "graph": {"name": "EmptyTest"},
    "configuration": {
      "num_cpus": 1,
      "cores_per_cpu": 1,
      "shared_locks": [],
      "periodic": [{"task": "TaskA", "period": 100}]
    },
    "nodes": [
      {"id": "TaskA", "type": "task", "priority": 97, "core": 0, "time": [[3, 8]], "locks": []},
      {"id": "Empty1", "type": "empty"},
      {"id": "TaskB", "type": "task", "priority": 98, "core": 0, "time": [[2, 5]], "locks": []}
    ],
    "edges": [
      {"source": "TaskA", "target": "Empty1"},
      {"source": "Empty1", "target": "TaskB"}
    ]
  })";

  Parser parser;
  auto result = parser.parse_string(json);
  ASSERT_TRUE(result.success);

  TDG tdg(parser.get_num_cpus(), parser.get_cores_per_cpu());
  tdg.parse_json_string(json);

  std::string dot = tdg.to_dot_string();
  EXPECT_NE(dot.find("Empty1"), std::string::npos);
}

TEST_F(TdgExportTest, FixedPriorityRestartPreemptionReturnsLowTaskToEntry) {
  std::string json = R"({
    "graph": {"name": "FixedPriorityRestart"},
    "configuration": {
      "num_cpus": 1,
      "cores_per_cpu": 1,
      "shared_locks": [],
      "policy": "fixed_prior_with_restart"
    },
    "nodes": [
      {"id": "LowTask", "type": "task", "priority": 90, "core": 0, "time": [[1, 2]], "locks": []},
      {"id": "HighTask", "type": "task", "priority": 100, "core": 0, "time": [[1, 1]], "locks": []}
    ],
    "edges": []
  })";

  TDG tdg;
  tdg.parse_json_string(json);

  petri::PTPN ptpn;
  converter::TDG2PN::transform(tdg, ptpn);

  const auto low_it = ptpn.node_start_end_map.find("LowTask");
  const auto high_it = ptpn.node_start_end_map.find("HighTask");
  ASSERT_NE(low_it, ptpn.node_start_end_map.end());
  ASSERT_NE(high_it, ptpn.node_start_end_map.end());

  const size_t low_entry = low_it->second.first;
  const size_t low_ready = ptpn.node_pn_map.at("LowTask")[2];
  const size_t high_entry = high_it->second.first;
  const size_t high_ready = ptpn.node_pn_map.at("HighTask")[2];

  bool found_restart_preempt = false;
  for (size_t t = 0; t < ptpn.transitions.size(); ++t) {
    const auto& transition = ptpn.transitions[t];
    if (transition.name.find("HighTask_restart_preempt_LowTask") == std::string::npos) {
      continue;
    }
    found_restart_preempt = true;
    EXPECT_EQ(ptpn.get_pre_matrix()[high_entry][t], 1);
    EXPECT_EQ(ptpn.get_pre_matrix()[low_ready][t], 1);
    EXPECT_EQ(ptpn.get_post_matrix()[t][high_ready], 1);
    EXPECT_EQ(ptpn.get_post_matrix()[t][low_entry], 1);
  }

  EXPECT_TRUE(found_restart_preempt);
}

TEST_F(TdgExportTest, FixedPriorityResumePreemptionRestoresLowTaskToPreemptionPoint) {
  std::string json = R"({
    "graph": {"name": "FixedPriorityResume"},
    "configuration": {
      "num_cpus": 1,
      "cores_per_cpu": 1,
      "shared_locks": [],
      "policy": "fixed_prior_with_resume"
    },
    "nodes": [
      {"id": "LowTask", "type": "task", "priority": 90, "core": 0, "time": [[1, 2]], "locks": []},
      {"id": "HighTask", "type": "task", "priority": 100, "core": 0, "time": [[1, 1]], "locks": []}
    ],
    "edges": []
  })";

  TDG tdg;
  tdg.parse_json_string(json);

  petri::PTPN ptpn;
  converter::TDG2PN::transform(tdg, ptpn);

  const auto low_it = ptpn.node_start_end_map.find("LowTask");
  const auto high_it = ptpn.node_start_end_map.find("HighTask");
  ASSERT_NE(low_it, ptpn.node_start_end_map.end());
  ASSERT_NE(high_it, ptpn.node_start_end_map.end());

  const size_t low_ready = ptpn.node_pn_map.at("LowTask")[2];
  const size_t high_entry = high_it->second.first;
  const size_t high_ready = ptpn.node_pn_map.at("HighTask")[2];
  const size_t high_exit = high_it->second.second;

  ssize_t suspended_place = -1;
  size_t preempt_transition = 0;
  size_t resume_transition = 0;

  for (size_t p = 0; p < ptpn.places.size(); ++p) {
    if (ptpn.places[p].name.find("LowTask_suspended_HighTask") != std::string::npos) {
      suspended_place = static_cast<ssize_t>(p);
      break;
    }
  }
  ASSERT_NE(suspended_place, -1);

  bool found_resume_preempt = false;
  bool found_resume_transition = false;
  for (size_t t = 0; t < ptpn.transitions.size(); ++t) {
    const auto& transition = ptpn.transitions[t];
    if (transition.name.find("HighTask_resume_preempt_LowTask") != std::string::npos) {
      found_resume_preempt = true;
      preempt_transition = t;
    }
    if (transition.name.find("LowTask_resume_HighTask") != std::string::npos) {
      found_resume_transition = true;
      resume_transition = t;
    }
  }

  ASSERT_TRUE(found_resume_preempt);
  ASSERT_TRUE(found_resume_transition);

  EXPECT_EQ(ptpn.get_pre_matrix()[high_entry][preempt_transition], 1);
  EXPECT_EQ(ptpn.get_pre_matrix()[low_ready][preempt_transition], 1);
  EXPECT_EQ(ptpn.get_post_matrix()[preempt_transition][high_ready], 1);
  EXPECT_EQ(ptpn.get_post_matrix()[preempt_transition][suspended_place], 1);

  EXPECT_EQ(ptpn.get_pre_matrix()[suspended_place][resume_transition], 1);
  EXPECT_EQ(ptpn.get_pre_matrix()[high_exit][resume_transition], 1);
  EXPECT_EQ(ptpn.get_post_matrix()[resume_transition][low_ready], 1);
}

TEST_F(TdgExportTest, FixedPriorityVariantsDoNotAddSpinLockPreemptionPaths) {
  std::string restart_json = R"({
    "graph": {"name": "SpinRestart"},
    "configuration": {
      "num_cpus": 1,
      "cores_per_cpu": 1,
      "shared_locks": ["spin1"],
      "policy": "fixed_prior_with_restart"
    },
    "nodes": [
      {"id": "LowTask", "type": "task", "priority": 90, "core": 0, "time": [[1, 2], [2, 3], [3, 4]], "locks": ["spin1"]},
      {"id": "HighTask", "type": "task", "priority": 100, "core": 0, "time": [[1, 1]], "locks": []}
    ],
    "edges": []
  })";

  TDG restart_tdg;
  restart_tdg.parse_json_string(restart_json);

  petri::PTPN restart_ptpn;
  converter::TDG2PN::transform(restart_tdg, restart_ptpn);

  bool has_restart_lock_preempt = false;
  for (const auto& transition : restart_ptpn.transitions) {
    if (transition.name.find("restart_lock_preempt") != std::string::npos) {
      has_restart_lock_preempt = true;
      break;
    }
  }
  EXPECT_FALSE(has_restart_lock_preempt);

  std::string resume_json = R"({
    "graph": {"name": "SpinResume"},
    "configuration": {
      "num_cpus": 1,
      "cores_per_cpu": 1,
      "shared_locks": ["spin1"],
      "policy": "fixed_prior_with_resume"
    },
    "nodes": [
      {"id": "LowTask", "type": "task", "priority": 90, "core": 0, "time": [[1, 2], [2, 3], [3, 4]], "locks": ["spin1"]},
      {"id": "HighTask", "type": "task", "priority": 100, "core": 0, "time": [[1, 1]], "locks": []}
    ],
    "edges": []
  })";

  TDG resume_tdg;
  resume_tdg.parse_json_string(resume_json);

  petri::PTPN resume_ptpn;
  converter::TDG2PN::transform(resume_tdg, resume_ptpn);

  bool has_resume_lock_preempt = false;
  bool has_lock_suspended_place = false;
  for (const auto& transition : resume_ptpn.transitions) {
    if (transition.name.find("resume_lock_preempt") != std::string::npos) {
      has_resume_lock_preempt = true;
      break;
    }
  }
  for (const auto& place : resume_ptpn.places) {
    if (place.name.find("lock_suspended") != std::string::npos) {
      has_lock_suspended_place = true;
      break;
    }
  }

  EXPECT_FALSE(has_resume_lock_preempt);
  EXPECT_FALSE(has_lock_suspended_place);
}

TEST_F(TdgExportTest, ForkJoinTransitionEdgeRules) {
  std::string valid_json = R"({
    "graph": {"name": "ForkJoinEdgeRules"},
    "configuration": {
      "num_cpus": 1,
      "cores_per_cpu": 1,
      "shared_locks": []
    },
    "nodes": [
      {"id": "TaskA", "type": "task", "priority": 97, "core": 0, "time": [[1, 2]], "locks": []},
      {"id": "Fork1", "type": "fork"},
      {"id": "Join1", "type": "join"},
      {"id": "TaskB", "type": "task", "priority": 98, "core": 0, "time": [[2, 3]], "locks": []}
    ],
    "edges": [
      {"source": "TaskA", "target": "Fork1"},
      {"source": "Join1", "target": "TaskB"}
    ]
  })";

  TDG valid_tdg;
  valid_tdg.parse_json_string(valid_json);

  petri::PTPN valid_ptpn;
  EXPECT_NO_THROW(converter::TDG2PN::transform(valid_tdg, valid_ptpn));

  bool has_task_to_fork_arc = false;
  bool has_join_to_task_arc = false;

  const auto fork_it = valid_tdg.nodes_type.find("Fork1");
  const auto join_it = valid_tdg.nodes_type.find("Join1");
  ASSERT_NE(fork_it, valid_tdg.nodes_type.end());
  ASSERT_NE(join_it, valid_tdg.nodes_type.end());

  ASSERT_TRUE(std::holds_alternative<ForkTask>(fork_it->second));
  ASSERT_TRUE(std::holds_alternative<JoinTask>(join_it->second));

  const auto task_a_it = valid_ptpn.node_start_end_map.find("TaskA");
  const auto task_b_it = valid_ptpn.node_start_end_map.find("TaskB");
  const auto fork_map_it = valid_ptpn.node_start_end_map.find("Fork1");
  const auto join_map_it = valid_ptpn.node_start_end_map.find("Join1");
  ASSERT_NE(task_a_it, valid_ptpn.node_start_end_map.end());
  ASSERT_NE(task_b_it, valid_ptpn.node_start_end_map.end());
  ASSERT_NE(fork_map_it, valid_ptpn.node_start_end_map.end());
  ASSERT_NE(join_map_it, valid_ptpn.node_start_end_map.end());

  const size_t task_a_exit = task_a_it->second.second;
  const size_t task_b_entry = task_b_it->second.first;
  const size_t fork_transition = fork_map_it->second.first;
  const size_t join_transition = join_map_it->second.first;

  ASSERT_LT(task_a_exit, valid_ptpn.places.size());
  ASSERT_LT(task_b_entry, valid_ptpn.places.size());
  ASSERT_LT(fork_transition, valid_ptpn.transitions.size());
  ASSERT_LT(join_transition, valid_ptpn.transitions.size());

  has_task_to_fork_arc = valid_ptpn.get_pre_matrix()[task_a_exit][fork_transition] == 1;
  has_join_to_task_arc = valid_ptpn.get_post_matrix()[join_transition][task_b_entry] == 1;

  EXPECT_TRUE(has_task_to_fork_arc);
  EXPECT_TRUE(has_join_to_task_arc);

  std::string invalid_json = R"({
    "graph": {"name": "InvalidForkJoinEdgeRules"},
    "configuration": {
      "num_cpus": 1,
      "cores_per_cpu": 1,
      "shared_locks": []
    },
    "nodes": [
      {"id": "TaskA", "type": "task", "priority": 97, "core": 0, "time": [[1, 2]], "locks": []},
      {"id": "Fork1", "type": "fork"},
      {"id": "Join1", "type": "join"}
    ],
    "edges": [
      {"source": "TaskA", "target": "Fork1"},
      {"source": "Fork1", "target": "Join1"}
    ]
  })";

  TDG invalid_tdg;
  invalid_tdg.parse_json_string(invalid_json);

  petri::PTPN invalid_ptpn;
  EXPECT_THROW(converter::TDG2PN::transform(invalid_tdg, invalid_ptpn), std::runtime_error);
}
