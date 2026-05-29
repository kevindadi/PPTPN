#include <gtest/gtest.h>
#include "analysis/graph.h"
#include "analysis/scheduling.h"
#include "petri/petri.h"

using state_class::SchedulingAlgorithms;
using petri::PTPN;
using petri::TimeInterval;

class SchedulingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ptpn_ = create_simple_ptpn();
  }

  PTPN create_simple_ptpn() {
    PTPN ptpn;

    // 创建简单的 PTPN：
    // 2 个核心，每个核心 2 个变迁
    // 核心 0: T0 (priority=10), T1 (priority=5)
    // 核心 1: T2 (priority=10), T3 (priority=5)
    // 还有一个控制变迁 T4 (core=-1)

    // 添加 2 个 places
    ptpn.add_place("P0");
    ptpn.add_place("P1");

    // T0: 核心 0, 优先级 10
    ptpn.add_transition("T0", TimeInterval(1, 5), 10, 0, true);
    // T1: 核心 0, 优先级 5
    ptpn.add_transition("T1", TimeInterval(2, 8), 5, 0, true);
    // T2: 核心 1, 优先级 10
    ptpn.add_transition("T2", TimeInterval(1, 5), 10, 1, true);
    // T3: 核心 1, 优先级 5
    ptpn.add_transition("T3", TimeInterval(2, 8), 5, 1, true);
    // T4: 控制变迁，不在任何核心上
    ptpn.add_transition("T4", TimeInterval(0, 10), 0, -1, true);

    // 设置弧
    // P0 -> T0, P0 -> T1, P1 -> T2, P1 -> T3
    // T0 -> P0, T1 -> P1, T2 -> P1, T3 -> P0
    ptpn.set_pre_arc(0, 0, 1);
    ptpn.set_pre_arc(0, 1, 1);
    ptpn.set_pre_arc(1, 2, 1);
    ptpn.set_pre_arc(1, 3, 1);

    ptpn.set_post_arc(0, 0, 1);
    ptpn.set_post_arc(1, 1, 1);
    ptpn.set_post_arc(2, 1, 1);
    ptpn.set_post_arc(3, 0, 1);

    // 设置初始标识
    ptpn.set_initial_marking({1, 1});

    return ptpn;
  }

  PTPN create_priority_test_ptpn() {
    // 创建用于测试优先级的 PTPN
    PTPN ptpn;

    ptpn.add_place("P0");

    // 核心 0: 3 个不同优先级的变迁
    ptpn.add_transition("T0", TimeInterval(1, 5), 100, 0, true);  // 最高优先级
    ptpn.add_transition("T1", TimeInterval(2, 8), 50, 0, true);   // 中优先级
    ptpn.add_transition("T2", TimeInterval(3, 10), 10, 0, true);  // 最低优先级

    ptpn.set_pre_arc(0, 0, 1);
    ptpn.set_pre_arc(0, 1, 1);
    ptpn.set_pre_arc(0, 2, 1);

    ptpn.set_post_arc(0, 0, 1);
    ptpn.set_post_arc(1, 0, 1);
    ptpn.set_post_arc(2, 0, 1);

    ptpn.set_initial_marking({1});

    return ptpn;
  }

  PTPN create_unsuspendable_ptpn() {
    // 创建包含不可挂起变迁的 PTPN
    PTPN ptpn;

    ptpn.add_place("P0");

    // 核心 0: 变迁 T0 不可挂起，T1 可挂起
    ptpn.add_transition("T0", TimeInterval(1, 5), 100, 0, false);  // 不可挂起
    ptpn.add_transition("T1", TimeInterval(2, 8), 50, 0, true);   // 可挂起

    ptpn.set_pre_arc(0, 0, 1);
    ptpn.set_pre_arc(0, 1, 1);

    ptpn.set_post_arc(0, 0, 1);
    ptpn.set_post_arc(1, 0, 1);

    ptpn.set_initial_marking({1});

    return ptpn;
  }

  PTPN ptpn_;
};

TEST_F(SchedulingTest, SelectActivePerCore) {
  std::set<size_t> enabled = {0, 1, 2, 3};

  auto result = SchedulingAlgorithms::select_active_per_core(enabled, ptpn_);

  // 应该选择每个核心上优先级最高的变迁
  // 核心 0: T0 (优先级 10) vs T1 (优先级 5) -> T0
  // 核心 1: T2 (优先级 10) vs T3 (优先级 5) -> T2
  // 控制变迁 T4 不在任何核心上，应该被包含
  // 注意：T4 的优先级是 0，比 T0/T2 的优先级 10 低
  EXPECT_EQ(result.size(), 2);  // T0, T2
  EXPECT_TRUE(result.count(0));  // 核心 0 最高优先级
  EXPECT_TRUE(result.count(2));  // 核心 1 最高优先级
  EXPECT_FALSE(result.count(1));  // 核心 0 次高优先级
  EXPECT_FALSE(result.count(3));  // 核心 1 次高优先级
}

TEST_F(SchedulingTest, SelectActivePerCoreWithControlOnly) {
  // 只启用控制变迁
  std::set<size_t> enabled = {4};  // T4 是控制变迁

  auto result = SchedulingAlgorithms::select_active_per_core(enabled, ptpn_);

  EXPECT_EQ(result.size(), 1);
  EXPECT_TRUE(result.count(4));
}

TEST_F(SchedulingTest, ComputeSuspended) {
  // T0 正在活跃，T1、T2、T3 使能
  std::set<size_t> enabled = {0, 1, 2, 3};
  std::set<size_t> active = {0};

  auto result = SchedulingAlgorithms::compute_suspended(enabled, active, ptpn_);

  // T1 应该被挂起（核心 0 有更高优先级的 T0）
  // T2, T3 不应该被挂起（活跃中）
  EXPECT_EQ(result.size(), 1);
  EXPECT_TRUE(result.count(1));
}

TEST_F(SchedulingTest, ComputeSuspendedNoHigherPriority) {
  // 如果没有更高优先级的活跃变迁，不应该挂起
  std::set<size_t> enabled = {1, 2, 3};
  std::set<size_t> active = {};  // 没有任何活跃变迁

  auto result = SchedulingAlgorithms::compute_suspended(enabled, active, ptpn_);

  // 没有活跃变迁，所有可挂起变迁都不应该被挂起
  EXPECT_TRUE(result.empty());
}

TEST_F(SchedulingTest, ShouldSuspend) {
  std::set<size_t> active = {0};  // T0 活跃

  // T1 应该挂起（核心 0 有更高优先级 T0）
  EXPECT_TRUE(SchedulingAlgorithms::should_suspend(1, active, ptpn_));

  // T2, T3 不应该挂起（不同核心）
  EXPECT_FALSE(SchedulingAlgorithms::should_suspend(2, active, ptpn_));
  EXPECT_FALSE(SchedulingAlgorithms::should_suspend(3, active, ptpn_));
}

TEST_F(SchedulingTest, ShouldSuspendWithUnsuspendable) {
  PTPN ptpn = create_unsuspendable_ptpn();

  std::set<size_t> active = {0};  // T0 活跃且不可挂起

  // T1 可挂起，但 T0 不可挂起
  // should_suspend 不检查高优先级变迁是否可挂起
  EXPECT_TRUE(SchedulingAlgorithms::should_suspend(1, active, ptpn));
}

TEST_F(SchedulingTest, ShouldNotSuspendControlTransition) {
  std::set<size_t> active = {4};

  // T4 是控制变迁，不应该被挂起
  EXPECT_FALSE(SchedulingAlgorithms::should_suspend(4, active, ptpn_));
}

TEST_F(SchedulingTest, ShouldRestore) {
  // T1 在核心 0，T0 不在活跃集合中
  std::set<size_t> active = {2, 3};  // T2, T3 活跃

  // T1 应该恢复（核心 0 上没有更高优先级活跃变迁）
  EXPECT_TRUE(SchedulingAlgorithms::should_restore(1, active, ptpn_));

  // T3 不应该恢复（T2 在核心 1，优先级 10 > 5）
  EXPECT_FALSE(SchedulingAlgorithms::should_restore(3, active, ptpn_));
}

TEST_F(SchedulingTest, ShouldRestoreWithEmptyActive) {
  std::set<size_t> active = {};

  // 没有任何活跃变迁时，所有可挂起变迁都应该恢复
  EXPECT_TRUE(SchedulingAlgorithms::should_restore(0, active, ptpn_));
  EXPECT_TRUE(SchedulingAlgorithms::should_restore(1, active, ptpn_));
}

TEST_F(SchedulingTest, GetHigherPriorityActive) {
  // T0 在核心 0 优先级 10，T1 在核心 0 优先级 5
  std::set<size_t> active = {0, 1, 2};  // T0, T1, T2 活跃

  // T1 在核心 0 优先级 5，应该找到更高优先级的 T0
  auto higher = SchedulingAlgorithms::get_higher_priority_active(1, active, ptpn_);

  // T1 在核心 0，更高优先级的 T0 也在核心 0
  EXPECT_EQ(higher.size(), 1);
  EXPECT_TRUE(higher.count(0));
}

TEST_F(SchedulingTest, GetHigherPriorityActiveSamePriority) {
  // T0 (core=0, priority=10) 和 T2 (core=1, priority=10) 活跃
  std::set<size_t> active = {0, 2};

  // T3 在核心 1 优先级 5，同核心的 T2 优先级 10 更高
  auto higher = SchedulingAlgorithms::get_higher_priority_active(3, active, ptpn_);

  // T2 在核心 1 且优先级更高，应该被找到
  EXPECT_EQ(higher.size(), 1);
  EXPECT_TRUE(higher.count(2));
}

TEST_F(SchedulingTest, GetHigherPriorityActiveDifferentCore) {
  std::set<size_t> active = {0};  // T0 活跃在核心 0

  // T2 在核心 1，不应该找到核心 0 上的 T0
  auto higher = SchedulingAlgorithms::get_higher_priority_active(2, active, ptpn_);

  EXPECT_TRUE(higher.empty());
}

TEST_F(SchedulingTest, PriorityTest) {
  PTPN ptpn = create_priority_test_ptpn();

  std::set<size_t> enabled = {0, 1, 2};

  auto result = SchedulingAlgorithms::select_active_per_core(enabled, ptpn);

  // 应该选择 T0（最高优先级）
  EXPECT_EQ(result.size(), 1);
  EXPECT_TRUE(result.count(0));
}

TEST_F(SchedulingTest, ComputeSuspendedWithHigherPriority) {
  PTPN ptpn = create_priority_test_ptpn();

  std::set<size_t> enabled = {0, 1, 2};
  std::set<size_t> active = {0};  // T0 活跃

  auto result = SchedulingAlgorithms::compute_suspended(enabled, active, ptpn);

  // T1 和 T2 应该被挂起（核心 0 有更高优先级 T0）
  EXPECT_EQ(result.size(), 2);
  EXPECT_TRUE(result.count(1));
  EXPECT_TRUE(result.count(2));
}

TEST_F(SchedulingTest, SuspendedTransitionCannotFireWithTime) {
  PTPN ptpn = create_priority_test_ptpn();
  state_class::StateClassReachabilityGraph graph(ptpn);
  state_class::StateClass state = graph.create_initial_state();

  ASSERT_TRUE(state.enabled.count(1));
  ASSERT_FALSE(state.active.count(1));
  ASSERT_TRUE(state.suspended.count(1));
  ASSERT_EQ(state.clocks[1].state, state_class::ClockState::SUSPENDED);

  auto [ok, next, firing_time] = graph.fire_with_time(1, state);

  EXPECT_FALSE(ok);
  EXPECT_TRUE(next.marking.empty());
  EXPECT_DOUBLE_EQ(firing_time, 0.0);
}

TEST_F(SchedulingTest, EmptyEnabled) {
  std::set<size_t> enabled = {};
  std::set<size_t> active = {};

  auto result = SchedulingAlgorithms::select_active_per_core(enabled, ptpn_);
  EXPECT_TRUE(result.empty());

  auto suspended = SchedulingAlgorithms::compute_suspended(enabled, active, ptpn_);
  EXPECT_TRUE(suspended.empty());
}

TEST_F(SchedulingTest, AllTransitionsSameCore) {
  // 创建一个所有变迁都在同一核心的 PTPN
  PTPN ptpn;

  ptpn.add_place("P0");

  ptpn.add_transition("T0", TimeInterval(1, 5), 100, 0, true);
  ptpn.add_transition("T1", TimeInterval(2, 8), 80, 0, true);
  ptpn.add_transition("T2", TimeInterval(3, 10), 60, 0, true);

  ptpn.set_pre_arc(0, 0, 1);
  ptpn.set_pre_arc(0, 1, 1);
  ptpn.set_pre_arc(0, 2, 1);

  ptpn.set_post_arc(0, 0, 1);
  ptpn.set_post_arc(1, 0, 1);
  ptpn.set_post_arc(2, 0, 1);

  ptpn.set_initial_marking({1});

  std::set<size_t> enabled = {0, 1, 2};

  auto result = SchedulingAlgorithms::select_active_per_core(enabled, ptpn);

  // 应该只选择最高优先级的一个变迁
  EXPECT_EQ(result.size(), 1);
  EXPECT_TRUE(result.count(0));
}