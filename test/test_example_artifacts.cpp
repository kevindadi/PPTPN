#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "analysis/state.h"
#include "json/json.h"
#include "petri/graph.h"
#include "petri/petri.h"
#include "tdg/tdg.h"
#include "tdg2pn/tdg2pn.h"

namespace fs = std::filesystem;

namespace {

fs::path repo_root() {
  return fs::path(__FILE__).parent_path().parent_path();
}

std::string read_file(const fs::path& path) {
  std::ifstream file(path);
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

int compute_expected_wcet(const TaskNode& task) {
  int wcet = 0;
  for (const auto& interval : task.time) {
    wcet += interval.second;
  }
  return wcet;
}

void verify_example_case(const fs::path& case_dir, size_t max_states = 256) {
  const fs::path input_path = repo_root() / case_dir / "input.json";
  const fs::path tdg_dot_path = repo_root() / case_dir / "tdg.dot";
  const fs::path ptpn_dot_path = repo_root() / case_dir / "ptpn.dot";
  const fs::path state_class_dot_path = repo_root() / case_dir / "state-class-graph.dot";
  const fs::path expected_json_path = repo_root() / case_dir / "expected.json";

  ASSERT_TRUE(fs::exists(input_path)) << input_path;
  ASSERT_TRUE(fs::exists(tdg_dot_path)) << tdg_dot_path;

  parse::Parser parser;
  auto parse_result = parser.parse_file(input_path.string());
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  auto validation = parser.validate();
  ASSERT_TRUE(validation.success);

  tdg::TDG tdg(parser.get_num_cpus(), parser.get_cores_per_cpu());
  tdg.parse_json(input_path.string());

  ASSERT_FALSE(tdg.all_task.empty());
  const std::string tdg_dot = read_file(tdg_dot_path);
  EXPECT_FALSE(tdg_dot.empty());
  for (const auto& node : tdg.all_task) {
    if (!std::holds_alternative<TaskNode>(node)) {
      continue;
    }
    const auto& task = std::get<TaskNode>(node);
    EXPECT_NE(tdg_dot.find(task.name), std::string::npos);
  }

  petri::PTPN ptpn;
  converter::TDG2PN::transform(tdg, ptpn);
  EXPECT_GT(ptpn.num_places(), 0U);
  EXPECT_GT(ptpn.num_transitions(), 0U);

  graph::GraphPTPN graph_ptpn(ptpn);
  ASSERT_TRUE(graph_ptpn.save_to_dot(ptpn_dot_path.string()));
  const std::string ptpn_dot = read_file(ptpn_dot_path);
  EXPECT_FALSE(ptpn_dot.empty());

  state_class::StateClassReachabilityGraph reachability_graph(ptpn);
  const size_t state_count = reachability_graph.build(max_states);
  EXPECT_GT(state_count, 0U);
  ASSERT_TRUE(reachability_graph.save_to_dot(state_class_dot_path.string()));
  const std::string state_class_dot = read_file(state_class_dot_path);
  EXPECT_FALSE(state_class_dot.empty());
  EXPECT_NE(state_class_dot.find("digraph StateClassGraph"), std::string::npos);

  nlohmann::json wcet_json;
  wcet_json["tasks"] = nlohmann::json::array();
  size_t task_id = 0;
  for (const auto& node : tdg.all_task) {
    if (!std::holds_alternative<TaskNode>(node)) {
      continue;
    }
    const auto& task = std::get<TaskNode>(node);
    wcet_json["tasks"].push_back({
        {"id", task_id++},
        {"name", task.name},
        {"wcet", compute_expected_wcet(task)},
        {"segments", task.time.size()},
    });
  }

  const fs::path wcet_json_path = repo_root() / case_dir / "wcet.json";
  std::ofstream wcet_out(wcet_json_path);
  ASSERT_TRUE(wcet_out.is_open());
  wcet_out << wcet_json.dump(2) << '\n';
  wcet_out.close();

  ASSERT_TRUE(fs::exists(wcet_json_path));
  const auto parsed_wcet = nlohmann::json::parse(read_file(wcet_json_path));
  ASSERT_TRUE(parsed_wcet.contains("tasks"));
  EXPECT_EQ(parsed_wcet["tasks"].size(), task_id);
  for (const auto& task_entry : parsed_wcet["tasks"]) {
    EXPECT_TRUE(task_entry.contains("name"));
    EXPECT_TRUE(task_entry.contains("wcet"));
    EXPECT_TRUE(task_entry.contains("segments"));
  }

  if (fs::exists(expected_json_path)) {
    const auto expected = nlohmann::json::parse(read_file(expected_json_path));
    EXPECT_FALSE(expected.empty());
  }
}

const parse::JsonNode* find_node(const parse::Parser& parser,
                                 const std::string& node_id) {
  for (const auto& node : parser.get_nodes()) {
    if (node.id == node_id) {
      return &node;
    }
  }
  return nullptr;
}

bool has_edge(const parse::Parser& parser, const std::string& source,
              const std::string& target) {
  for (const auto& edge : parser.get_edges()) {
    if (edge.source == source && edge.target == target) {
      return true;
    }
  }
  return false;
}

void verify_preemption_delayed_structure() {
  const fs::path input_path = repo_root() / "example" / "bench" /
                              "preemption_delayed" / "input.json";
  parse::Parser parser;
  auto parse_result = parser.parse_file(input_path.string());
  ASSERT_TRUE(parse_result.success) << parse_result.error_message;

  const auto* a = find_node(parser, "A");
  const auto* b = find_node(parser, "B");
  const auto* c = find_node(parser, "C");
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_NE(c, nullptr);

  EXPECT_EQ(a->core, b->core);
  EXPECT_NE(c->core, a->core);
  EXPECT_GT(b->priority, a->priority);
  ASSERT_EQ(c->time.size(), 1U);
  EXPECT_EQ(c->time[0].first, 2);
  EXPECT_EQ(c->time[0].second, 2);
  EXPECT_TRUE(has_edge(parser, "C", "B"));
}

}  // namespace

TEST(ExampleArtifactsTest, CommonExample) {
  verify_example_case(fs::path("example") / "common");
}

TEST(ExampleArtifactsTest, SingleLockExample) {
  verify_example_case(fs::path("example") / "single_lock");
}

TEST(ExampleArtifactsTest, NestedLockExample) {
  verify_example_case(fs::path("example") / "nested_lock");
}

TEST(ExampleArtifactsTest, BenchPreemptionDelayed) {
  verify_example_case(fs::path("example") / "bench" / "preemption_delayed", 512);
  verify_preemption_delayed_structure();
}

TEST(ExampleArtifactsTest, BenchPriorityTieNoPreemption) {
  verify_example_case(fs::path("example") / "bench" / "priority_tie_no_preemption", 512);
}

TEST(ExampleArtifactsTest, BenchMulticoreNoPreemption) {
  verify_example_case(fs::path("example") / "bench" / "multicore_no_preemption", 512);
}

TEST(ExampleArtifactsTest, BenchPreemptionChain) {
  verify_example_case(fs::path("example") / "bench" / "preemption_chain", 1024);
}

TEST(ExampleArtifactsTest, BenchLockContention) {
  verify_example_case(fs::path("example") / "bench" / "lock_contention", 1024);
}

TEST(ExampleArtifactsTest, BenchLargeLayered) {
  verify_example_case(fs::path("example") / "bench" / "large_layered", 5000);
}
