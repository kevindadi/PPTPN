#include <gtest/gtest.h>
#include <memory>

#include "analysis/scheduling/reachability.h"

namespace scheduling {

class ReachabilityTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ptpn_ = std::make_unique<petri::PTPN>();
  }

  std::unique_ptr<petri::PTPN> ptpn_;
};

TEST_F(ReachabilityTest, simple_chain) {
  // 创建单任务 Petri 网
  // entry -> ready -> exec -> exit
  size_t p_entry = ptpn_->add_place("entry");
  size_t p_ready = ptpn_->add_place("ready");
  size_t p_exit = ptpn_->add_place("exit");

  size_t t_get_core = ptpn_->add_transition(
      "get_core", {0, 0}, 100, 0, false);
  size_t t_exec = ptpn_->add_transition(
      "exec", {3, 5}, 100, 0, true);

  // entry --get_core--> ready --exec--> exit
  ptpn_->set_pre_arc(p_entry, t_get_core);
  ptpn_->set_post_arc(t_get_core, p_ready);
  ptpn_->set_pre_arc(p_ready, t_exec);
  ptpn_->set_post_arc(t_exec, p_exit);

  ptpn_->set_initial_marking(p_entry, 1);

  ReachabilityGraph graph(*ptpn_);
  size_t num_states = graph.build(100);

  // 验证：应有多个状态
  EXPECT_GE(num_states, 2);
}

TEST_F(ReachabilityTest, two_tasks_no_preempt) {
  // 两个任务，无抢占
  size_t p_a_entry = ptpn_->add_place("A_entry");
  size_t p_a_exit = ptpn_->add_place("A_exit");
  size_t p_b_entry = ptpn_->add_place("B_entry");
  size_t p_b_exit = ptpn_->add_place("B_exit");

  // A: entry -> exec -> exit
  size_t t_a_exec = ptpn_->add_transition(
      "A_exec", {3, 5}, 100, 0, true);
  ptpn_->set_pre_arc(p_a_entry, t_a_exec);
  ptpn_->set_post_arc(t_a_exec, p_a_exit);

  // B: entry -> exec -> exit
  size_t t_b_exec = ptpn_->add_transition(
      "B_exec", {2, 4}, 90, 0, true);
  ptpn_->set_pre_arc(p_b_entry, t_b_exec);
  ptpn_->set_post_arc(t_b_exec, p_b_exit);

  // 依赖：A -> B
  size_t t_a_to_b = ptpn_->add_transition(
      "A_to_B", {0, 0}, 0, -1, false);
  ptpn_->set_pre_arc(p_a_exit, t_a_to_b);
  ptpn_->set_post_arc(t_a_to_b, p_b_entry);

  ptpn_->set_initial_marking(p_a_entry, 1);

  ReachabilityGraph graph(*ptpn_);
  size_t num_states = graph.build(100);

  // 验证：应有多个状态
  EXPECT_GE(num_states, 2);
}

TEST_F(ReachabilityTest, statistics) {
  size_t p_entry = ptpn_->add_place("entry");
  size_t p_exit = ptpn_->add_place("exit");

  size_t t_exec = ptpn_->add_transition(
      "exec", {2, 3}, 100, 0, true);
  ptpn_->set_pre_arc(p_entry, t_exec);
  ptpn_->set_post_arc(t_exec, p_exit);

  ptpn_->set_initial_marking(p_entry, 1);

  ReachabilityGraph graph(*ptpn_);
  graph.build(100);

  const auto& stats = graph.get_statistics();
  EXPECT_EQ(stats.total_states, graph.num_states());
  EXPECT_EQ(stats.total_edges, graph.num_edges());
  EXPECT_FALSE(stats.truncated);
}

}  // namespace scheduling