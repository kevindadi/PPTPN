#include <gtest/gtest.h>
#include <memory>

#include "analysis/scheduling/scheduler.h"
#include "petri/petri.h"

namespace scheduling {

class SchedulerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ptpn_ = std::make_unique<petri::PTPN>();
  }

  std::unique_ptr<petri::PTPN> ptpn_;
};

TEST_F(SchedulerTest, select_active_per_core_single) {
  // 三个变迁，同一核心，不同优先级
  size_t t_low = ptpn_->add_transition("low", {1, 5}, 1, 0, true);
  size_t t_med = ptpn_->add_transition("med", {2, 3}, 2, 0, true);
  size_t t_high = ptpn_->add_transition("high", {3, 4}, 3, 0, true);

  std::set<size_t> enabled = {t_low, t_med, t_high};
  auto active = SchedulingAlgorithms::select_active_per_core(enabled, *ptpn_);

  EXPECT_EQ(active.size(), 1);
  EXPECT_TRUE(active.count(t_high));
}

TEST_F(SchedulerTest, select_active_per_core_multi) {
  // 两个核心，各有变迁
  size_t t0 = ptpn_->add_transition("t0", {1, 5}, 1, 0, true);
  size_t t1 = ptpn_->add_transition("t1", {2, 3}, 2, 0, true);
  size_t t2 = ptpn_->add_transition("t2", {3, 4}, 1, 1, true);
  size_t t3 = ptpn_->add_transition("t3", {4, 5}, 2, 1, true);

  std::set<size_t> enabled = {t0, t1, t2, t3};
  auto active = SchedulingAlgorithms::select_active_per_core(enabled, *ptpn_);

  EXPECT_EQ(active.size(), 2);
  EXPECT_TRUE(active.count(t1));  // core 0 最高优先级
  EXPECT_TRUE(active.count(t3)); // core 1 最高优先级
}

TEST_F(SchedulerTest, select_active_with_control) {
  // 控制变迁（core < 0）应全部保留
  size_t t_task = ptpn_->add_transition("task", {1, 5}, 1, 0, true);
  size_t t_ctrl = ptpn_->add_transition("ctrl", {0, 0}, 0, -1, false);

  std::set<size_t> enabled = {t_task, t_ctrl};
  auto active = SchedulingAlgorithms::select_active_per_core(enabled, *ptpn_);

  EXPECT_EQ(active.size(), 2);
  EXPECT_TRUE(active.count(t_task));
  EXPECT_TRUE(active.count(t_ctrl));
}

TEST_F(SchedulerTest, compute_suspended) {
  size_t t_low = ptpn_->add_transition("low", {1, 5}, 1, 0, true);
  size_t t_high = ptpn_->add_transition("high", {2, 3}, 2, 0, true);

  std::set<size_t> enabled = {t_low, t_high};
  std::set<size_t> active = {t_high};  // 高优先级活跃

  auto suspended = SchedulingAlgorithms::compute_suspended(
      enabled, active, *ptpn_);

  EXPECT_EQ(suspended.size(), 1);
  EXPECT_TRUE(suspended.count(t_low));
}

TEST_F(SchedulerTest, should_suspend) {
  size_t t_low = ptpn_->add_transition("low", {1, 5}, 1, 0, true);
  size_t t_high = ptpn_->add_transition("high", {2, 3}, 2, 0, true);

  std::set<size_t> active = {t_high};

  EXPECT_TRUE(SchedulingAlgorithms::should_suspend(t_low, active, *ptpn_));
  EXPECT_FALSE(SchedulingAlgorithms::should_suspend(t_high, active, *ptpn_));
}

TEST_F(SchedulerTest, should_restore) {
  size_t t_low = ptpn_->add_transition("low", {1, 5}, 1, 0, true);
  size_t t_high = ptpn_->add_transition("high", {2, 3}, 2, 0, true);

  // 低优先级挂起，高优先级不活跃时应恢复
  std::set<size_t> empty_active;
  EXPECT_TRUE(SchedulingAlgorithms::should_restore(t_low, empty_active, *ptpn_));

  // 低优先级挂起，高优先级活跃时应不恢复
  std::set<size_t> active = {t_high};
  EXPECT_FALSE(SchedulingAlgorithms::should_restore(t_low, active, *ptpn_));
}

}  // namespace scheduling