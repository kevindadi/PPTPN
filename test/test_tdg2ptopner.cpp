#include <fstream>
#include <gtest/gtest.h>

#include "petri/petri.h"
#include "tdg/tdg.h"
#include "tdg2pn/tdg2pn.h"
#include "tdg2ptopner/export_ppn.h"
#include "tdg2ptopner/ptpn_to_ppn.h"
#include "tdg2ptopner/tdg2ptopner.h"
#include "tdg2ptopner/validate.h"

namespace {

tdg::TDG make_two_task_tdg() {
  tdg::TDG tdg(1, 1);
  tdg.policy = SchedulePolicy::FIXED_PRIOR_WITH_RESTART;

  TaskNode high;
  high.name = "C";
  high.core = 0;
  high.priority = 98;
  high.time = {{5, 5}};

  TaskNode low_a;
  low_a.name = "A";
  low_a.core = 0;
  low_a.priority = 97;
  low_a.time = {{3, 3}};

  TaskNode low_b;
  low_b.name = "B";
  low_b.core = 0;
  low_b.priority = 96;
  low_b.time = {{2, 2}};

  tdg.all_task = {high, low_a, low_b};
  tdg.nodes_type = {{"C", high}, {"A", low_a}, {"B", low_b}};
  tdg.tasks_priority = {{"C", 98}, {"A", 97}, {"B", 96}};
  tdg.start_tasks = {{"C", 1}, {"A", 1}, {"B", 1}};
  return tdg;
}

}  // namespace

TEST(Tdg2PtopnerValidateTest, RejectsIntervalTime) {
  tdg::TDG tdg(1, 1);
  tdg.policy = SchedulePolicy::FIXED_PRIOR_WITH_RESTART;
  TaskNode task;
  task.name = "A";
  task.time = {{3, 8}};
  tdg.nodes_type["A"] = task;
  tdg.all_task.push_back(task);

  const auto result = ptopner_export::validate_for_ptopner(tdg);
  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("点区间"), std::string::npos);
}

TEST(Tdg2PtopnerValidateTest, RejectsResumePolicy) {
  tdg::TDG tdg(1, 1);
  tdg.policy = SchedulePolicy::FIXED;
  TaskNode task;
  task.name = "A";
  task.time = {{3, 3}};
  tdg.nodes_type["A"] = task;
  tdg.all_task.push_back(task);

  const auto result = ptopner_export::validate_for_ptopner(tdg);
  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("fixed_prior_with_restart"), std::string::npos);
}

TEST(Tdg2PtopnerValidateTest, RejectsLocks) {
  tdg::TDG tdg(1, 1);
  tdg.policy = SchedulePolicy::FIXED_PRIOR_WITH_RESTART;
  TaskNode task;
  task.name = "A";
  task.time = {{3, 3}};
  task.lock = {"mutex0"};
  tdg.nodes_type["A"] = task;
  tdg.all_task.push_back(task);
  tdg.lock_set.insert("mutex0");

  const auto result = ptopner_export::validate_for_ptopner(tdg);
  EXPECT_FALSE(result.ok);
}

TEST(Tdg2PtopnerValidateTest, AcceptsPointIntervalRestartInput) {
  tdg::TDG tdg(2, 1);
  tdg.parse_json(
      "/home/kevin/PTPN/experiment/point-interval/rt-system/priority/input.json");
  tdg.policy = SchedulePolicy::FIXED_PRIOR_WITH_RESTART;

  const auto result = ptopner_export::validate_for_ptopner(tdg);
  EXPECT_TRUE(result.ok) << result.errors[0];
}

TEST(Tdg2PtopnerPriorTest, MapsExecGetCoreAndPreemptPriors) {
  EXPECT_EQ(ptopner_export::classify_transition("Cget_core"),
            ptopner_export::TransitionRole::GET_CORE);
  EXPECT_EQ(ptopner_export::classify_transition("Aexec"),
            ptopner_export::TransitionRole::EXEC);
  EXPECT_EQ(ptopner_export::classify_transition("C_restart_preempt_A_0"),
            ptopner_export::TransitionRole::PREEMPT);
  EXPECT_EQ(ptopner_export::classify_transition("A_to_B"),
            ptopner_export::TransitionRole::CONTROL);

  EXPECT_FLOAT_EQ(
      ptopner_export::map_prior_to_float(9899, ptopner_export::TransitionRole::GET_CORE),
      98.2F);
  EXPECT_FLOAT_EQ(
      ptopner_export::map_prior_to_float(9898, ptopner_export::TransitionRole::PREEMPT),
      98.1F);
  EXPECT_FLOAT_EQ(
      ptopner_export::map_prior_to_float(9897, ptopner_export::TransitionRole::PREEMPT),
      98.0F);
  EXPECT_FLOAT_EQ(
      ptopner_export::map_prior_to_float(9899, ptopner_export::TransitionRole::EXEC), 0.0F);
}

TEST(Tdg2PtopnerExportTest, WritesParserCompatiblePpn) {
  ptopner_export::PpnModel model;
  model.places = {{"c1", 1}, {"p1", 1}, {"p2", 0}};
  model.transitions = {
      {"t1", {1}, {2}, 5, 0.0F, false},
      {"t2", {0, 2}, {1}, 0, 98.2F, false},
      {"t3", {1}, {0, 2}, 3, 0.0F, true},
  };

  const std::string content = ptopner_export::ppn_to_string(model);
  EXPECT_NE(content.find("transition  preset  postset  time  prior  is_suspend"), std::string::npos);
  EXPECT_NE(content.find("place    name    tokens"), std::string::npos);
  EXPECT_NE(content.find("98.2"), std::string::npos);
  EXPECT_NE(content.find("@"), std::string::npos);

  const std::string path = "/tmp/ptpn-test-mini.ppn";
  ASSERT_TRUE(ptopner_export::export_ppn(model, path));

  std::ifstream in(path);
  ASSERT_TRUE(in.good());
  std::string line;
  std::getline(in, line);
  EXPECT_EQ(line, "transition  preset  postset  time  prior  is_suspend");
}

TEST(Tdg2PtopnerIntegrationTest, EndToEndTwoTaskNetGoldenSnapshot) {
  tdg::TDG tdg = make_two_task_tdg();
  const auto validation = ptopner_export::validate_for_ptopner(tdg);
  ASSERT_TRUE(validation.ok);

  petri::PTPN ptpn;
  converter::TDG2PN::transform(tdg, ptpn);
  const auto model = ptopner_export::ptpn_to_ppn_model(ptpn);

  EXPECT_LE(model.places.size(), static_cast<size_t>(ptopner_export::kPtopnerMaxPlaces));
  EXPECT_LE(model.transitions.size(), static_cast<size_t>(ptopner_export::kPtopnerMaxPlaces));

  bool found_get_core = false;
  bool found_victim_exec_suspend = false;
  bool found_preempt = false;
  for (const auto& transition : model.transitions) {
    if (transition.name.find("get_core") != std::string::npos) {
      found_get_core = true;
      EXPECT_GT(transition.prior, 0.0F);
      EXPECT_FALSE(transition.is_suspend);
    }
    if (transition.name == "Aexec" || transition.name == "Bexec") {
      found_victim_exec_suspend = true;
      EXPECT_FLOAT_EQ(transition.prior, 0.0F);
      EXPECT_TRUE(transition.is_suspend);
    }
    if (transition.name == "Cexec") {
      EXPECT_FLOAT_EQ(transition.prior, 0.0F);
      EXPECT_FALSE(transition.is_suspend);
    }
    if (transition.name.find("restart_preempt_") != std::string::npos) {
      found_preempt = true;
      EXPECT_GT(transition.prior, 0.0F);
      EXPECT_LT(transition.prior, 98.2F);
    }
  }
  EXPECT_TRUE(found_get_core);
  EXPECT_TRUE(found_victim_exec_suspend);
  EXPECT_TRUE(found_preempt);
}

TEST(Tdg2PtopnerIntegrationTest, TransformToPpnFilePriorityInput) {
  tdg::TDG tdg(2, 1);
  tdg.parse_json(
      "/home/kevin/PTPN/experiment/point-interval/rt-system/priority/input.json");
  tdg.policy = SchedulePolicy::FIXED_PRIOR_WITH_RESTART;

  const std::string path = "/tmp/ptpn-priority-input.ppn";
  const auto result = ptopner_export::transform_to_ppn_file(tdg, path);
  ASSERT_TRUE(result.success) << result.error_message;

  std::ifstream in(path);
  ASSERT_TRUE(in.good());
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  EXPECT_NE(content.find("Aget_core"), std::string::npos);
  EXPECT_NE(content.find("restart_preempt_"), std::string::npos);
  EXPECT_NE(content.find("core0"), std::string::npos);
  EXPECT_NE(content.find("0,20."), std::string::npos);
}
