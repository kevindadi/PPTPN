#include <gtest/gtest.h>

#include <boost/graph/adjacency_list.hpp>

#include "analysis/clock_state.h"
#include "analysis/dbm.h"
#include "analysis/ptpn_analysis.h"
#include "petri/petri.h"

namespace state_class {
struct StateClassReachabilityGraphTestAccess {
  static void apply_preemption(StateClassReachabilityGraph& graph,
                               const std::vector<size_t>& chosen,
                               ReachabilityState& state) {
    graph.apply_preemption(chosen, state);
  }

  static void recompute_suspension(const StateClassReachabilityGraph& graph,
                                   ReachabilityState& state) {
    graph.recompute_suspension(state);
  }
};
}  // namespace state_class

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

petri::PTPN make_suspendable_same_core_net() {
  petri::PTPN ptpn;

  const size_t input = ptpn.add_place("p0", 2);
  ptpn.set_initial_marking(input, 1);

  const size_t low_priority =
      ptpn.add_transition("low_priority", petri::TimeInterval(0, 5), 1, 0, true);
  const size_t high_priority =
      ptpn.add_transition("high_priority", petri::TimeInterval(0, 5), 2, 0, true);

  ptpn.set_pre_arc(input, low_priority, 1);
  ptpn.set_post_arc(low_priority, input, 1);

  ptpn.set_pre_arc(input, high_priority, 1);
  ptpn.set_post_arc(high_priority, input, 1);

  return ptpn;
}

petri::PTPN make_recompute_suspension_fallback_net() {
  petri::PTPN ptpn;

  const size_t source = ptpn.add_place("source", 1);
  const size_t ready = ptpn.add_place("ready", 1);
  const size_t source_done = ptpn.add_place("source_done", 1);
  const size_t ready_low_done = ptpn.add_place("ready_low_done", 1);
  const size_t ready_high_done = ptpn.add_place("ready_high_done", 1);
  ptpn.set_initial_marking(source, 1);

  const size_t original =
      ptpn.add_transition("original", petri::TimeInterval(0, 0), 1, 0, false);
  const size_t replacement_low =
      ptpn.add_transition("replacement_low", petri::TimeInterval(0, 0), 1, 0, true);
  const size_t replacement_high =
      ptpn.add_transition("replacement_high", petri::TimeInterval(0, 0), 5, 0, true);

  ptpn.set_pre_arc(source, original, 1);
  ptpn.set_post_arc(original, source_done, 1);

  ptpn.set_pre_arc(ready, replacement_low, 1);
  ptpn.set_post_arc(replacement_low, ready_low_done, 1);

  ptpn.set_pre_arc(ready, replacement_high, 1);
  ptpn.set_post_arc(replacement_high, ready_high_done, 1);

  return ptpn;
}

petri::PTPN make_post_fire_survivor_net() {
  petri::PTPN ptpn;

  const size_t shared = ptpn.add_place("shared", 1);
  const size_t fired_done = ptpn.add_place("fired_done", 1);
  const size_t survivor_done = ptpn.add_place("survivor_done", 1);
  ptpn.set_initial_marking(shared, 1);

  const size_t fired =
      ptpn.add_transition("fired", petri::TimeInterval(0, 0), 1, -1, false);
  const size_t survivor =
      ptpn.add_transition("survivor", petri::TimeInterval(0, 5), 1, -1, false);

  ptpn.set_pre_arc(shared, fired, 1);
  ptpn.set_post_arc(fired, shared, 1);
  ptpn.set_post_arc(fired, fired_done, 1);

  ptpn.set_pre_arc(shared, survivor, 1);
  ptpn.set_post_arc(survivor, survivor_done, 1);

  return ptpn;
}

petri::PTPN make_newly_enabled_siblings_net() {
  petri::PTPN ptpn;

  const size_t input = ptpn.add_place("input", 1);
  const size_t shared = ptpn.add_place("shared", 1);
  const size_t left_done = ptpn.add_place("left_done", 1);
  const size_t right_done = ptpn.add_place("right_done", 1);
  ptpn.set_initial_marking(input, 1);

  const size_t trigger =
      ptpn.add_transition("trigger", petri::TimeInterval(0, 0), 1, -1, false);
  const size_t left =
      ptpn.add_transition("left", petri::TimeInterval(0, 4), 1, -1, false);
  const size_t right =
      ptpn.add_transition("right", petri::TimeInterval(0, 6), 1, -1, false);

  ptpn.set_pre_arc(input, trigger, 1);
  ptpn.set_post_arc(trigger, shared, 1);

  ptpn.set_pre_arc(shared, left, 1);
  ptpn.set_post_arc(left, left_done, 1);

  ptpn.set_pre_arc(shared, right, 1);
  ptpn.set_post_arc(right, right_done, 1);

  return ptpn;
}

petri::PTPN make_two_survivor_future_closed_net() {
  petri::PTPN ptpn;

  const size_t fired_token = ptpn.add_place("fired_token", 1);
  const size_t left_token = ptpn.add_place("left_token", 1);
  const size_t right_token = ptpn.add_place("right_token", 1);
  const size_t fired_done = ptpn.add_place("fired_done", 1);
  ptpn.set_initial_marking(fired_token, 1);
  ptpn.set_initial_marking(left_token, 1);
  ptpn.set_initial_marking(right_token, 1);

  const size_t fired =
      ptpn.add_transition("fired", petri::TimeInterval(0, 0), 1, -1, false);
  const size_t left =
      ptpn.add_transition("left", petri::TimeInterval(0, 5), 1, -1, false);
  const size_t right =
      ptpn.add_transition("right", petri::TimeInterval(0, 7), 1, -1, false);

  ptpn.set_pre_arc(fired_token, fired, 1);
  ptpn.set_post_arc(fired, fired_token, 1);
  ptpn.set_post_arc(fired, fired_done, 1);

  ptpn.set_pre_arc(left_token, left, 1);
  ptpn.set_post_arc(left, left_token, 1);

  ptpn.set_pre_arc(right_token, right, 1);
  ptpn.set_post_arc(right, right_token, 1);

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
  const size_t survivor_input = ptpn.add_place("p3", 1);
  const size_t survivor_done = ptpn.add_place("p4", 1);
  ptpn.set_initial_marking(input, 1);
  ptpn.set_initial_marking(survivor_input, 1);

  const size_t low = ptpn.add_transition(
      "low", petri::TimeInterval(0, 0, false, false), 1, 0, false);
  const size_t high = ptpn.add_transition(
      "high", petri::TimeInterval(0, 3, true, false), 99, 0, false);
  const size_t survivor =
      ptpn.add_transition("survivor", petri::TimeInterval(0, 5), 1, -1, false);

  ptpn.set_pre_arc(input, low, 1);
  ptpn.set_post_arc(low, low_done, 1);
  ptpn.set_pre_arc(input, high, 1);
  ptpn.set_post_arc(high, high_done, 1);
  ptpn.set_pre_arc(survivor_input, survivor, 1);
  ptpn.set_post_arc(survivor, survivor_done, 1);

  state_class::PTPNAnalyzer analyzer(ptpn);
  ASSERT_GE(analyzer.build(8), 2u);

  const auto& graph = analyzer.get_graph();
  const auto initial = analyzer.get_initial_vertex();
  std::set<size_t> fired;
  for (auto [edge_it, edge_end] = boost::out_edges(initial, graph); edge_it != edge_end;
       ++edge_it) {
    const auto& edge = boost::get(boost::edge_name, graph, *edge_it);
    fired.insert(static_cast<size_t>(edge.transition_id));
  }

  EXPECT_TRUE(fired.count(low));
  EXPECT_TRUE(fired.count(survivor));
  EXPECT_FALSE(fired.count(high));

  auto state = analyzer.create_initial_state();
  const size_t predecessor_survivor_clock =
      static_cast<size_t>(state.clock_index_for_transition(survivor));
  ASSERT_GT(predecessor_survivor_clock, 0u);

  state.timing.zone.set_constraint(0, predecessor_survivor_clock, -2);
  state.timing.zone.minimize();
  state.sync_clocks_from_zone();
  ASSERT_EQ(state.timing.clocks[survivor].lower_bound, 2);

  const auto [ok, successor, fire_time] = analyzer.fire_with_time(low, state);

  ASSERT_TRUE(ok);
  EXPECT_DOUBLE_EQ(0.0, fire_time);
  EXPECT_TRUE(successor.scheduling.enabled.count(survivor));
  ASSERT_TRUE(successor.has_zone_clock_for_transition(survivor));

  const size_t survivor_clock =
      static_cast<size_t>(successor.clock_index_for_transition(survivor));
  EXPECT_EQ(successor.timing.zone.get_constraint(0, survivor_clock), 0);
  EXPECT_EQ(successor.timing.zone.get_constraint(survivor_clock, 0), 5);
  EXPECT_EQ(successor.timing.clocks[survivor].lower_bound, 0);
  EXPECT_EQ(successor.timing.clocks[survivor].upper_bound, 5);
}

TEST(PtpnAnalysisSemanticsTest, ApplyPreemptionPreservesTimeFirstChosenTransitions) {
  const petri::PTPN ptpn = make_time_first_net();
  state_class::StateClassReachabilityGraph graph(ptpn);
  auto state = graph.create_initial_state();

  ASSERT_EQ(state.scheduling.enabled, std::set<size_t>({0u, 1u}));

  state_class::StateClassReachabilityGraphTestAccess::apply_preemption(graph, {0u}, state);

  EXPECT_EQ(state.scheduling.enabled, std::set<size_t>({0u, 1u}));
  EXPECT_EQ(state.scheduling.active, std::set<size_t>({0u}));
  EXPECT_TRUE(state.scheduling.suspended.empty());
  EXPECT_EQ(state.timing.clocks[0].state, state_class::ClockState::ACTIVE);
  EXPECT_EQ(state.timing.clocks[1].state, state_class::ClockState::UNACTIVE);
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

TEST(PtpnAnalysisSemanticsTest,
     RecomputeSuspensionFallsBackToReplacementWhenPreviousActiveDisappears) {
  const petri::PTPN ptpn = make_recompute_suspension_fallback_net();
  state_class::StateClassReachabilityGraph graph(ptpn);
  auto state = graph.create_initial_state();

  EXPECT_EQ(state.scheduling.enabled, std::set<size_t>({0u}));
  EXPECT_EQ(state.scheduling.active, std::set<size_t>({0u}));
  EXPECT_TRUE(state.scheduling.suspended.empty());

  state.marking = {0, 1, 0, 0, 0};

  state_class::StateClassReachabilityGraphTestAccess::recompute_suspension(graph,
                                                                            state);

  EXPECT_EQ(state.scheduling.enabled, (std::set<size_t>{1u, 2u}));
  EXPECT_EQ(state.scheduling.active, std::set<size_t>({2u}));
  EXPECT_EQ(state.scheduling.suspended, std::set<size_t>({1u}));
  EXPECT_EQ(state.timing.clocks[0].state, state_class::ClockState::UNACTIVE);
  EXPECT_EQ(state.timing.clocks[1].state, state_class::ClockState::SUSPENDED);
  EXPECT_EQ(state.timing.clocks[2].state, state_class::ClockState::ACTIVE);
}

TEST(PtpnAnalysisSemanticsTest, RecomputeSuspensionKeepsSuspendedClockFrozen) {
  const petri::PTPN ptpn = make_suspendable_same_core_net();
  state_class::StateClassReachabilityGraph graph(ptpn);
  auto state = graph.create_initial_state();

  graph.suspend_transition(0u, state);
  const size_t suspended_clock =
      static_cast<size_t>(state.clock_index_for_transition(0u));

  ASSERT_TRUE(state.scheduling.enabled.count(0u));
  ASSERT_TRUE(state.scheduling.active.count(1u));
  ASSERT_TRUE(state.scheduling.suspended.count(0u));
  ASSERT_EQ(state.timing.clocks[0].state, state_class::ClockState::SUSPENDED);
  ASSERT_TRUE(state.timing.zone.is_frozen(suspended_clock));

  state_class::StateClassReachabilityGraphTestAccess::recompute_suspension(graph, state);

  EXPECT_EQ(state.scheduling.enabled, std::set<size_t>({0u, 1u}));
  EXPECT_EQ(state.scheduling.active, std::set<size_t>({1u}));
  EXPECT_EQ(state.scheduling.suspended, std::set<size_t>({0u}));
  EXPECT_EQ(state.timing.clocks[0].state, state_class::ClockState::SUSPENDED);
  EXPECT_EQ(state.timing.clocks[1].state, state_class::ClockState::ACTIVE);
  EXPECT_TRUE(state.timing.zone.is_frozen(suspended_clock));
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
  state_class::DBM dbm(3);
  dbm.set_constraint(0, 1, -2);
  dbm.set_constraint(1, 0, 5);
  dbm.set_constraint(0, 2, -4);
  dbm.set_constraint(2, 0, 7);

  state_class::reset_dbm_instrumentation();

  dbm.synchronize_clocks({1, 2});

  EXPECT_EQ(dbm.get_constraint(1, 2), 0);
  EXPECT_EQ(dbm.get_constraint(2, 1), 0);
  EXPECT_EQ(state_class::get_dbm_instrumentation().minimize_calls, 1u);
}


TEST(PtpnAnalysisSemanticsTest, SuccessorZoneRemainsFutureClosedAfterFire) {
  const petri::PTPN ptpn = make_post_fire_survivor_net();
  state_class::PTPNAnalyzer analyzer(ptpn);

  auto state = analyzer.create_initial_state();
  const size_t predecessor_survivor_clock =
      static_cast<size_t>(state.clock_index_for_transition(1));
  ASSERT_GT(predecessor_survivor_clock, 0u);

  state.timing.zone.set_constraint(0, predecessor_survivor_clock, -2);
  state.timing.zone.minimize();
  state.sync_clocks_from_zone();
  ASSERT_EQ(state.timing.clocks[1].lower_bound, 2);

  const auto [ok, successor, fire_time] = analyzer.fire_with_time(0, state);

  ASSERT_TRUE(ok);
  EXPECT_DOUBLE_EQ(0.0, fire_time);
  EXPECT_TRUE(successor.scheduling.enabled.count(1));
  ASSERT_TRUE(successor.has_zone_clock_for_transition(1));

  const size_t survivor_clock =
      static_cast<size_t>(successor.clock_index_for_transition(1));
  EXPECT_EQ(successor.timing.zone.get_constraint(0, survivor_clock), 0);
  EXPECT_EQ(successor.timing.zone.get_constraint(survivor_clock, 0), 5);
  EXPECT_EQ(successor.timing.clocks[1].lower_bound, 0);
  EXPECT_EQ(successor.timing.clocks[1].upper_bound, 5);
}

TEST(PtpnAnalysisSemanticsTest, NewlyEnabledTransitionsShareZeroOrigin) {
  const petri::PTPN ptpn = make_newly_enabled_siblings_net();
  state_class::PTPNAnalyzer analyzer(ptpn);

  const auto state = analyzer.create_initial_state();
  const auto [ok, successor, fire_time] = analyzer.fire_with_time(0, state);

  ASSERT_TRUE(ok);
  EXPECT_DOUBLE_EQ(0.0, fire_time);
  EXPECT_TRUE(successor.scheduling.enabled.count(1));
  EXPECT_TRUE(successor.scheduling.enabled.count(2));
  ASSERT_TRUE(successor.has_zone_clock_for_transition(1));
  ASSERT_TRUE(successor.has_zone_clock_for_transition(2));

  const size_t left_clock =
      static_cast<size_t>(successor.clock_index_for_transition(1));
  const size_t right_clock =
      static_cast<size_t>(successor.clock_index_for_transition(2));

  EXPECT_EQ(successor.timing.zone.get_constraint(0, left_clock), 0);
  EXPECT_EQ(successor.timing.zone.get_constraint(left_clock, 0), 4);
  EXPECT_EQ(successor.timing.zone.get_constraint(0, right_clock), 0);
  EXPECT_EQ(successor.timing.zone.get_constraint(right_clock, 0), 6);
  EXPECT_EQ(successor.timing.clocks[1].lower_bound, 0);
  EXPECT_EQ(successor.timing.clocks[1].upper_bound, 4);
  EXPECT_EQ(successor.timing.clocks[2].lower_bound, 0);
  EXPECT_EQ(successor.timing.clocks[2].upper_bound, 6);
}

TEST(PtpnAnalysisSemanticsTest,
     FutureClosedSuccessorDropsPredecessorCouplingBetweenSurvivors) {
  const petri::PTPN ptpn = make_two_survivor_future_closed_net();
  state_class::PTPNAnalyzer analyzer(ptpn);

  auto state = analyzer.create_initial_state();
  ASSERT_TRUE(state.has_zone_clock_for_transition(1));
  ASSERT_TRUE(state.has_zone_clock_for_transition(2));

  const size_t left_clock = static_cast<size_t>(state.clock_index_for_transition(1));
  const size_t right_clock = static_cast<size_t>(state.clock_index_for_transition(2));

  state.timing.zone.set_constraint(0, left_clock, -2);
  state.timing.zone.set_constraint(0, right_clock, -4);
  state.timing.zone.set_constraint(left_clock, right_clock, 1);
  state.timing.zone.set_constraint(right_clock, left_clock, 0);
  state.timing.zone.minimize();
  state.sync_clocks_from_zone();

  ASSERT_NE(state.timing.zone.get_constraint(left_clock, right_clock),
            state_class::INF_TIME);
  ASSERT_NE(state.timing.zone.get_constraint(right_clock, left_clock),
            state_class::INF_TIME);
  ASSERT_GT(state.timing.clocks[1].lower_bound, 0);
  ASSERT_GT(state.timing.clocks[2].lower_bound, 0);

  const int predecessor_left_upper = state.timing.zone.get_constraint(left_clock, 0);
  const int predecessor_right_upper = state.timing.zone.get_constraint(right_clock, 0);
  ASSERT_EQ(predecessor_left_upper, 5);
  ASSERT_EQ(predecessor_right_upper, 5);
  ASSERT_EQ(state.timing.zone.get_constraint(left_clock, right_clock), 1);
  ASSERT_EQ(state.timing.zone.get_constraint(right_clock, left_clock), 0);

  const auto [ok, successor, fire_time] = analyzer.fire_with_time(0, state);

  ASSERT_TRUE(ok);
  EXPECT_DOUBLE_EQ(0.0, fire_time);
  EXPECT_TRUE(successor.scheduling.enabled.count(1));
  EXPECT_TRUE(successor.scheduling.enabled.count(2));
  ASSERT_TRUE(successor.has_zone_clock_for_transition(1));
  ASSERT_TRUE(successor.has_zone_clock_for_transition(2));

  const size_t successor_left_clock =
      static_cast<size_t>(successor.clock_index_for_transition(1));
  const size_t successor_right_clock =
      static_cast<size_t>(successor.clock_index_for_transition(2));

  EXPECT_EQ(successor.timing.zone.get_constraint(0, successor_left_clock), 0);
  EXPECT_EQ(successor.timing.zone.get_constraint(successor_left_clock, 0),
            predecessor_left_upper);
  EXPECT_EQ(successor.timing.zone.get_constraint(0, successor_right_clock), 0);
  EXPECT_EQ(successor.timing.zone.get_constraint(successor_right_clock, 0),
            predecessor_right_upper);
  EXPECT_EQ(successor.timing.zone.get_constraint(successor_left_clock,
                                                 successor_right_clock),
            predecessor_left_upper);
  EXPECT_EQ(successor.timing.zone.get_constraint(successor_right_clock,
                                                 successor_left_clock),
            predecessor_right_upper);
  EXPECT_NE(successor.timing.zone.get_constraint(successor_left_clock,
                                                 successor_right_clock),
            1);
  EXPECT_NE(successor.timing.zone.get_constraint(successor_right_clock,
                                                 successor_left_clock),
            0);
  EXPECT_EQ(successor.timing.clocks[1].lower_bound, 0);
  EXPECT_EQ(successor.timing.clocks[1].upper_bound, predecessor_left_upper);
  EXPECT_EQ(successor.timing.clocks[2].lower_bound, 0);
  EXPECT_EQ(successor.timing.clocks[2].upper_bound, predecessor_right_upper);
}
