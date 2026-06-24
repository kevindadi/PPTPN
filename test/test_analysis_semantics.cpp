#include <gtest/gtest.h>

#include <boost/graph/adjacency_list.hpp>

#include "analysis/clock_state.h"
#include "analysis/dbm.h"
#include "analysis/ptpn_analysis.h"
#include "petri/petri.h"

namespace {

petri::PTPN make_time_first_net() {
  petri::PTPN ptpn;

  const size_t input = ptpn.add_place("p0", 1);
  const size_t low_done = ptpn.add_place("p1", 1);
  const size_t high_done = ptpn.add_place("p2", 1);
  ptpn.set_initial_marking(input, 1);

  const size_t low_priority =
      ptpn.add_transition("low_priority", petri::TimeInterval(0, 0), 1, 0, false);
  const size_t high_priority =
      ptpn.add_transition("high_priority", petri::TimeInterval(3, 3), 99, 0, false);

  ptpn.set_pre_arc(input, low_priority, 1);
  ptpn.set_post_arc(low_priority, low_done, 1);

  ptpn.set_pre_arc(input, high_priority, 1);
  ptpn.set_post_arc(high_priority, high_done, 1);

  return ptpn;
}

}  // namespace

TEST(PtpnAnalysisSemanticsTest, PicksEarliestFiringTimeBeforePriority) {
  const petri::PTPN ptpn = make_time_first_net();
  state_class::PTPNAnalyzer analyzer(ptpn);

  ASSERT_EQ(analyzer.build(8), 2u);

  const auto& graph = analyzer.get_graph();
  const auto initial = analyzer.get_initial_vertex();

  std::vector<size_t> fired_transitions;
  for (auto [edge_it, edge_end] = boost::out_edges(initial, graph); edge_it != edge_end;
       ++edge_it) {
    const auto& edge = boost::get(boost::edge_name, graph, *edge_it);
    fired_transitions.push_back(static_cast<size_t>(edge.transition_id));
  }

  ASSERT_EQ(fired_transitions.size(), 1u);
  EXPECT_EQ(fired_transitions[0], 0u);
}

TEST(PtpnAnalysisSemanticsTest, StrictBoundsDoNotBreakTimeFirstPriorityRule) {
  petri::PTPN ptpn;

  const size_t input = ptpn.add_place("p0", 1);
  const size_t low_done = ptpn.add_place("p1", 1);
  const size_t high_done = ptpn.add_place("p2", 1);
  ptpn.set_initial_marking(input, 1);

  const size_t low = ptpn.add_transition(
      "low", petri::TimeInterval(0, 0, false, false), 1, 0, false);
  const size_t high = ptpn.add_transition(
      "high", petri::TimeInterval(0, 3, true, false), 99, 0, false);

  ptpn.set_pre_arc(input, low, 1);
  ptpn.set_post_arc(low, low_done, 1);
  ptpn.set_pre_arc(input, high, 1);
  ptpn.set_post_arc(high, high_done, 1);

  state_class::PTPNAnalyzer analyzer(ptpn);
  ASSERT_EQ(2u, analyzer.build(8));

  const auto& graph = analyzer.get_graph();
  const auto initial = analyzer.get_initial_vertex();
  std::vector<size_t> fired;
  for (auto [edge_it, edge_end] = boost::out_edges(initial, graph); edge_it != edge_end;
       ++edge_it) {
    const auto& edge = boost::get(boost::edge_name, graph, *edge_it);
    fired.push_back(static_cast<size_t>(edge.transition_id));
  }

  ASSERT_EQ(1u, fired.size());
  EXPECT_EQ(low, fired[0]);
}

TEST(PtpnAnalysisSemanticsTest, OpenLowerBoundDelaysFiringByOneTick) {
  petri::PTPN ptpn;
  const size_t input = ptpn.add_place("p0", 1);
  const size_t done = ptpn.add_place("p1", 1);
  ptpn.set_initial_marking(input, 1);

  const size_t task = ptpn.add_transition(
      "task", petri::TimeInterval(1, 3, true, false), 1, 0, false);
  ptpn.set_pre_arc(input, task, 1);
  ptpn.set_post_arc(task, done, 1);

  state_class::PTPNAnalyzer analyzer(ptpn);
  auto state = analyzer.create_initial_state();
  EXPECT_DOUBLE_EQ(2.0, analyzer.advance_time(state));
}

TEST(PtpnAnalysisSemanticsTest, FutureRemovesOnlyUnfrozenLowerBounds) {
  state_class::DBM dbm(3);
  dbm.set_constraint(0, 1, -2);
  dbm.set_constraint(0, 2, -4);
  dbm.freeze_clock(2);

  state_class::reset_dbm_instrumentation();

  dbm.future();

  EXPECT_EQ(dbm.get_constraint(0, 1), state_class::INF_TIME);
  EXPECT_EQ(dbm.get_constraint(0, 2), -4);
  EXPECT_EQ(dbm.get_constraint(1, 0), state_class::INF_TIME);
  EXPECT_EQ(dbm.get_constraint(2, 0), state_class::INF_TIME);
  EXPECT_EQ(state_class::get_dbm_instrumentation().minimize_calls, 1u);
}

TEST(PtpnAnalysisSemanticsTest, ConstrainUpperBoundOnlyTightensFiniteBounds) {
  state_class::DBM dbm(2);
  dbm.set_constraint(1, 0, 9);

  state_class::reset_dbm_instrumentation();

  dbm.constrain_upper_bound(1, 7);
  EXPECT_EQ(dbm.get_constraint(1, 0), 7);

  dbm.constrain_upper_bound(1, 8);
  EXPECT_EQ(dbm.get_constraint(1, 0), 7);

  dbm.constrain_upper_bound(1, state_class::INF_TIME);
  EXPECT_EQ(dbm.get_constraint(1, 0), 7);

  dbm.constrain_upper_bound(3, 5);
  EXPECT_EQ(dbm.get_constraint(1, 0), 7);
  EXPECT_EQ(state_class::get_dbm_instrumentation().minimize_calls, 1u);
}

TEST(PtpnAnalysisSemanticsTest, ConstrainUpperBoundLeavesInfiniteBoundsUnchanged) {
  state_class::DBM dbm(2);

  state_class::reset_dbm_instrumentation();

  dbm.constrain_upper_bound(1, 6);

  EXPECT_EQ(dbm.get_constraint(1, 0), state_class::INF_TIME);
  EXPECT_EQ(state_class::get_dbm_instrumentation().minimize_calls, 0u);
}

TEST(PtpnAnalysisSemanticsTest, SynchronizeClocksForcesPairwiseEquality) {
  state_class::DBM dbm(4);
  dbm.set_constraint(1, 2, 5);
  dbm.set_constraint(2, 1, 3);
  dbm.set_constraint(1, 3, 8);
  dbm.set_constraint(3, 1, 1);
  dbm.set_constraint(2, 3, 7);
  dbm.set_constraint(3, 2, 6);

  state_class::reset_dbm_instrumentation();

  dbm.synchronize_clocks({1, 2, 3, 9});

  EXPECT_EQ(dbm.get_constraint(1, 2), 0);
  EXPECT_EQ(dbm.get_constraint(2, 1), 0);
  EXPECT_EQ(dbm.get_constraint(1, 3), 0);
  EXPECT_EQ(dbm.get_constraint(3, 1), 0);
  EXPECT_EQ(dbm.get_constraint(2, 3), 0);
  EXPECT_EQ(dbm.get_constraint(3, 2), 0);
  EXPECT_EQ(state_class::get_dbm_instrumentation().minimize_calls, 1u);
}
