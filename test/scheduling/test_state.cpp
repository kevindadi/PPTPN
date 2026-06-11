#include <gtest/gtest.h>
#include "state.h"

namespace scheduling {

class StateTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

TEST_F(StateTest, equality) {
  StateClass a;
  a.marking = {1, 0, 0};
  a.zone = DBM(1);
  a.enabled = {0};
  a.active = {0};
  a.suspended = {};

  StateClass b;
  b.marking = {1, 0, 0};
  b.zone = DBM(1);
  b.enabled = {0};
  b.active = {0};
  b.suspended = {};

  EXPECT_EQ(a, b);
}

TEST_F(StateTest, inequality) {
  StateClass a;
  a.marking = {1, 0, 0};

  StateClass b;
  b.marking = {0, 1, 0};

  EXPECT_NE(a, b);
}

TEST_F(StateTest, clock_mapping) {
  StateClass state;
  state.transition_to_clock = {-1, 1, -1};
  state.clock_to_transition = {std::numeric_limits<size_t>::max(), 1};

  EXPECT_EQ(state.clock_index_for_transition(1), 1);
  EXPECT_EQ(state.transition_for_clock(1), 1);
  EXPECT_TRUE(state.has_clock_for_transition(1));
  EXPECT_FALSE(state.has_clock_for_transition(0));
}

TEST_F(StateTest, copy) {
  StateClass original;
  original.marking = {1, 2, 3};
  original.cumulative_time = 5.0;
  original.state_id = 42;

  auto copy = original.copy();

  EXPECT_EQ(copy.marking, original.marking);
  EXPECT_EQ(copy.cumulative_time, original.cumulative_time);
  EXPECT_EQ(copy.state_id, original.state_id);
}

TEST_F(StateTest, are_equivalent) {
  StateClass a;
  a.marking = {1, 0};
  a.zone = DBM(1);
  a.enabled = {0};
  a.active = {0};
  a.suspended = {};

  StateClass b;
  b.marking = {1, 0};
  b.zone = DBM(1);
  b.enabled = {0};
  b.active = {0};
  b.suspended = {};

  EXPECT_TRUE(are_equivalent(a, b));
}

TEST_F(StateTest, are_not_equivalent) {
  StateClass a;
  a.marking = {1, 0};
  a.active = {0};

  StateClass b;
  b.marking = {1, 0};
  b.active = {1};  // 不同活跃集合

  EXPECT_FALSE(are_equivalent(a, b));
}

}  // namespace scheduling
