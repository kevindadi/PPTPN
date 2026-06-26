#ifndef ANALYSIS_STATE_H
#define ANALYSIS_STATE_H

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "clock_state.h"
#include "dbm.h"

namespace state_class {

struct ReachabilityState;
struct StateKey;
struct StateKeyHash;
struct TransitionEdge;
struct SuccessorCandidate;

struct TransitionEdge {
  int transition_id;
  double firing_time;

  TransitionEdge() : transition_id(-1), firing_time(0.0) {}
  TransitionEdge(int tid, double time) : transition_id(tid), firing_time(time) {}

  bool operator==(const TransitionEdge& other) const {
    return transition_id == other.transition_id &&
           std::abs(firing_time - other.firing_time) < 1e-9;
  }

  [[nodiscard]] std::string to_string() const {
    std::ostringstream oss;
    oss << "T" << transition_id << "@" << firing_time;
    return oss.str();
  }
};

struct SchedulingState {
  std::set<size_t> enabled;
  std::set<size_t> active;
  std::set<size_t> suspended;

  bool operator==(const SchedulingState& other) const {
    return enabled == other.enabled && active == other.active && suspended == other.suspended;
  }

  bool operator<(const SchedulingState& other) const {
    if (enabled < other.enabled) return true;
    if (other.enabled < enabled) return false;
    if (active < other.active) return true;
    if (other.active < active) return false;
    return suspended < other.suspended;
  }

  void clear() {
    enabled.clear();
    active.clear();
    suspended.clear();
  }
};

struct SearchMetadata {
  double cumulative_time = 0.0;
  size_t state_id = 0;

  void clear() {
    cumulative_time = 0.0;
    state_id = 0;
  }
};

struct TimingState {
  std::vector<TransitionClock> clocks;
  DBM zone;
  std::vector<int> transition_to_clock;
  std::vector<size_t> clock_to_transition;

  bool operator==(const TimingState& other) const {
    return clocks == other.clocks && zone == other.zone &&
           transition_to_clock == other.transition_to_clock &&
           clock_to_transition == other.clock_to_transition;
  }

  bool operator<(const TimingState& other) const {
    if (transition_to_clock < other.transition_to_clock) return true;
    if (other.transition_to_clock < transition_to_clock) return false;
    if (clock_to_transition < other.clock_to_transition) return true;
    if (other.clock_to_transition < clock_to_transition) return false;
    if (zone < other.zone) return true;
    if (other.zone < zone) return false;
    return clocks < other.clocks;
  }

  void clear() {
    clocks.clear();
    zone = DBM{};
    transition_to_clock.clear();
    clock_to_transition.clear();
  }
};

struct ReachabilityState {
  std::vector<int> marking;
  TimingState timing;
  SchedulingState scheduling;
  SearchMetadata metadata;

  ReachabilityState() = default;

  explicit ReachabilityState(const std::vector<int>& m, size_t num_transitions = 0)
      : marking(m) {
    if (num_transitions > 0) {
      timing.clocks.resize(num_transitions);
    }
  }

  ReachabilityState(const ReachabilityState& other) = default;
  ReachabilityState& operator=(const ReachabilityState& other) = default;

  bool operator==(const ReachabilityState& other) const;
  bool operator<(const ReachabilityState& other) const;

  [[nodiscard]] ReachabilityState copy() const;
  [[nodiscard]] std::string to_string() const;

  void rebuild_zone_from_clocks();
  void sync_clocks_from_zone();
  [[nodiscard]] int clock_index_for_transition(size_t transition_id) const;
  [[nodiscard]] size_t transition_for_clock(size_t clock_idx) const;
  [[nodiscard]] bool has_zone_clock_for_transition(size_t transition_id) const;

  [[nodiscard]] bool has_active_clocks() const { return !scheduling.active.empty(); }

  [[nodiscard]] int get_next_deadline() const {
    int min_deadline = INF_TIME;
    for (size_t t : scheduling.active) {
      if (t < timing.clocks.size()) {
        min_deadline = std::min(min_deadline, timing.clocks[t].upper_bound);
      }
    }
    return min_deadline;
  }
};

struct StateKey {
  std::vector<int> marking;
  std::vector<int> transition_to_clock;
  std::vector<size_t> clock_to_transition;
  std::vector<int> zone_matrix;
  std::set<size_t> frozen_clocks;
  SchedulingState scheduling;

  bool operator==(const StateKey& other) const {
    return marking == other.marking && transition_to_clock == other.transition_to_clock &&
           clock_to_transition == other.clock_to_transition && zone_matrix == other.zone_matrix &&
           frozen_clocks == other.frozen_clocks && scheduling == other.scheduling;
  }
};

struct StateKeyHash {
  size_t operator()(const StateKey& key) const;
};

struct SuccessorCandidate {
  ReachabilityState state;
  TransitionEdge edge;
  size_t transition_id = 0;
};

}  // namespace state_class

#endif  // ANALYSIS_STATE_H
