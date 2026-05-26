#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/properties.hpp>

#include "analysis/state.h"
#include "petri/petri.h"
#include "test_state_class_test_access.h"

namespace {

using petri::Marking;
using petri::PTPN;
using petri::TimeInterval;
using state_class::SCGraph;
using state_class::SCVertex;
using state_class::StateClass;
using state_class::StateClassReachabilityGraph;
using state_class::StateClassReachabilityGraphTestAccess;

struct PrioritySameCoreNet {
  PTPN net;
  size_t p_high;
  size_t p_low;
  size_t done_high;
  size_t done_low;
  size_t high;
  size_t low;
};

struct ControlAndTaskNet {
  PTPN net;
  size_t p_task_high;
  size_t p_task_low;
  size_t p_control;
  size_t d_task_high;
  size_t d_task_low;
  size_t d_control;
  size_t task_high;
  size_t task_low;
  size_t control;
};

struct ResumeNet {
  PTPN net;
  size_t ready_high;
  size_t ready_low;
  size_t done_high;
  size_t done_low;
  size_t high;
  size_t low;
};

struct SelfLoopNet {
  PTPN net;
  size_t ready;
  size_t loop;
};

PrioritySameCoreNet build_priority_same_core_net(bool low_suspendable = false) {
  PrioritySameCoreNet fixture;
  fixture.p_high = fixture.net.add_place("p_high");
  fixture.p_low = fixture.net.add_place("p_low");
  fixture.done_high = fixture.net.add_place("done_high");
  fixture.done_low = fixture.net.add_place("done_low");
  fixture.high = fixture.net.add_transition("high", TimeInterval(1, 1), 100,
                                            0, false);
  fixture.low = fixture.net.add_transition("low", TimeInterval(3, 7), 10, 0,
                                           low_suspendable);
  fixture.net.set_pre_arc(fixture.p_high, fixture.high);
  fixture.net.set_post_arc(fixture.high, fixture.done_high);
  fixture.net.set_pre_arc(fixture.p_low, fixture.low);
  fixture.net.set_post_arc(fixture.low, fixture.done_low);
  fixture.net.set_initial_marking({1, 1, 0, 0});
  return fixture;
}

ControlAndTaskNet build_control_and_task_net(bool low_suspendable = false) {
  ControlAndTaskNet fixture;
  fixture.p_task_high = fixture.net.add_place("p_task_high");
  fixture.p_task_low = fixture.net.add_place("p_task_low");
  fixture.p_control = fixture.net.add_place("p_control");
  fixture.d_task_high = fixture.net.add_place("d_task_high");
  fixture.d_task_low = fixture.net.add_place("d_task_low");
  fixture.d_control = fixture.net.add_place("d_control");
  fixture.task_high = fixture.net.add_transition("task_high", TimeInterval(1, 1),
                                                 100, 0, false);
  fixture.task_low = fixture.net.add_transition("task_low", TimeInterval(2, 4), 10,
                                                0, low_suspendable);
  fixture.control = fixture.net.add_transition("control", TimeInterval(0, 0), 5,
                                               -1, false);
  fixture.net.set_pre_arc(fixture.p_task_high, fixture.task_high);
  fixture.net.set_post_arc(fixture.task_high, fixture.d_task_high);
  fixture.net.set_pre_arc(fixture.p_task_low, fixture.task_low);
  fixture.net.set_post_arc(fixture.task_low, fixture.d_task_low);
  fixture.net.set_pre_arc(fixture.p_control, fixture.control);
  fixture.net.set_post_arc(fixture.control, fixture.d_control);
  fixture.net.set_initial_marking({1, 1, 1, 0, 0, 0});
  return fixture;
}

ResumeNet build_resume_net() {
  ResumeNet fixture;
  fixture.ready_high = fixture.net.add_place("ready_high");
  fixture.ready_low = fixture.net.add_place("ready_low");
  fixture.done_high = fixture.net.add_place("done_high");
  fixture.done_low = fixture.net.add_place("done_low");
  fixture.high = fixture.net.add_transition("high", TimeInterval(1, 1), 100, 0,
                                            false);
  fixture.low = fixture.net.add_transition("low", TimeInterval(2, 6), 10, 0,
                                           true);
  fixture.net.set_pre_arc(fixture.ready_high, fixture.high);
  fixture.net.set_post_arc(fixture.high, fixture.done_high);
  fixture.net.set_pre_arc(fixture.ready_low, fixture.low);
  fixture.net.set_post_arc(fixture.low, fixture.done_low);
  fixture.net.set_initial_marking({1, 1, 0, 0});
  return fixture;
}

SelfLoopNet build_self_loop_net() {
  SelfLoopNet fixture;
  fixture.ready = fixture.net.add_place("ready");
  fixture.loop = fixture.net.add_transition("loop", TimeInterval(1, 1), 10, 0,
                                            false);
  fixture.net.set_pre_arc(fixture.ready, fixture.loop);
  fixture.net.set_post_arc(fixture.loop, fixture.ready);
  fixture.net.set_initial_marking({1});
  return fixture;
}

bool contains(const std::set<size_t>& values, size_t value) {
  return values.find(value) != values.end();
}

bool has_outgoing_transition(const SCGraph& graph, SCVertex vertex,
                             size_t transition_id) {
  auto [edge_it, edge_end] = boost::out_edges(vertex, graph);
  for (; edge_it != edge_end; ++edge_it) {
    const auto& edge = boost::get(boost::edge_name, graph, *edge_it);
    if (edge.transition_id == static_cast<int>(transition_id)) {
      return true;
    }
  }
  return false;
}

SCVertex target_for_transition(const SCGraph& graph, SCVertex vertex,
                               size_t transition_id) {
  auto [edge_it, edge_end] = boost::out_edges(vertex, graph);
  for (; edge_it != edge_end; ++edge_it) {
    const auto& edge = boost::get(boost::edge_name, graph, *edge_it);
    if (edge.transition_id == static_cast<int>(transition_id)) {
      return boost::target(*edge_it, graph);
    }
  }
  return vertex;
}

}  // namespace

TEST(StateClassReachabilitySemanticsTest,
     NormalizeSchedulingStateSeparatesEffectiveAndSuspendedTransitions) {
  const auto fixture = build_priority_same_core_net(true);
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);

  StateClassReachabilityGraphTestAccess::normalize_scheduling_state(graph,
                                                                    initial);

  EXPECT_TRUE(contains(initial.enabled, fixture.high));
  EXPECT_FALSE(contains(initial.enabled, fixture.low));
  EXPECT_TRUE(contains(initial.suspended, fixture.low));
}

TEST(StateClassReachabilitySemanticsTest,
     NormalizeSchedulingStateKeepsControlTransitionsOutsideTaskCompetition) {
  const auto fixture = build_control_and_task_net(true);
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);

  StateClassReachabilityGraphTestAccess::normalize_scheduling_state(graph,
                                                                    initial);

  EXPECT_TRUE(contains(initial.enabled, fixture.task_high));
  EXPECT_TRUE(contains(initial.enabled, fixture.control));
  EXPECT_FALSE(contains(initial.enabled, fixture.task_low));
  EXPECT_TRUE(contains(initial.suspended, fixture.task_low));
  EXPECT_FALSE(contains(initial.suspended, fixture.control));
}

TEST(StateClassReachabilitySemanticsTest,
     FireWithDbmPreservesSuspendableClockAcrossPreemptionRelease) {
  const auto fixture = build_resume_net();
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);

  StateClassReachabilityGraphTestAccess::normalize_scheduling_state(graph,
                                                                    initial);

  const size_t low_clock = fixture.low + 1;
  initial.Z2.set_constraint(low_clock, 0, 6);
  initial.Z2.set_constraint(0, low_clock, -4);

  auto [ok, successor, firing_time] =
      StateClassReachabilityGraphTestAccess::fire_with_dbm(graph, fixture.high,
                                                           initial);

  ASSERT_TRUE(ok);
  EXPECT_DOUBLE_EQ(firing_time, 1.0);
  EXPECT_TRUE(contains(successor.enabled, fixture.low));
  EXPECT_FALSE(contains(successor.suspended, fixture.low));
  EXPECT_EQ(successor.Z2.get_constraint(low_clock, 0), 6);
  EXPECT_EQ(successor.Z2.get_constraint(0, low_clock), -4);
}

TEST(StateClassReachabilitySemanticsTest,
     BuildKeepsStableSelfLoopMarkingAndRecordsDedupHit) {
  const auto fixture = build_self_loop_net();
  StateClassReachabilityGraph graph(fixture.net);

  ASSERT_EQ(graph.build(8), 2U);
  EXPECT_EQ(graph.get_statistics().total_states, 2U);
  EXPECT_EQ(graph.get_statistics().dedup_hits_count, 1U);

  const auto& sc_graph = graph.get_graph();
  const SCVertex initial = graph.get_initial_vertex();
  ASSERT_TRUE(has_outgoing_transition(sc_graph, initial, fixture.loop));

  const SCVertex target = target_for_transition(sc_graph, initial, fixture.loop);
  EXPECT_NE(target, initial);
  const StateClass& state = boost::get(boost::vertex_name, sc_graph, target);
  EXPECT_EQ(state.marking, (Marking{1}));
}
