#ifndef TEST_STATE_CLASS_TEST_ACCESS_H
#define TEST_STATE_CLASS_TEST_ACCESS_H

#include <cstddef>
#include <set>
#include <tuple>
#include <vector>

#include "analysis/state.h"

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

  static void normalize_scheduling_state(
      const StateClassReachabilityGraph& graph, StateClass& state) {
    graph.normalize_scheduling_state(state);
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

#endif
