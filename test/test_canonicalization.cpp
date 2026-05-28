#include <gtest/gtest.h>
#include "analysis/canonicalization.h"
#include "analysis/state.h"

using state_class::ClockState;
using state_class::TransitionClock;
using state_class::CanonicalizationMode;
using state_class::StateClass;

class CanonicalizationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 创建两个测试状态
    state_a_ = create_test_state_a();
    state_b_ = create_test_state_b();
  }

  StateClass create_test_state_a() {
    StateClass s;
    s.marking = {1, 0, 1};
    s.clocks.resize(3);
    s.clocks[0] = TransitionClock(5, ClockState::ACTIVE);
    s.clocks[1] = TransitionClock(8, ClockState::SUSPENDED);
    s.clocks[2] = TransitionClock(10, ClockState::UNACTIVE);
    s.enabled = {0, 1, 2};
    s.active = {0};
    s.suspended = {1};
    s.cumulative_time = 5.0;
    s.state_id = 1;
    return s;
  }

  StateClass create_test_state_b() {
    StateClass s;
    s.marking = {1, 0, 1};
    s.clocks.resize(3);
    s.clocks[0] = TransitionClock(6, ClockState::ACTIVE);
    s.clocks[1] = TransitionClock(8, ClockState::SUSPENDED);
    s.clocks[2] = TransitionClock(12, ClockState::UNACTIVE);
    s.enabled = {0, 2};
    s.active = {0};
    s.suspended = {};
    s.cumulative_time = 6.0;
    s.state_id = 2;
    return s;
  }

  StateClass state_a_;
  StateClass state_b_;
};

TEST_F(CanonicalizationTest, MarkingMustMatch) {
  // 创建 marking 不同的状态
  StateClass s1;
  s1.marking = {1, 0};
  s1.clocks.resize(2);

  StateClass s2;
  s2.marking = {0, 1};  // 不同
  s2.clocks.resize(2);

  auto result = state_class::canonicalize(s1, s2, CanonicalizationMode::EQUALITY);

  // marking 不同应该返回空结果
  EXPECT_TRUE(result.marking.empty());
}

TEST_F(CanonicalizationTest, EqualityMode) {
  auto result = state_class::canonicalize(state_a_, state_b_, CanonicalizationMode::EQUALITY);

  EXPECT_EQ(result.marking, state_a_.marking);
  // EQUALITY 模式使用第一个状态的时钟值
  EXPECT_EQ(result.clocks[0].lower_bound, state_a_.clocks[0].lower_bound);
  EXPECT_EQ(result.clocks[0].upper_bound, state_a_.clocks[0].upper_bound);
}

TEST_F(CanonicalizationTest, MaxLowerBoundMode) {
  auto result = state_class::canonicalize(state_a_, state_b_, CanonicalizationMode::MAX_LOWER_BOUND);

  EXPECT_EQ(result.marking, state_a_.marking);

  // 时钟 0: a=[0,5], b=[0,6] -> max_lower=0, min_upper=5
  EXPECT_EQ(result.clocks[0].lower_bound, 0);
  EXPECT_EQ(result.clocks[0].upper_bound, 5);

  // 时钟 1: a=[0,8], b=[0,8] -> max_lower=0, min_upper=8
  EXPECT_EQ(result.clocks[1].lower_bound, 0);
  EXPECT_EQ(result.clocks[1].upper_bound, 8);

  // 时钟 2: a=[0,10], b=[0,12] -> max_lower=0, min_upper=10
  EXPECT_EQ(result.clocks[2].lower_bound, 0);
  EXPECT_EQ(result.clocks[2].upper_bound, 10);
}

TEST_F(CanonicalizationTest, IntersectionMode) {
  auto result = state_class::canonicalize(state_a_, state_b_, CanonicalizationMode::INTERSECTION);

  EXPECT_EQ(result.marking, state_a_.marking);

  // INTERSECTION 模式：取最大下界、最小上界
  // 时钟 0: max(0,0)=0, min(5,6)=5
  EXPECT_EQ(result.clocks[0].lower_bound, 0);
  EXPECT_EQ(result.clocks[0].upper_bound, 5);

  // 时钟 1: a 有状态 SUSPENDED，b 有状态 SUSPENDED -> 状态相同
  EXPECT_EQ(result.clocks[1].state, ClockState::SUSPENDED);
}

TEST_F(CanonicalizationTest, EnabledSetUnion) {
  auto result = state_class::canonicalize(state_a_, state_b_, CanonicalizationMode::MAX_LOWER_BOUND);

  // enabled 应该是并集: {0,1,2} U {0,2} = {0,1,2}
  EXPECT_EQ(result.enabled.size(), 3);
  EXPECT_TRUE(result.enabled.count(0));
  EXPECT_TRUE(result.enabled.count(1));
  EXPECT_TRUE(result.enabled.count(2));
}

TEST_F(CanonicalizationTest, ActiveSetIntersection) {
  auto result = state_class::canonicalize(state_a_, state_b_, CanonicalizationMode::MAX_LOWER_BOUND);

  // active 应该是交集: {0} ∩ {0} = {0}
  EXPECT_EQ(result.active.size(), 1);
  EXPECT_TRUE(result.active.count(0));
}

TEST_F(CanonicalizationTest, SuspendedSetIntersection) {
  auto result = state_class::canonicalize(state_a_, state_b_, CanonicalizationMode::MAX_LOWER_BOUND);

  // suspended 应该是交集: {1} ∩ {} = {}
  EXPECT_TRUE(result.suspended.empty());
}

TEST_F(CanonicalizationTest, MetadataPreserved) {
  auto result = state_class::canonicalize(state_a_, state_b_, CanonicalizationMode::MAX_LOWER_BOUND);

  // 使用较大的 state_id 和累计时间
  EXPECT_EQ(result.state_id, std::max(state_a_.state_id, state_b_.state_id));
  EXPECT_EQ(result.cumulative_time, std::max(state_a_.cumulative_time, state_b_.cumulative_time));
}

TEST_F(CanonicalizationTest, AreEquivalentEqualityMode) {
  // 创建两个完全相同的状态
  StateClass s1;
  s1.marking = {1, 1};
  s1.clocks.resize(2);
  s1.clocks[0] = TransitionClock(5, ClockState::ACTIVE);
  s1.clocks[1] = TransitionClock(8, ClockState::SUSPENDED);

  StateClass s2;
  s2.marking = {1, 1};
  s2.clocks.resize(2);
  s2.clocks[0] = TransitionClock(5, ClockState::ACTIVE);
  s2.clocks[1] = TransitionClock(8, ClockState::SUSPENDED);

  EXPECT_TRUE(state_class::are_equivalent(s1, s2, CanonicalizationMode::EQUALITY));
}

TEST_F(CanonicalizationTest, AreEquivalentDifferentMarking) {
  StateClass s1;
  s1.marking = {1, 0};

  StateClass s2;
  s2.marking = {0, 1};

  EXPECT_FALSE(state_class::are_equivalent(s1, s2, CanonicalizationMode::EQUALITY));
}

TEST_F(CanonicalizationTest, AreEquivalentDifferentClocks) {
  StateClass s1;
  s1.marking = {1, 1};
  s1.clocks.resize(2);
  s1.clocks[0] = TransitionClock(5, ClockState::ACTIVE);
  s1.clocks[1] = TransitionClock(8, ClockState::SUSPENDED);

  StateClass s2;
  s2.marking = {1, 1};
  s2.clocks.resize(2);
  s2.clocks[0] = TransitionClock(5, ClockState::ACTIVE);
  s2.clocks[1] = TransitionClock(9, ClockState::SUSPENDED);  // 不同上界

  EXPECT_FALSE(state_class::are_equivalent(s1, s2, CanonicalizationMode::EQUALITY));
}

TEST_F(CanonicalizationTest, AreEquivalentMaxLowerBoundMode) {
  // MAX_LOWER_BOUND 模式下，只要 lower 和 upper 相同就等价（忽略状态）
  StateClass s1;
  s1.marking = {1, 1};
  s1.clocks.resize(2);
  s1.clocks[0] = TransitionClock(5, ClockState::ACTIVE);
  s1.clocks[1] = TransitionClock(8, ClockState::SUSPENDED);

  StateClass s2;
  s2.marking = {1, 1};
  s2.clocks.resize(2);
  s2.clocks[0] = TransitionClock(5, ClockState::UNACTIVE);  // 不同状态
  s2.clocks[1] = TransitionClock(8, ClockState::ACTIVE);    // 不同状态

  EXPECT_TRUE(state_class::are_equivalent(s1, s2, CanonicalizationMode::MAX_LOWER_BOUND));
}

TEST_F(CanonicalizationTest, AreEquivalentDifferentLowerBound) {
  // MAX_LOWER_BOUND 模式下，lower 不同就不等价
  StateClass s1;
  s1.marking = {1, 1};
  s1.clocks.resize(2);
  s1.clocks[0] = TransitionClock(5, ClockState::ACTIVE);

  StateClass s2;
  s2.marking = {1, 1};
  s2.clocks.resize(2);
  s2.clocks[0] = TransitionClock(6, ClockState::ACTIVE);  // 不同 lower

  EXPECT_FALSE(state_class::are_equivalent(s1, s2, CanonicalizationMode::MAX_LOWER_BOUND));
}

TEST_F(CanonicalizationTest, AreEquivalentIntersectionMode) {
  StateClass s1;
  s1.marking = {1, 1};
  s1.clocks.resize(2);
  s1.clocks[0] = TransitionClock(5, ClockState::ACTIVE);
  s1.clocks[1] = TransitionClock(8, ClockState::SUSPENDED);

  StateClass s2;
  s2.marking = {1, 1};
  s2.clocks.resize(2);
  s2.clocks[0] = TransitionClock(5, ClockState::ACTIVE);
  s2.clocks[1] = TransitionClock(8, ClockState::SUSPENDED);

  EXPECT_TRUE(state_class::are_equivalent(s1, s2, CanonicalizationMode::INTERSECTION));
}

TEST_F(CanonicalizationTest, DifferentClockSizes) {
  StateClass s1;
  s1.marking = {1};
  s1.clocks.resize(1);

  StateClass s2;
  s2.marking = {1};
  s2.clocks.resize(2);

  // 时钟数量不同，are_equivalent 应该返回 false
  EXPECT_FALSE(state_class::are_equivalent(s1, s2, CanonicalizationMode::EQUALITY));
}

TEST_F(CanonicalizationTest, EmptyClocks) {
  StateClass s1;
  s1.marking = {1, 0};

  StateClass s2;
  s2.marking = {1, 0};

  auto result = state_class::canonicalize(s1, s2, CanonicalizationMode::MAX_LOWER_BOUND);

  EXPECT_EQ(result.marking, s1.marking);
  EXPECT_TRUE(result.clocks.empty());
}