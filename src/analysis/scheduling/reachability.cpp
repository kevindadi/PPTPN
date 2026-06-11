#include "reachability.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace scheduling {

ReachabilityGraph::ReachabilityGraph(const petri::PTPN& ptpn)
    : ptpn_(ptpn), next_state_id_(0) {}

StateKey ReachabilityGraph::make_key(const StateClass& state) const {
  StateKey key;
  key.marking = state.marking;
  key.zone_matrix = state.zone.raw_matrix();
  key.frozen_clocks = state.zone.frozen_clocks();
  key.enabled = state.enabled;
  key.active = state.active;
  key.suspended = state.suspended;
  return key;
}

Vertex ReachabilityGraph::find_or_add_state(const StateClass& state) {
  StateKey key = make_key(state);
  auto it = state_to_vertex_.find(key);
  if (it != state_to_vertex_.end()) {
    stats_.dedup_hits++;
    return it->second;
  }

  stats_.dedup_misses++;
  StateClass new_state = state;
  new_state.state_id = next_state_id_++;

  Vertex v = states_.size();
  states_.push_back(new_state);
  state_to_vertex_.emplace(std::move(key), v);

  return v;
}

std::set<size_t> ReachabilityGraph::compute_enabled(
    const std::vector<int>& marking) const {
  std::set<size_t> enabled;
  const size_t num_transitions = ptpn_.num_transitions();

  for (size_t t = 0; t < num_transitions; ++t) {
    if (petri::PTPN::is_enabled(marking, ptpn_, t)) {
      enabled.insert(t);
    }
  }

  return enabled;
}

int ReachabilityGraph::compute_firing_time(
    const StateClass& state, size_t t) const {
  if (!state.enabled.count(t)) {
    return -1;
  }

  if (state.suspended.count(t)) {
    return -1;  // 挂起的变迁不能发生
  }

  if (!state.has_clock_for_transition(t)) {
    return -1;
  }

  const auto& trans = ptpn_.get_transition(t);
  const int alpha = trans.time_interval.earliest;
  const int beta = trans.time_interval.latest == petri::INF
                       ? INF_TIME
                       : trans.time_interval.latest;

  const size_t clock_idx = static_cast<size_t>(
      state.clock_index_for_transition(t));
  const int lower_bound = state.zone.get_lower_bound(clock_idx);
  const int upper_bound = state.zone.get_upper_bound(clock_idx);

  // τ(t) = max(α(t), lb_t)
  const int firing_time = std::max(alpha, lower_bound);

  // 检查是否在时间窗口内
  if (firing_time > upper_bound) {
    return -1;
  }
  if (beta != INF_TIME && firing_time > beta) {
    return -1;
  }

  return firing_time;
}

void ReachabilityGraph::advance_time(StateClass& state, int delta) const {
  if (delta <= 0) return;

  // 推进所有非冻结的 active 时钟
  state.zone.elapse_time(delta);
  state.cumulative_time += delta;
}

}  // namespace scheduling