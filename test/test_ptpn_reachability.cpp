#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <tuple>
#include <vector>

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/properties.hpp>

#include "analysis/state.h"
#include "petri/petri.h"

namespace state_class {

struct StateClassReachabilityGraphTestAccess {
  static StateClass create_initial_state_class(
      StateClassReachabilityGraph& graph) {
    return graph.create_initial_state_class();
  }

  static std::vector<size_t> collect_enabled_transitions(
      const StateClassReachabilityGraph& graph, const StateClass& state) {
    return graph.collect_enabled_transitions(state);
  }

  static std::vector<size_t> select_per_core(
      const StateClassReachabilityGraph& graph, const std::set<size_t>& enabled) {
    return graph.select_per_core(enabled);
  }

  static void apply_preemption(const StateClassReachabilityGraph& graph,
                               const std::vector<size_t>& chosen,
                               StateClass& state) {
    graph.apply_preemption(chosen, state);
  }

  static bool maximal_time_elapse(const StateClassReachabilityGraph& graph,
                                  StateClass& state, double& dt) {
    return graph.maximal_time_elapse(state, dt);
  }

  static std::tuple<bool, StateClass, double> fire_with_dbm(
      StateClassReachabilityGraph& graph, size_t trans_idx,
      const StateClass& state) {
    return graph.fire_with_dbm(trans_idx, state);
  }

  static StateClass canonicalize(const StateClassReachabilityGraph& graph,
                                 const StateClass& state) {
    return graph.canonicalize(state);
  }
};

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

// Expect: an infinite latest bound is represented as INF_TIME and still fires at alpha.
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
