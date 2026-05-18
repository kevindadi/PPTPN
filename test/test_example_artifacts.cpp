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

void verify_example_case(const fs::path& case_dir) {
  const fs::path input_path = repo_root() / case_dir / "input.json";
  const fs::path tdg_dot_path = repo_root() / case_dir / "tdg.dot";
  const fs::path ptpn_dot_path = repo_root() / case_dir / "ptpn.dot";
  const fs::path state_class_dot_path = repo_root() / case_dir / "state-class-graph.dot";

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
  const size_t state_count = reachability_graph.build(256);
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
