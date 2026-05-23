#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <tuple>
#include <vector>

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/properties.hpp>

#include "analysis/state.h"
#include "petri/petri.h"
#include "test_state_class_test_access.h"

namespace state_class {

}  // namespace state_class

namespace {

using petri::Marking;
using petri::PTPN;
using petri::TimeInterval;
using state_class::SCGraph;
using state_class::SCVertex;
using state_class::StateClass;
using state_class::StateClassReachabilityGraph;
using state_class::StateClassReachabilityGraphTestAccess;
using state_class::TransitionEdge;

struct OneTransitionNet {
  PTPN net;
  size_t ready;
  size_t done;
  size_t run;
};

// Diagram:
//   ready(1) --run[2,5], prio=10, core=0--> done(0)
OneTransitionNet build_one_transition_net() {
  OneTransitionNet fixture;
  fixture.ready = fixture.net.add_place("ready");
  fixture.done = fixture.net.add_place("done");
  fixture.run = fixture.net.add_transition("run", TimeInterval(2, 5), 10, 0,
                                           false);
  fixture.net.set_pre_arc(fixture.ready, fixture.run);
  fixture.net.set_post_arc(fixture.run, fixture.done);
  fixture.net.set_initial_marking({1, 0});
  return fixture;
}

struct PrioritySameCoreNet {
  PTPN net;
  size_t p_high;
  size_t p_low;
  size_t done_high;
  size_t done_low;
  size_t high;
  size_t low;
};

// Diagram (low_suspendable=false):
//   p_high(1) --high[1,1], prio=100, core=0, non-susp--> done_high(0)
//   p_low(1)  --low[3,7],   prio=10,  core=0, non-susp--> done_low(0)
//   M0 = {p_high=1, p_low=1, done_high=0, done_low=0}
// Diagram (low_suspendable=true):
//   same structure, low is suspendable
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

struct MultiCoreNet {
  PTPN net;
  size_t p0_high;
  size_t p0_low;
  size_t p1;
  size_t d0_high;
  size_t d0_low;
  size_t d1;
  size_t core0_high;
  size_t core0_low;
  size_t core1_only;
};

struct TiedPriorityCoreNet {
  PTPN net;
  size_t p0_a;
  size_t p0_b;
  size_t done_a;
  size_t done_b;
  size_t t0_a;
  size_t t0_b;
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

// Diagram:
//   p0_high(1) --core0_high[1,1], prio=100, core=0--> d0_high(0)
//   p0_low(1)  --core0_low[1,1],   prio=10,  core=0--> d0_low(0)
//   p1(1)      --core1_only[1,1], prio=50,  core=1--> d1(0)
//   M0 = {p0_high=1, p0_low=1, p1=1, d0_high=0, d0_low=0, d1=0}
MultiCoreNet build_multi_core_net() {
  MultiCoreNet fixture;
  fixture.p0_high = fixture.net.add_place("p0_high");
  fixture.p0_low = fixture.net.add_place("p0_low");
  fixture.p1 = fixture.net.add_place("p1");
  fixture.d0_high = fixture.net.add_place("d0_high");
  fixture.d0_low = fixture.net.add_place("d0_low");
  fixture.d1 = fixture.net.add_place("d1");
  fixture.core0_high = fixture.net.add_transition(
      "core0_high", TimeInterval(1, 1), 100, 0, false);
  fixture.core0_low = fixture.net.add_transition(
      "core0_low", TimeInterval(1, 1), 10, 0, false);
  fixture.core1_only = fixture.net.add_transition(
      "core1_only", TimeInterval(1, 1), 50, 1, false);
  fixture.net.set_pre_arc(fixture.p0_high, fixture.core0_high);
  fixture.net.set_post_arc(fixture.core0_high, fixture.d0_high);
  fixture.net.set_pre_arc(fixture.p0_low, fixture.core0_low);
  fixture.net.set_post_arc(fixture.core0_low, fixture.d0_low);
  fixture.net.set_pre_arc(fixture.p1, fixture.core1_only);
  fixture.net.set_post_arc(fixture.core1_only, fixture.d1);
  fixture.net.set_initial_marking({1, 1, 1, 0, 0, 0});
  return fixture;
}

TiedPriorityCoreNet build_tied_priority_core_net() {
  TiedPriorityCoreNet fixture;
  fixture.p0_a = fixture.net.add_place("p0_a");
  fixture.p0_b = fixture.net.add_place("p0_b");
  fixture.done_a = fixture.net.add_place("done_a");
  fixture.done_b = fixture.net.add_place("done_b");
  fixture.t0_a = fixture.net.add_transition("t0_a", TimeInterval(1, 1), 100, 0,
                                            false);
  fixture.t0_b = fixture.net.add_transition("t0_b", TimeInterval(1, 1), 100, 0,
                                            false);
  fixture.net.set_pre_arc(fixture.p0_a, fixture.t0_a);
  fixture.net.set_post_arc(fixture.t0_a, fixture.done_a);
  fixture.net.set_pre_arc(fixture.p0_b, fixture.t0_b);
  fixture.net.set_post_arc(fixture.t0_b, fixture.done_b);
  fixture.net.set_initial_marking({1, 1, 0, 0});
  return fixture;
}

struct ReenableNet {
  PTPN net;
  size_t ready;
  size_t gate;
  size_t done;
  size_t trigger;
  size_t loop;
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

ReenableNet build_reenable_net() {
  ReenableNet fixture;
  fixture.ready = fixture.net.add_place("ready");
  fixture.gate = fixture.net.add_place("gate");
  fixture.done = fixture.net.add_place("done");
  fixture.trigger = fixture.net.add_transition("trigger", TimeInterval(1, 1), 20,
                                               1, false);
  fixture.loop = fixture.net.add_transition("loop", TimeInterval(2, 5), 10, 0,
                                            false);
  fixture.net.set_pre_arc(fixture.ready, fixture.loop);
  fixture.net.set_post_arc(fixture.loop, fixture.ready);
  fixture.net.set_post_arc(fixture.loop, fixture.done);
  fixture.net.set_pre_arc(fixture.gate, fixture.trigger);
  fixture.net.set_post_arc(fixture.trigger, fixture.done);
  fixture.net.set_initial_marking({1, 1, 0});
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

bool contains(const std::vector<size_t>& values, size_t value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

bool contains(const std::set<size_t>& values, size_t value) {
  return values.find(value) != values.end();
}

std::vector<TransitionEdge> outgoing_edges_for(const SCGraph& graph,
                                               SCVertex vertex) {
  std::vector<TransitionEdge> edges;
  auto [edge_it, edge_end] = boost::out_edges(vertex, graph);
  for (; edge_it != edge_end; ++edge_it) {
    edges.push_back(boost::get(boost::edge_name, graph, *edge_it));
  }
  return edges;
}

bool has_outgoing_transition(const SCGraph& graph, SCVertex vertex,
                             size_t transition_id) {
  const auto edges = outgoing_edges_for(graph, vertex);
  return std::any_of(edges.begin(), edges.end(), [transition_id](const auto& e) {
    return e.transition_id == static_cast<int>(transition_id);
  });
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

bool has_marking(const SCGraph& graph, const Marking& marking) {
  auto [vertex_it, vertex_end] = boost::vertices(graph);
  for (; vertex_it != vertex_end; ++vertex_it) {
    const StateClass& state = boost::get(boost::vertex_name, graph, *vertex_it);
    if (state.marking == marking) {
      return true;
    }
  }
  return false;
}

std::vector<int> outgoing_transition_ids(const SCGraph& graph, SCVertex vertex) {
  std::vector<int> ids;
  auto [edge_it, edge_end] = boost::out_edges(vertex, graph);
  for (; edge_it != edge_end; ++edge_it) {
    const auto& edge = boost::get(boost::edge_name, graph, *edge_it);
    ids.push_back(edge.transition_id);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

}  // namespace

// Expect: constructed places, transition metadata, arcs, and M0 match the diagram.
TEST(PTPNReachabilityTest, ManualNetConstructionCreatesExpectedMatrices) {
  const auto fixture = build_one_transition_net();

  EXPECT_EQ(fixture.net.num_places(), 2U);
  EXPECT_EQ(fixture.net.num_transitions(), 1U);
  EXPECT_EQ(fixture.net.get_transition(fixture.run).name, "run");
  EXPECT_EQ(fixture.net.get_transition(fixture.run).time_interval.earliest, 2);
  EXPECT_EQ(fixture.net.get_transition(fixture.run).time_interval.latest, 5);
  EXPECT_EQ(fixture.net.get_transition(fixture.run).priority, 10);
  EXPECT_EQ(fixture.net.get_transition(fixture.run).core, 0);
  EXPECT_FALSE(fixture.net.get_transition(fixture.run).suspendable);
  EXPECT_EQ(fixture.net.get_pre_matrix()[fixture.ready][fixture.run], 1);
  EXPECT_EQ(fixture.net.get_post_matrix()[fixture.run][fixture.done], 1);
  EXPECT_EQ(fixture.net.get_marking(), (Marking{1, 0}));
}

// Expect: run is enabled exactly when ready contains enough tokens.
TEST(PTPNReachabilityTest, PetriIsEnabledReflectsTokenAvailability) {
  const auto fixture = build_one_transition_net();

  EXPECT_TRUE(PTPN::is_enabled({1, 0}, fixture.net, fixture.run));
  EXPECT_FALSE(PTPN::is_enabled({0, 0}, fixture.net, fixture.run));
  EXPECT_THROW(PTPN::is_enabled({1}, fixture.net, fixture.run),
               std::invalid_argument);
  EXPECT_THROW(PTPN::is_enabled({1, 0}, fixture.net, 10), std::out_of_range);
}

// Expect: firing consumes input tokens, produces output tokens, and respects capacity.
TEST(PTPNReachabilityTest, PetriFireConsumesAndProducesTokens) {
  const auto fixture = build_one_transition_net();

  EXPECT_EQ(PTPN::fire({1, 0}, fixture.net, fixture.run), (Marking{0, 1}));
  EXPECT_THROW(PTPN::fire({0, 0}, fixture.net, fixture.run),
               std::runtime_error);

  PTPN capacity_net;
  const size_t input = capacity_net.add_place("input");
  const size_t output = capacity_net.add_place("output", 1);
  const size_t produce = capacity_net.add_transition("produce");
  capacity_net.set_pre_arc(input, produce);
  capacity_net.set_post_arc(produce, output);
  capacity_net.set_initial_marking({1, 1});
  EXPECT_EQ(PTPN::fire({1, 1}, capacity_net, produce), (Marking{0, 1}));
}

// Expect: initial StateClass mirrors M0 and initializes run's Z1 clock to [2,5].
TEST(PTPNReachabilityTest,
     InitialStateClassContainsInitialMarkingAndEnabledTransitions) {
  const auto fixture = build_one_transition_net();
  StateClassReachabilityGraph graph(fixture.net);

  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);

  const size_t clock_idx = fixture.run + 1;
  EXPECT_EQ(initial.marking, fixture.net.get_marking());
  EXPECT_DOUBLE_EQ(initial.cumulative_time, 0.0);
  EXPECT_TRUE(contains(initial.enabled, fixture.run));
  EXPECT_TRUE(initial.suspended.empty());
  EXPECT_EQ(initial.Z1.size(), fixture.net.num_transitions() + 1);
  EXPECT_EQ(initial.Z2.size(), fixture.net.num_transitions() + 1);
  EXPECT_EQ(initial.Z1.get_constraint(clock_idx, 0), 5);
  EXPECT_EQ(initial.Z1.get_constraint(0, clock_idx), -2);
}

// Expect: collected enabled transitions match raw Petri token availability.
TEST(PTPNReachabilityTest, CollectEnabledTransitionsMatchesPetriEnabled) {
  PTPN net;
  const size_t p0 = net.add_place("p0");
  const size_t p1 = net.add_place("p1");
  const size_t d0 = net.add_place("d0");
  const size_t d1 = net.add_place("d1");
  const size_t t0 = net.add_transition("t0", TimeInterval(1, 1), 10, 0);
  const size_t t1 = net.add_transition("t1", TimeInterval(1, 1), 20, 1);
  const size_t t2 = net.add_transition("t2", TimeInterval(1, 1), 30, 2);
  net.set_pre_arc(p0, t0);
  net.set_post_arc(t0, d0);
  net.set_pre_arc(p1, t1);
  net.set_post_arc(t1, d1);
  net.set_pre_arc(p0, t2);
  net.set_post_arc(t2, d1);
  net.set_initial_marking({1, 0, 0, 0});

  StateClassReachabilityGraph graph(net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);
  auto enabled =
      StateClassReachabilityGraphTestAccess::collect_enabled_transitions(graph,
                                                                        initial);

  EXPECT_TRUE(PTPN::is_enabled(initial.marking, net, t0));
  EXPECT_FALSE(PTPN::is_enabled(initial.marking, net, t1));
  EXPECT_TRUE(PTPN::is_enabled(initial.marking, net, t2));
  EXPECT_TRUE(contains(enabled, t0));
  EXPECT_FALSE(contains(enabled, t1));
  EXPECT_TRUE(contains(enabled, t2));
}

// Expect: core 0 selects core0_high; core 1 selects core1_only.
TEST(PTPNReachabilityTest, SelectPerCoreChoosesHighestPriorityOnEachCore) {
  const auto fixture = build_multi_core_net();
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);
  const std::set<size_t> enabled(initial.enabled.begin(), initial.enabled.end());

  auto chosen =
      StateClassReachabilityGraphTestAccess::select_per_core(graph, enabled);

  ASSERT_EQ(chosen.size(), 2U);
  EXPECT_EQ(chosen[0], fixture.core0_high);
  EXPECT_EQ(chosen[1], fixture.core1_only);
  EXPECT_FALSE(contains(chosen, fixture.core0_low));
}

// Expect: ties on the same task core keep the whole top-priority group.
TEST(PTPNReachabilityTest, SelectPerCoreKeepsAllHighestPriorityTiesPerCore) {
  const auto fixture = build_tied_priority_core_net();
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);
  const std::set<size_t> enabled(initial.enabled.begin(), initial.enabled.end());

  auto chosen =
      StateClassReachabilityGraphTestAccess::select_per_core(graph, enabled);

  ASSERT_EQ(chosen.size(), 2U);
  EXPECT_TRUE(contains(chosen, fixture.t0_a));
  EXPECT_TRUE(contains(chosen, fixture.t0_b));
}

// Expect: core=-1 control transitions are preserved alongside per-core task winners.
TEST(PTPNReachabilityTest, SelectPerCorePreservesControlTransitionsOnCoreMinusOne) {
  const auto fixture = build_control_and_task_net();
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);
  const std::set<size_t> enabled(initial.enabled.begin(), initial.enabled.end());

  auto chosen =
      StateClassReachabilityGraphTestAccess::select_per_core(graph, enabled);

  ASSERT_EQ(chosen.size(), 2U);
  EXPECT_TRUE(contains(chosen, fixture.task_high));
  EXPECT_TRUE(contains(chosen, fixture.control));
  EXPECT_FALSE(contains(chosen, fixture.task_low));
}

// Expect: from the initial graph state, high fires and low does not.
TEST(PTPNReachabilityTest,
     BuildOnlyFiresSelectedTransitionFromInitialStateOnSameCore) {
  const auto fixture = build_priority_same_core_net();
  StateClassReachabilityGraph graph(fixture.net);

  ASSERT_GT(graph.build(8), 0U);
  const auto& sc_graph = graph.get_graph();
  const SCVertex initial = graph.get_initial_vertex();

  EXPECT_TRUE(has_outgoing_transition(sc_graph, initial, fixture.high));
  EXPECT_FALSE(has_outgoing_transition(sc_graph, initial, fixture.low));
}

// Expect: fire_with_dbm(run) reaches done and removes run from enabled.
TEST(PTPNReachabilityTest, FireWithDbmUpdatesMarkingAndEnabledSet) {
  const auto fixture = build_one_transition_net();
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);

  auto [ok, successor, firing_time] =
      StateClassReachabilityGraphTestAccess::fire_with_dbm(graph, fixture.run,
                                                          initial);

  EXPECT_TRUE(ok);
  EXPECT_EQ(successor.marking, (Marking{0, 1}));
  EXPECT_FALSE(contains(successor.enabled, fixture.run));
  EXPECT_DOUBLE_EQ(firing_time, 2.0);
}

// Expect: a transition that is still enabled after another firing keeps its accumulated waiting time.
TEST(PTPNReachabilityTest,
     FireWithDbmPreservesPersistentEnabledClockProgress) {
  const auto fixture = build_reenable_net();
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);

  const size_t loop_clock = fixture.loop + 1;
  initial.Z1.set_constraint(loop_clock, 0, 4);
  initial.Z1.set_constraint(0, loop_clock, -3);

  auto [ok, successor, firing_time] =
      StateClassReachabilityGraphTestAccess::fire_with_dbm(graph,
                                                          fixture.trigger,
                                                          initial);

  ASSERT_TRUE(ok);
  EXPECT_DOUBLE_EQ(firing_time, 1.0);
  EXPECT_TRUE(contains(successor.enabled, fixture.loop));
  EXPECT_EQ(successor.Z1.get_constraint(loop_clock, 0), 4);
  EXPECT_EQ(successor.Z1.get_constraint(0, loop_clock), -3);
}

// Expect: a fired transition that is immediately re-enabled gets a fresh zero-age clock.
TEST(PTPNReachabilityTest,
     FireWithDbmResetsImmediatelyReenabledClockToZero) {
  const auto fixture = build_reenable_net();
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);

  const size_t loop_clock = fixture.loop + 1;
  initial.Z1.set_constraint(loop_clock, 0, 5);
  initial.Z1.set_constraint(0, loop_clock, -2);

  auto [ok, successor, firing_time] =
      StateClassReachabilityGraphTestAccess::fire_with_dbm(graph, fixture.loop,
                                                          initial);

  ASSERT_TRUE(ok);
  EXPECT_DOUBLE_EQ(firing_time, 2.0);
  EXPECT_TRUE(contains(successor.enabled, fixture.loop));
  EXPECT_EQ(successor.Z1.get_constraint(loop_clock, 0), 0);
  EXPECT_EQ(successor.Z1.get_constraint(0, loop_clock), 0);
}

// Expect: build creates an edge run and a successor marking {ready=0, done=1}.
TEST(PTPNReachabilityTest,
     BuildCreatesSuccessorStateWithExpectedMarkingAndEdge) {
  const auto fixture = build_one_transition_net();
  StateClassReachabilityGraph graph(fixture.net);

  ASSERT_GT(graph.build(4), 0U);
  const auto& sc_graph = graph.get_graph();
  const SCVertex initial = graph.get_initial_vertex();
  ASSERT_TRUE(has_outgoing_transition(sc_graph, initial, fixture.run));

  const SCVertex target = target_for_transition(sc_graph, initial, fixture.run);
  const StateClass& successor = boost::get(boost::vertex_name, sc_graph, target);
  EXPECT_EQ(successor.marking, (Marking{0, 1}));
  EXPECT_FALSE(contains(successor.enabled, fixture.run));
}

// Expect: the two-step chain reaches terminal marking {p0=0, p1=0, p2=1}.
TEST(PTPNReachabilityTest,
     SequentialNetReachesExpectedTerminalMarking) {
  // Diagram:
  //   p0(1) --t0[1,1], prio=10, core=0--> p1(0)
  //   p1    --t1[1,1], prio=10, core=0--> p2(0)
  PTPN net;
  const size_t p0 = net.add_place("p0");
  const size_t p1 = net.add_place("p1");
  const size_t p2 = net.add_place("p2");
  const size_t t0 = net.add_transition("t0", TimeInterval(1, 1), 10, 0);
  const size_t t1 = net.add_transition("t1", TimeInterval(1, 1), 10, 0);
  net.set_pre_arc(p0, t0);
  net.set_post_arc(t0, p1);
  net.set_pre_arc(p1, t1);
  net.set_post_arc(t1, p2);
  net.set_initial_marking({1, 0, 0});

  StateClassReachabilityGraph graph(net);
  ASSERT_GT(graph.build(8), 0U);

  bool found_terminal = false;
  const auto& sc_graph = graph.get_graph();
  auto [vertex_it, vertex_end] = boost::vertices(sc_graph);
  for (; vertex_it != vertex_end; ++vertex_it) {
    const StateClass& state = boost::get(boost::vertex_name, sc_graph, *vertex_it);
    if (state.marking == Marking{0, 0, 1}) {
      found_terminal = true;
      break;
    }
  }
  EXPECT_TRUE(found_terminal);
}

// Expect: high preempts low; low is recorded as suspended and its clock freezes.
TEST(PTPNReachabilityTest,
     ApplyPreemptionSuspendsLowerPrioritySuspendableTransition) {
  const auto fixture = build_priority_same_core_net(true);
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);
  auto chosen =
      StateClassReachabilityGraphTestAccess::select_per_core(graph,
                                                            initial.enabled);

  StateClass scheduled = initial.copy();
  StateClassReachabilityGraphTestAccess::apply_preemption(graph, chosen,
                                                          scheduled);

  const size_t low_clock = fixture.low + 1;
  ASSERT_EQ(chosen.size(), 1U);
  EXPECT_EQ(chosen[0], fixture.high);
  EXPECT_TRUE(contains(scheduled.suspended, fixture.low));
  EXPECT_TRUE(scheduled.Z1.is_frozen(low_clock));
  EXPECT_TRUE(scheduled.Z2.is_frozen(low_clock));
}

// Expect: time advances by high's bound, while low remains suspended and frozen.
TEST(PTPNReachabilityTest,
     MaximalTimeElapseDoesNotAdvanceFrozenSuspendedClock) {
  const auto fixture = build_priority_same_core_net(true);
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);
  auto chosen =
      StateClassReachabilityGraphTestAccess::select_per_core(graph,
                                                            initial.enabled);

  StateClass scheduled = initial.copy();
  StateClassReachabilityGraphTestAccess::apply_preemption(graph, chosen,
                                                          scheduled);
  const size_t low_clock = fixture.low + 1;

  double dt = 0;
  EXPECT_TRUE(StateClassReachabilityGraphTestAccess::maximal_time_elapse(
      graph, scheduled, dt));

  EXPECT_DOUBLE_EQ(dt, 1.0);
  EXPECT_TRUE(contains(scheduled.suspended, fixture.low));
  EXPECT_TRUE(scheduled.Z1.is_frozen(low_clock));
  EXPECT_TRUE(scheduled.Z2.is_frozen(low_clock));
}

// Expect: after high fires, low remains enabled but is no longer suspended.
TEST(PTPNReachabilityTest,
     FiringHigherPriorityTransitionReleasesLowerPrioritySuspension) {
  const auto fixture = build_priority_same_core_net(true);
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);
  auto chosen =
      StateClassReachabilityGraphTestAccess::select_per_core(graph,
                                                            initial.enabled);

  StateClass scheduled = initial.copy();
  StateClassReachabilityGraphTestAccess::apply_preemption(graph, chosen,
                                                          scheduled);

  auto [ok, successor, firing_time] =
      StateClassReachabilityGraphTestAccess::fire_with_dbm(graph, fixture.high,
                                                          scheduled);
  ASSERT_TRUE(ok);
  successor = StateClassReachabilityGraphTestAccess::canonicalize(graph,
                                                                  successor);

  const size_t low_clock = fixture.low + 1;
  EXPECT_EQ(successor.marking, (Marking{0, 1, 1, 0}));
  EXPECT_TRUE(contains(successor.enabled, fixture.low));
  EXPECT_FALSE(contains(successor.suspended, fixture.low));
  EXPECT_FALSE(successor.Z1.is_frozen(low_clock));
  EXPECT_FALSE(successor.Z2.is_frozen(low_clock));
  EXPECT_DOUBLE_EQ(firing_time, 1.0);
}

// Expect: a resumed suspendable transition keeps its stopwatch progress in Z2.
TEST(PTPNReachabilityTest,
     FireWithDbmPreservesResumedSuspendableClockProgress) {
  const auto fixture = build_resume_net();
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);
  auto chosen =
      StateClassReachabilityGraphTestAccess::select_per_core(graph,
                                                            initial.enabled);

  StateClass scheduled = initial.copy();
  StateClassReachabilityGraphTestAccess::apply_preemption(graph, chosen,
                                                          scheduled);

  const size_t low_clock = fixture.low + 1;
  scheduled.Z2.set_constraint(low_clock, 0, 6);
  scheduled.Z2.set_constraint(0, low_clock, -4);

  auto [ok, successor, firing_time] =
      StateClassReachabilityGraphTestAccess::fire_with_dbm(graph, fixture.high,
                                                          scheduled);

  ASSERT_TRUE(ok);
  EXPECT_DOUBLE_EQ(firing_time, 1.0);
  EXPECT_TRUE(contains(successor.enabled, fixture.low));
  EXPECT_FALSE(contains(successor.suspended, fixture.low));
  EXPECT_EQ(successor.Z2.get_constraint(low_clock, 0), 6);
  EXPECT_EQ(successor.Z2.get_constraint(0, low_clock), -4);
}

// Expect: an infinite latest bound is represented as INF_TIME and still fires at the DBM-derived earliest time.
TEST(PTPNReachabilityTest,
     InfiniteLatestBound) {
  // Diagram:
  //   p0(1) --unbounded[1,inf], prio=10, core=0--> p1(0)
  PTPN net;
  const size_t p0 = net.add_place("p0");
  const size_t p1 = net.add_place("p1");
  const size_t t0 = net.add_transition("unbounded", TimeInterval(1, petri::INF),
                                       10, 0, false);
  net.set_pre_arc(p0, t0);
  net.set_post_arc(t0, p1);
  net.set_initial_marking({1, 0});

  StateClassReachabilityGraph graph(net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);
  const size_t clock_idx = t0 + 1;

  EXPECT_EQ(initial.Z1.get_constraint(clock_idx, 0), state_class::INF_TIME);
  EXPECT_EQ(initial.Z1.get_constraint(0, clock_idx), -1);

  auto [ok, successor, firing_time] =
      StateClassReachabilityGraphTestAccess::fire_with_dbm(graph, t0, initial);
  EXPECT_TRUE(ok);
  EXPECT_EQ(successor.marking, (Marking{0, 1}));
  EXPECT_DOUBLE_EQ(firing_time, 1.0);
}

// Expect: firing time comes from the restricted DBM lower bound when it exceeds alpha.
TEST(PTPNReachabilityTest,
     FireWithDbmUsesRestrictedDbmLowerBoundForFiringTime) {
  const auto fixture = build_one_transition_net();
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);

  const size_t clock_idx = fixture.run + 1;
  initial.Z1.set_constraint(0, clock_idx, -4);
  initial.Z1.set_constraint(clock_idx, 0, 5);

  auto [ok, successor, firing_time] =
      StateClassReachabilityGraphTestAccess::fire_with_dbm(graph, fixture.run,
                                                          initial);

  ASSERT_TRUE(ok);
  EXPECT_EQ(successor.marking, (Marking{0, 1}));
  EXPECT_DOUBLE_EQ(firing_time, 4.0);
}

// Expect: DBM time elapse numerically shifts active clock bounds and leaves frozen clocks unchanged.
TEST(PTPNReachabilityTest, DbmElapseTimeShiftsOnlyActiveClocks) {
  state_class::DBM dbm(4);
  dbm.set_constraint(1, 0, 5);
  dbm.set_constraint(0, 1, -2);
  dbm.set_constraint(2, 0, 7);
  dbm.set_constraint(0, 2, -3);
  dbm.freeze_clock(2);

  dbm.elapse_time(4);

  EXPECT_EQ(dbm.get_constraint(1, 0), 9);
  EXPECT_EQ(dbm.get_constraint(0, 1), -6);
  EXPECT_EQ(dbm.get_constraint(2, 0), 7);
  EXPECT_EQ(dbm.get_constraint(0, 2), -3);
}

// Expect: reset_clock preserves DBM closure by copying row/col constraints from clock 0,
// not just zeroing the diagonal.  A stale 10-entry in row/col should be cleared.
TEST(PTPNReachabilityTest, DbmResetClockClearsStaleRowAndColumnConstraints) {
  state_class::DBM dbm(4);
  dbm.set_constraint(1, 0, 5);
  dbm.set_constraint(0, 1, -2);
  dbm.set_constraint(1, 2, 10);
  dbm.set_constraint(3, 1, 10);
  dbm.set_constraint(2, 0, state_class::INF_TIME);
  dbm.set_constraint(0, 2, 0);

  dbm.reset_clock(1);

  EXPECT_EQ(dbm.get_constraint(0, 1), 0);
  EXPECT_EQ(dbm.get_constraint(1, 0), 0);
  EXPECT_EQ(dbm.get_constraint(1, 2), 0);
  EXPECT_EQ(dbm.get_constraint(3, 1), state_class::INF_TIME);
  EXPECT_EQ(dbm.get_constraint(1, 1), 0);
}

// Expect: normalization keeps only the highest-priority effective transition and marks lower suspendable peers as suspended.
TEST(PTPNReachabilityTest,
     NormalizeSchedulingStateComputesEffectiveEnabledAndSuspendedSets) {
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

// Expect: control transitions on core=-1 stay effective and do not suspend task transitions.
TEST(PTPNReachabilityTest,
     NormalizeSchedulingStateDoesNotTreatControlTransitionsAsTaskPreemption) {
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

// Expect: normalizing an active suspendable transition drops stale Z1 leftovers and keeps the live waiting domain in Z2.
TEST(PTPNReachabilityTest,
     NormalizeSchedulingStateClearsStaleZ1ForActiveSuspendableTransition) {
  PTPN net;
  const size_t ready = net.add_place("ready");
  const size_t done = net.add_place("done");
  const size_t susp = net.add_transition("susp", TimeInterval(2, 5), 10, 0,
                                         true);
  net.set_pre_arc(ready, susp);
  net.set_post_arc(susp, done);
  net.set_initial_marking({1, 0});

  StateClassReachabilityGraph graph(net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);

  const size_t clock_idx = susp + 1;
  initial.Z1.set_constraint(clock_idx, 0, 99);
  initial.Z1.set_constraint(0, clock_idx, -88);

  StateClassReachabilityGraphTestAccess::normalize_scheduling_state(graph,
                                                                    initial);

  EXPECT_TRUE(contains(initial.enabled, susp));
  EXPECT_TRUE(initial.suspended.empty());
  EXPECT_EQ(initial.Z1.get_constraint(clock_idx, 0), state_class::INF_TIME);
  EXPECT_EQ(initial.Z1.get_constraint(0, clock_idx), 0);
  EXPECT_EQ(initial.Z2.get_constraint(clock_idx, 0), 5);
  EXPECT_EQ(initial.Z2.get_constraint(0, clock_idx), -2);
}

// Expect: canonicalization preserves the normalized effective-enabled metadata rather than recomputing raw enabledness.
TEST(PTPNReachabilityTest,
     CanonicalizePreservesNormalizedSchedulingMetadata) {
  const auto fixture = build_priority_same_core_net(true);
  StateClassReachabilityGraph graph(fixture.net);
  StateClass initial =
      StateClassReachabilityGraphTestAccess::create_initial_state_class(graph);

  StateClassReachabilityGraphTestAccess::normalize_scheduling_state(graph,
                                                                    initial);
  StateClass canonical =
      StateClassReachabilityGraphTestAccess::canonicalize(graph, initial);

  EXPECT_TRUE(contains(canonical.enabled, fixture.high));
  EXPECT_FALSE(contains(canonical.enabled, fixture.low));
  EXPECT_TRUE(contains(canonical.suspended, fixture.low));
}

// Expect: parallel build preserves the same initial outgoing transitions and terminal reachability.
TEST(PTPNReachabilityTest, ParallelBuildMatchesSequentialGraphSummary) {
  PTPN net;
  const size_t p0 = net.add_place("p0");
  const size_t p1 = net.add_place("p1");
  const size_t p2 = net.add_place("p2");
  const size_t t0 = net.add_transition("t0", TimeInterval(1, 1), 10, 0);
  const size_t t1 = net.add_transition("t1", TimeInterval(1, 1), 10, 0);
  net.set_pre_arc(p0, t0);
  net.set_post_arc(t0, p1);
  net.set_pre_arc(p1, t1);
  net.set_post_arc(t1, p2);
  net.set_initial_marking({1, 0, 0});

  StateClassReachabilityGraph sequential(net);
  StateClassReachabilityGraph parallel(net);

  ASSERT_GT(sequential.build(8, 1), 0U);
  ASSERT_GT(parallel.build(8, 4), 0U);

  EXPECT_EQ(sequential.get_statistics().total_states,
            parallel.get_statistics().total_states);
  EXPECT_EQ(sequential.get_statistics().total_transitions,
            parallel.get_statistics().total_transitions);

  const auto sequential_initial_edges = outgoing_transition_ids(
      sequential.get_graph(), sequential.get_initial_vertex());
  const auto parallel_initial_edges = outgoing_transition_ids(
      parallel.get_graph(), parallel.get_initial_vertex());
  EXPECT_EQ(sequential_initial_edges, parallel_initial_edges);

  EXPECT_TRUE(has_marking(sequential.get_graph(), Marking{0, 0, 1}));
  EXPECT_TRUE(has_marking(parallel.get_graph(), Marking{0, 0, 1}));
}

// Expect: auto-thread build truncates consistently once max_states is reached.
TEST(PTPNReachabilityTest, ParallelBuildReportsTruncation) {
  const auto fixture = build_multi_core_net();
  StateClassReachabilityGraph graph(fixture.net);

  ASSERT_GT(graph.build(1, 0), 0U);
  EXPECT_TRUE(graph.get_statistics().truncated);
  EXPECT_EQ(graph.get_statistics().total_states, 1U);
}
