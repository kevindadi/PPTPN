#include <gtest/gtest.h>
#include "dbm.h"

namespace scheduling {

class DBMTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST(DBMTest, construction) {
  DBM dbm(1);
  EXPECT_EQ(dbm.size(), 1);
  EXPECT_EQ(dbm.get_constraint(0, 0), 0);  // c_0 - c_0 ≤ 0
}

TEST(DBMTest, add_clock) {
  DBM dbm(1);
  size_t idx = dbm.add_clock();
  EXPECT_EQ(dbm.size(), 2);
  EXPECT_EQ(idx, 1);
  EXPECT_EQ(dbm.get_constraint(0, idx), 0);   // c_0 - c_1 ≤ 0
  EXPECT_EQ(dbm.get_constraint(idx, 0), INF_TIME);  // c_1 - c_0 ≤ ∞
}

TEST(DBMTest, set_get_constraint) {
  DBM dbm(2);
  dbm.set_constraint(1, 0, 5);  // c_1 - c_0 ≤ 5
  EXPECT_EQ(dbm.get_constraint(1, 0), 5);
}

TEST(DBMTest, minimize) {
  DBM dbm(3);
  dbm.set_constraint(0, 1, -3);  // c_0 - c_1 ≤ -3 → c_1 - c_0 ≥ 3
  dbm.set_constraint(1, 2, 2);  // c_1 - c_2 ≤ 2
  dbm.minimize();
  // c_0 - c_2 ≤ -1 → c_2 - c_0 ≥ 1
  EXPECT_EQ(dbm.get_constraint(0, 2), -1);
}

TEST(DBMTest, time_elapse) {
  DBM dbm(2);
  dbm.set_constraint(0, 1, 0);   // c_0 - c_1 ≤ 0
  dbm.set_constraint(1, 0, 5);  // c_1 - c_0 ≤ 5

  dbm.elapse_time(2);

  EXPECT_EQ(dbm.get_constraint(0, 1), -2);  // c_0 - c_1 ≤ -2
  EXPECT_EQ(dbm.get_constraint(1, 0), 7);   // c_1 - c_0 ≤ 7
}

TEST(DBMTest, freeze_unfreeze) {
  DBM dbm(2);
  dbm.set_constraint(0, 1, 0);
  dbm.set_constraint(1, 0, 5);

  EXPECT_FALSE(dbm.is_frozen(1));

  dbm.freeze_clock(1);
  EXPECT_TRUE(dbm.is_frozen(1));

  dbm.elapse_time(3);
  // c_1 被冻结，不应变化
  EXPECT_EQ(dbm.get_constraint(0, 1), 0);
  EXPECT_EQ(dbm.get_constraint(1, 0), 5);

  dbm.unfreeze_clock(1);
  EXPECT_FALSE(dbm.is_frozen(1));
}

TEST(DBMTest, intersection) {
  DBM a(2);
  a.set_constraint(0, 1, 0);   // c_1 ≥ 0
  a.set_constraint(1, 0, 10); // c_1 ≤ 10

  DBM b(2);
  b.set_constraint(0, 1, -5);  // c_1 ≥ 5
  b.set_constraint(1, 0, 7);   // c_1 ≤ 7

  DBM result = a.intersection(b);
  EXPECT_EQ(result.get_lower_bound(1), 5);  // c_1 ≥ 5
  EXPECT_EQ(result.get_upper_bound(1), 7);  // c_1 ≤ 7
}

TEST(DBMTest, is_empty) {
  DBM dbm(1);
  EXPECT_FALSE(dbm.is_empty());

  DBM empty_dbm(0);
  EXPECT_TRUE(empty_dbm.is_empty());
}

TEST(DBMTest, equality) {
  DBM a(2);
  a.set_constraint(0, 1, 0);
  a.set_constraint(1, 0, 5);

  DBM b(2);
  b.set_constraint(0, 1, 0);
  b.set_constraint(1, 0, 5);

  EXPECT_EQ(a, b);
}

}  // namespace scheduling