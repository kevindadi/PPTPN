#include <gtest/gtest.h>
#include "analysis/clock_state.h"

using state_class::ClockState;
using state_class::TransitionClock;
using state_class::INF_TIME;

class TransitionClockTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

TEST_F(TransitionClockTest, DefaultConstructor) {
  TransitionClock tc;

  EXPECT_EQ(tc.lower_bound, 0);
  EXPECT_EQ(tc.upper_bound, INF_TIME);
  EXPECT_EQ(tc.state, ClockState::UNACTIVE);
}

TEST_F(TransitionClockTest, ConstructorWithWcet) {
  TransitionClock tc(5);

  EXPECT_EQ(tc.lower_bound, 0);
  EXPECT_EQ(tc.upper_bound, 5);
  EXPECT_EQ(tc.state, ClockState::UNACTIVE);
}

TEST_F(TransitionClockTest, ConstructorWithWcetAndState) {
  TransitionClock tc(10, ClockState::ACTIVE);

  EXPECT_EQ(tc.lower_bound, 0);
  EXPECT_EQ(tc.upper_bound, 10);
  EXPECT_EQ(tc.state, ClockState::ACTIVE);
}

TEST_F(TransitionClockTest, MakeActive) {
  TransitionClock tc = TransitionClock::make_active(8);

  EXPECT_EQ(tc.lower_bound, 0);
  EXPECT_EQ(tc.upper_bound, 8);
  EXPECT_EQ(tc.state, ClockState::ACTIVE);
}

TEST_F(TransitionClockTest, MakeSuspended) {
  // 当前时间 3，原始 WCET 为 10
  TransitionClock tc = TransitionClock::make_suspended(3, 10);

  EXPECT_EQ(tc.lower_bound, 3);
  EXPECT_EQ(tc.upper_bound, 10);
  EXPECT_EQ(tc.state, ClockState::SUSPENDED);
}

TEST_F(TransitionClockTest, EqualityOperator) {
  TransitionClock tc1(5, ClockState::ACTIVE);
  TransitionClock tc2(5, ClockState::ACTIVE);
  TransitionClock tc3(5, ClockState::SUSPENDED);
  TransitionClock tc4(6, ClockState::ACTIVE);

  EXPECT_TRUE(tc1 == tc2);
  EXPECT_FALSE(tc1 == tc3);
  EXPECT_FALSE(tc1 == tc4);
}

TEST_F(TransitionClockTest, LessThanOperator) {
  // 按状态排序: UNACTIVE < ACTIVE < SUSPENDED (根据实际实现)
  TransitionClock unactive(5, ClockState::UNACTIVE);
  TransitionClock active(5, ClockState::ACTIVE);
  TransitionClock suspended(5, ClockState::SUSPENDED);

  EXPECT_TRUE(unactive < active);
  EXPECT_TRUE(active < suspended);
  EXPECT_TRUE(unactive < suspended);

  // 同状态按 lower_bound
  TransitionClock lb3(5, ClockState::ACTIVE);
  lb3.lower_bound = 3;
  TransitionClock lb7(5, ClockState::ACTIVE);
  lb7.lower_bound = 7;
  EXPECT_TRUE(lb3 < lb7);

  // 同状态、同 lower_bound 按 upper_bound
  TransitionClock ub5(5, ClockState::ACTIVE);
  ub5.lower_bound = 4;
  TransitionClock ub10(10, ClockState::ACTIVE);
  ub10.lower_bound = 4;
  EXPECT_TRUE(ub5 < ub10);
}

TEST_F(TransitionClockTest, ToString) {
  // UNACTIVE 状态
  TransitionClock tc1;
  std::string str1 = tc1.to_string();
  EXPECT_NE(str1.find("UNACTIVE"), std::string::npos);
  EXPECT_NE(str1.find("inf"), std::string::npos);

  // ACTIVE 状态
  TransitionClock tc2(8, ClockState::ACTIVE);
  std::string str2 = tc2.to_string();
  EXPECT_NE(str2.find("ACTIVE"), std::string::npos);
  EXPECT_NE(str2.find("8"), std::string::npos);

  // SUSPENDED 状态
  TransitionClock tc3 = TransitionClock::make_suspended(5, 10);
  std::string str3 = tc3.to_string();
  EXPECT_NE(str3.find("SUSPENDED"), std::string::npos);
  EXPECT_NE(str3.find("5"), std::string::npos);
  EXPECT_NE(str3.find("10"), std::string::npos);
}

TEST_F(TransitionClockTest, SuspendedPreservesOriginalWcet) {
  // 挂起时，原始的 WCET 信息需要保留
  // 下界被设置为挂起时的当前时间
  int current_time = 7;
  int original_wcet = 15;

  TransitionClock tc = TransitionClock::make_suspended(current_time, original_wcet);

  // 上界应该等于原始 WCET（表示剩余时间）
  EXPECT_EQ(tc.upper_bound, original_wcet);
  // 下界是挂起时的时间
  EXPECT_EQ(tc.lower_bound, current_time);
}

TEST_F(TransitionClockTest, ClockStateEnumValues) {
  // 验证枚举的实际值（测试实现的行为，不是假设）
  // 枚举值取决于编译器，可以验证状态之间的顺序关系
  ClockState states[] = {ClockState::UNACTIVE, ClockState::ACTIVE, ClockState::SUSPENDED};

  // 验证三个状态都存在且不相等
  EXPECT_NE(states[0], states[1]);
  EXPECT_NE(states[1], states[2]);
  EXPECT_NE(states[0], states[2]);

  // 验证 less-than 操作符能正确排序（验证实际行为）
  TransitionClock tc0(5, states[0]);
  TransitionClock tc1(5, states[1]);
  TransitionClock tc2(5, states[2]);

  EXPECT_TRUE(tc0 < tc1);
  EXPECT_TRUE(tc1 < tc2);
  EXPECT_TRUE(tc0 < tc2);
}