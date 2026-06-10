#include "analysis/graph.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <sstream>
#include <vector>

#include "scheduling.h"

namespace state_class {

namespace {
constexpr int kControlTransitionPriority = 0;

int latest_bound_for_transition(const petri::Transition& trans) {
  return trans.time_interval.latest == petri::INF ? INF_TIME
                                                 : trans.time_interval.latest;
}

std::pair<int, int> current_clock_bounds(const StateClass& state, size_t t);

bool safe_add_graph_bound(int lhs, int rhs, int& result) {
  if (lhs == INF_TIME) {
    result = INF_TIME;
    return true;
  }

  if ((rhs > 0 && lhs > std::numeric_limits<int>::max() - rhs) ||
      (rhs < 0 && lhs < std::numeric_limits<int>::min() - rhs)) {
    result = rhs > 0 ? INF_TIME : std::numeric_limits<int>::min();
    return true;
  }

  result = lhs + rhs;
  return true;
}

bool elapse_active_clocks(StateClass& state, int delay) {
  if (delay < 0) {
    return false;
  }

  for (size_t t : state.active) {
    if (t >= state.clocks.size()) {
      continue;
    }

    const auto [clock_lower, clock_upper] = current_clock_bounds(state, t);
    if (clock_upper != INF_TIME && clock_lower + delay > clock_upper) {
      return false;
    }
  }

  if (delay > 0 && state.zone.size() > 0) {
    DBM next_zone = state.zone;

    for (size_t t : state.active) {
      if (!state.has_zone_clock_for_transition(t)) {
        continue;
      }

      const size_t clock_idx =
          static_cast<size_t>(state.clock_index_for_transition(t));
      const int current_lower = state.zone.get_constraint(0, clock_idx);
      int updated_lower = current_lower;
      safe_add_graph_bound(current_lower, -delay, updated_lower);
      next_zone.set_constraint(0, clock_idx, updated_lower);
    }

    for (size_t i = 1; i < state.zone.size(); ++i) {
      const size_t ti = state.transition_for_clock(i);
      const int age_i = state.active.count(ti) ? delay : 0;

      for (size_t j = 1; j < state.zone.size(); ++j) {
        const size_t tj = state.transition_for_clock(j);
        const int age_j = state.active.count(tj) ? delay : 0;
        const int delta_ij = age_i - age_j;
        if (delta_ij == 0) {
          continue;
        }

        const int current_bound = state.zone.get_constraint(i, j);
        int updated_bound = current_bound;
        safe_add_graph_bound(current_bound, delta_ij, updated_bound);
        next_zone.set_constraint(i, j, updated_bound);
      }
    }

    next_zone.minimize();
    state.zone = std::move(next_zone);
    state.sync_clocks_from_zone();
  } else {
    for (size_t t : state.active) {
      if (t >= state.clocks.size()) {
        continue;
      }
      state.clocks[t].lower_bound += delay;
    }
  }

  state.cumulative_time += delay;
  return true;
}

// State key helper using new clocks/active/suspended structure
StateKey make_state_key(const StateClass& state) {
  StateKey key;
  key.marking = state.marking;
  key.transition_to_clock = state.transition_to_clock;
  key.clock_to_transition = state.clock_to_transition;
  key.zone_matrix = state.zone.raw_matrix();
  key.frozen_clocks = state.zone.frozen_clocks();
  key.enabled = state.enabled;
  key.active = state.active;
  key.suspended = state.suspended;
  return key;
}

std::pair<int, int> current_clock_bounds(const StateClass& state, size_t t) {
  if (t < state.clocks.size() && state.has_zone_clock_for_transition(t) &&
      state.zone.size() > 0) {
    const size_t clock_idx =
        static_cast<size_t>(state.clock_index_for_transition(t));
    return {-state.zone.get_constraint(0, clock_idx),
            state.zone.get_constraint(clock_idx, 0)};
  }

  if (t < state.clocks.size()) {
    return {state.clocks[t].lower_bound, state.clocks[t].upper_bound};
  }

  return {0, INF_TIME};
}

bool is_uninitialized_clock(const TransitionClock& clock) {
  return clock.state == ClockState::UNACTIVE && clock.lower_bound == 0 &&
         clock.upper_bound == INF_TIME;
}

void rebuild_zone_preserving_constraints(
    StateClass& state, const DBM& previous_zone,
    const std::vector<int>& previous_transition_to_clock,
    const std::set<size_t>& previously_enabled,
    const std::set<size_t>& reset_transitions) {
  state.transition_to_clock.assign(state.clocks.size(), -1);
  state.clock_to_transition.clear();
  state.clock_to_transition.push_back(std::numeric_limits<size_t>::max());

  DBM next_zone(1);
  for (size_t t : state.enabled) {
    if (t >= state.clocks.size()) {
      continue;
    }

    const size_t clock_idx = next_zone.add_clock();
    state.transition_to_clock[t] = static_cast<int>(clock_idx);
    state.clock_to_transition.push_back(t);
  }

  const auto has_previous_clock = [&](size_t t) {
    return previously_enabled.count(t) && !reset_transitions.count(t) &&
           t < previous_transition_to_clock.size() &&
           previous_transition_to_clock[t] > 0 &&
           static_cast<size_t>(previous_transition_to_clock[t]) < previous_zone.size();
  };

  for (size_t ti : state.enabled) {
    if (ti >= state.transition_to_clock.size() || state.transition_to_clock[ti] <= 0) {
      continue;
    }

    const size_t new_i = static_cast<size_t>(state.transition_to_clock[ti]);
    if (has_previous_clock(ti)) {
      const size_t old_i = static_cast<size_t>(previous_transition_to_clock[ti]);
      next_zone.set_constraint(0, new_i, previous_zone.get_constraint(0, old_i));
      next_zone.set_constraint(new_i, 0, previous_zone.get_constraint(old_i, 0));
    } else {
      const auto& clock = state.clocks[ti];
      next_zone.set_constraint(0, new_i, -clock.lower_bound);
      next_zone.set_constraint(new_i, 0, clock.upper_bound);
    }
  }

  for (size_t ti : state.enabled) {
    if (!has_previous_clock(ti)) {
      continue;
    }

    const size_t new_i = static_cast<size_t>(state.transition_to_clock[ti]);
    const size_t old_i = static_cast<size_t>(previous_transition_to_clock[ti]);
    for (size_t tj : state.enabled) {
      if (!has_previous_clock(tj)) {
        continue;
      }

      const size_t new_j = static_cast<size_t>(state.transition_to_clock[tj]);
      const size_t old_j = static_cast<size_t>(previous_transition_to_clock[tj]);
      next_zone.set_constraint(new_i, new_j,
                               previous_zone.get_constraint(old_i, old_j));
    }
  }

  next_zone.minimize();
  state.zone = std::move(next_zone);
}

void sync_zone_activity(StateClass& state) {
  if (state.zone.size() == 0) {
    return;
  }

  for (size_t t : state.enabled) {
    if (!state.has_zone_clock_for_transition(t)) {
      continue;
    }

    const size_t clock_idx =
        static_cast<size_t>(state.clock_index_for_transition(t));
    if (state.active.count(t)) {
      state.zone.unfreeze_clock(clock_idx);
    } else {
      state.zone.freeze_clock(clock_idx);
    }
  }

  state.sync_clocks_from_zone();
}

void add_expansion_stats(StateClassReachabilityGraph::Statistics& target,
                         const StateExpansionResult& result) {
  target.enabled_transitions_count += result.enabled_transitions_count;
  target.pruned_states_count += result.pruned_states_count;
  target.transition_enabled_checks += result.transition_enabled_checks;
}

std::string format_transition_vector(const std::vector<size_t>& trans_indices,
                                     const petri::PTPN& ptpn,
                                     bool detailed = true) {
  if (trans_indices.empty()) {
    return "(none)";
  }

  std::string result;
  bool first = true;
  for (size_t t : trans_indices) {
    if (!first) {
      result += ", ";
    }
    first = false;

    if (detailed && t < ptpn.num_transitions()) {
      const auto& trans = ptpn.get_transition(t);
      result += "T" + std::to_string(t) + "(" + trans.name;
      result += ", priority=" + std::to_string(trans.priority);
      result += ", core=" + std::to_string(trans.core);
      result += trans.suspendable ? ", suspendable" : "";
      result += ")";
    } else {
      result += "T" + std::to_string(t);
    }
  }
  return result;
}

std::string format_transition_clock_summary(const StateClass& state,
                                           const petri::PTPN& ptpn) {
  std::ostringstream oss;
  bool first = true;

  for (size_t t : state.enabled) {
    if (t >= state.clocks.size()) continue;

    if (!first) oss << "; ";
    first = false;

    const auto& trans = ptpn.get_transition(t);
    const auto& clock = state.clocks[t];
    std::string state_str;
    switch (clock.state) {
      case ClockState::UNACTIVE: state_str = "UNACTIVE"; break;
      case ClockState::ACTIVE: state_str = "ACTIVE"; break;
      case ClockState::SUSPENDED: state_str = "SUSPENDED"; break;
    }

    std::string ub_str = (clock.upper_bound == INF_TIME)
                             ? "inf"
                             : std::to_string(clock.upper_bound);

    oss << "T" << t << "(" << trans.name << ")"
        << "[" << clock.lower_bound << ", " << ub_str << "]"
        << "@" << state_str;
  }

  return first ? "(none)" : oss.str();
}

}  // namespace

static void debug(const std::string& msg) {
  spdlog::debug("[STATE] {}", msg);
}

static void info(const std::string& msg) {
  spdlog::info("[STATE] {}", msg);
}

std::string StateClassReachabilityGraph::format_marking(
    const std::vector<int>& marking) {
  std::string result = "[";
  for (size_t i = 0; i < marking.size(); ++i) {
    result += std::to_string(marking[i]);
    if (i < marking.size() - 1) {
      result += ", ";
    }
  }
  result += "]";
  return result;
}

std::string StateClassReachabilityGraph::format_transitions(
    const std::set<size_t>& trans_indices, bool detailed) const {
  if (trans_indices.empty()) {
    return "(none)";
  }

  std::string result;
  bool first = true;
  for (size_t t : trans_indices) {
    if (!first) {
      result += ", ";
    }
    first = false;

    if (detailed && t < ptpn_.num_transitions()) {
      const auto& trans = ptpn_.get_transition(t);
      result += "T" + std::to_string(t) + "(" + trans.name;
      result += ", priority=" + std::to_string(trans.priority);
      result += ", core=" + std::to_string(trans.core);
      result += trans.suspendable ? ", suspendable" : "";
      result += ")";
    } else {
      result += "T" + std::to_string(t);
    }
  }
  return result;
}

std::string StateClassReachabilityGraph::format_places(
    const std::vector<int>& marking) const {
  std::string result = "[";
  bool first = true;
  for (size_t i = 0; i < marking.size(); ++i) {
    if (!first) {
      result += ", ";
    }
    first = false;

    if (i < ptpn_.num_places()) {
      const auto& place = ptpn_.get_place(i);
      result += "P" + std::to_string(i) + "(" + place.name +
                ")=" + std::to_string(marking[i]);
    } else {
      result += "P" + std::to_string(i) + "=" + std::to_string(marking[i]);
    }
  }
  result += "]";
  return result;
}

void StateClassReachabilityGraph::log_state_class_details(
    const StateClass& state, const std::string& prefix) const {
  spdlog::debug("{}========== State Class Details: ID={} ==========", prefix, state.state_id);
  spdlog::debug("{}Places: {}", prefix, format_places(state.marking));
  spdlog::debug("{}Enabled: {}", prefix, format_transitions(state.enabled));
  spdlog::debug("{}Active: {}", prefix, format_transitions(state.active));
  if (!state.suspended.empty()) {
    spdlog::debug("{}Suspended: {}", prefix, format_transitions(state.suspended));
  }
  spdlog::debug("{}Clocks: {}", prefix, format_transition_clock_summary(state, ptpn_));
  spdlog::debug("{}Cumulative time: {}", prefix, state.cumulative_time);
  spdlog::debug("{}==========================================", prefix);
}

StateClassReachabilityGraph::StateClassReachabilityGraph(const petri::PTPN& ptpn)
    : ptpn_(ptpn), next_state_id_(0), pruning_enabled_(false) {}

size_t StateClassReachabilityGraph::build(size_t max_states) {
  stats_ = Statistics();
  graph_.clear();
  state_to_vertex_.clear();
  next_state_id_ = 0;

  StateClass s0 = create_initial_state();
  s0 = canonicalize(s0, s0);

  SCVertex s0_vertex = find_or_add_vertex(s0);
  initial_vertex_ = s0_vertex;
  stats_.total_states++;

  std::vector<StateClass> frontier{s0};
  size_t max_frontier_size = frontier.size();

  // Single-threaded BFS expansion
  while (!frontier.empty()) {
    if (stats_.total_states >= max_states) {
      stats_.truncated = true;
      break;
    }

    std::vector<StateExpansionResult> results;
    results.reserve(frontier.size());

    // Expand each state in frontier
    for (const auto& cur : frontier) {
      results.push_back(expand_state_candidates(cur));
    }

    std::vector<StateClass> next_frontier;

    for (size_t i = 0; i < frontier.size(); ++i) {
      StateClass cur = frontier[i];
      SCVertex u = find_or_add_vertex(cur);
      const StateClass& graph_cur = boost::get(boost::vertex_name, graph_, u);
      cur.state_id = graph_cur.state_id;

      log_state_class_details(cur,
                              "[State " + std::to_string(cur.state_id) + "] ");

      const StateExpansionResult& result = results[i];
      add_expansion_stats(stats_, result);

      for (const auto& candidate : result.candidates) {
        spdlog::debug("[STATE] State {} --T{}@{}--> candidate",
                      cur.state_id, candidate.transition_id,
                      candidate.edge.firing_time);

        auto state_it = state_to_vertex_.find(make_state_key(candidate.state));
        SCVertex v;
        if (state_it != state_to_vertex_.end()) {
          v = state_it->second;
          const StateClass& existing_state = boost::get(boost::vertex_name, graph_, v);
          stats_.dedup_hits_count++;
          spdlog::debug("[STATE]   [Existing] candidate merged into state {}",
                        existing_state.state_id);
          log_state_class_details(
              existing_state,
              "[Existing state " + std::to_string(existing_state.state_id) + "] ");
        } else {
          if (stats_.total_states >= max_states) {
            stats_.truncated = true;
            break;
          }

          v = find_or_add_vertex(candidate.state);
          const StateClass& new_graph_state = boost::get(boost::vertex_name, graph_, v);
          StateClass queued_state = candidate.state;
          queued_state.state_id = new_graph_state.state_id;
          next_frontier.push_back(queued_state);
          max_frontier_size = std::max(max_frontier_size, next_frontier.size());
          stats_.total_states++;
          stats_.dedup_misses_count++;
          spdlog::debug("[STATE]   [New] Add state {} to graph and frontier",
                        new_graph_state.state_id);
          log_state_class_details(
              new_graph_state,
              "[New state " + std::to_string(new_graph_state.state_id) + "] ");
        }

        boost::add_edge(u, v, candidate.edge, graph_);
        stats_.total_transitions++;
      }

      spdlog::debug(
          "[STATE] State {}: {} candidates, {} fired, frontier size: {}, total states: {}",
          cur.state_id, result.chosen_count, result.fired_count,
          next_frontier.size(), stats_.total_states);

      if (stats_.truncated) {
        break;
      }
    }

    frontier = std::move(next_frontier);
  }

  if (stats_.truncated) {
    spdlog::warn(
        "[STATE] Build truncated at max_states={} with {} states and {} frontier states remaining",
        max_states, stats_.total_states, frontier.size());
  }

  info("Build complete: states=" + std::to_string(stats_.total_states) +
       ", transitions=" + std::to_string(stats_.total_transitions) +
       ", dedup_hits=" + std::to_string(stats_.dedup_hits_count) +
       ", dedup_misses=" + std::to_string(stats_.dedup_misses_count) +
       ", is_enabled_checks=" +
       std::to_string(stats_.transition_enabled_checks) +
       ", max_frontier_size=" + std::to_string(max_frontier_size) +
       ", truncated=" + (stats_.truncated ? "true" : "false"));

  return stats_.total_states;
}

int StateClassReachabilityGraph::compute_firing_time(const StateClass& state,
                                                    size_t t) const {
  if (t >= state.clocks.size()) {
    return -1;
  }

  if (!state.enabled.count(t) || !state.active.count(t) || state.suspended.count(t)) {
    return -1;
  }

  const auto& trans = ptpn_.get_transition(t);
  const int alpha = trans.time_interval.earliest;
  const int beta = latest_bound_for_transition(trans);
  const auto [clock_lower, clock_upper] = current_clock_bounds(state, t);
  const int firing_time = std::max(alpha, clock_lower);

  if (beta != INF_TIME && firing_time > beta) {
    return -1;
  }
  if (clock_upper != INF_TIME && firing_time > clock_upper) {
    return -1;
  }

  return firing_time;
}

StateExpansionResult StateClassReachabilityGraph::expand_state_candidates(
    const StateClass& cur) {
  const size_t enabled_checks_before = stats_.transition_enabled_checks;

  StateExpansionResult result;

  StateClass scheduled = cur.copy();
  recompute_enabled_sets(scheduled);

  if (pruning_enabled_ && scheduled.active.empty()) {
    debug("  [Prune] No active transitions, skip");
    result.pruned_states_count++;
    result.transition_enabled_checks +=
        stats_.transition_enabled_checks - enabled_checks_before;
    return result;
  }

  int tau_min = INF_TIME;
  for (size_t t : scheduled.active) {
    const int tau = compute_firing_time(scheduled, t);
    if (tau >= 0) {
      tau_min = std::min(tau_min, tau);
    }
  }

  if (tau_min == INF_TIME) {
    if (pruning_enabled_) {
      result.pruned_states_count++;
    }
    result.transition_enabled_checks +=
        stats_.transition_enabled_checks - enabled_checks_before;
    return result;
  }

  std::set<size_t> firable_now;
  for (size_t t : scheduled.active) {
    const int tau = compute_firing_time(scheduled, t);
    if (tau == tau_min) {
      firable_now.insert(t);
    }
  }

  const std::set<size_t> schedulable =
      SchedulingAlgorithms::select_active_per_core(firable_now, ptpn_);
  result.chosen_count = schedulable.size();
  result.enabled_transitions_count += schedulable.size();

  if (schedulable.empty()) {
    if (pruning_enabled_) {
      result.pruned_states_count++;
    }
    result.transition_enabled_checks +=
        stats_.transition_enabled_checks - enabled_checks_before;
    return result;
  }

  const StateKey source_key = make_state_key(cur);

  spdlog::debug("  Earliest firing time: {}, firable_now={}, schedulable={}",
                tau_min, firable_now.size(), schedulable.size());

  for (size_t chosen : schedulable) {
    auto [ok, nxt, tau] = fire_with_time(chosen, scheduled);
    if (!ok) {
      if (pruning_enabled_) {
        spdlog::debug("  {}: fire failed", format_transitions({chosen}, false));
        result.pruned_states_count++;
      } else {
        spdlog::debug("  {}: fire failed [pruning disabled]",
                      format_transitions({chosen}, false));
      }
      continue;
    }

    StateClass canonical_nxt = canonicalize(nxt, nxt);
    result.candidates.push_back({source_key, canonical_nxt,
                                 TransitionEdge(static_cast<int>(chosen), tau), chosen});
    result.fired_count++;
  }

  result.transition_enabled_checks +=
      stats_.transition_enabled_checks - enabled_checks_before;
  return result;
}

// =======================================================================
// 核心算法实现
// =======================================================================

double StateClassReachabilityGraph::advance_time(StateClass& state) const {
  int min_delay = INF_TIME;
  for (size_t t : state.active) {
    const int tau = compute_firing_time(state, t);
    if (tau >= 0) {
      min_delay = std::min(min_delay, tau);
    }
  }

  if (min_delay == INF_TIME) {
    return 0.0;
  }

  if (!elapse_active_clocks(state, min_delay)) {
    return 0.0;
  }

  spdlog::debug("  advance_time: dt={}, new cumulative={}", min_delay, state.cumulative_time);

  return static_cast<double>(min_delay);
}

std::tuple<bool, StateClass, double> StateClassReachabilityGraph::fire_with_time(
    size_t t, const StateClass& from) const {
  return fire_with_dbm(t, from);
}

void StateClassReachabilityGraph::recompute_enabled_sets(StateClass& state) const {
  recompute_enabled_sets_from_marking(state.marking, state);
}

void StateClassReachabilityGraph::recompute_enabled_sets_from_marking(
    const std::vector<int>& marking, StateClass& state) const {
  recompute_enabled_sets_from_marking(marking, state, {});
}

void StateClassReachabilityGraph::recompute_enabled_sets_from_marking(
    const std::vector<int>& marking, StateClass& state,
    const std::set<size_t>& force_reset_transitions) const {
  state.marking = marking;

  std::vector<size_t> raw_enabled;
  const size_t num_transitions = ptpn_.num_transitions();
  const std::set<size_t> previously_enabled = state.enabled;
  const DBM previous_zone = state.zone;
  const std::vector<int> previous_transition_to_clock = state.transition_to_clock;

  for (size_t t = 0; t < num_transitions; ++t) {
    if (petri::PTPN::is_enabled(marking, ptpn_, t)) {
      raw_enabled.push_back(t);
    }
  }

  std::set<size_t> raw_set(raw_enabled.begin(), raw_enabled.end());

  if (state.clocks.size() < num_transitions) {
    state.clocks.resize(num_transitions);
  }

  std::set<size_t> reset_transitions = force_reset_transitions;
  for (size_t t = 0; t < num_transitions; ++t) {
    if (!raw_set.count(t)) {
      state.clocks[t] = TransitionClock();
      continue;
    }

    const bool needs_initialization =
        !previously_enabled.count(t) || is_uninitialized_clock(state.clocks[t]) ||
        force_reset_transitions.count(t);

    if (needs_initialization) {
      const auto& trans = ptpn_.get_transition(t);
      state.clocks[t].lower_bound = 0;
      state.clocks[t].upper_bound = latest_bound_for_transition(trans);
      state.clocks[t].state = ClockState::UNACTIVE;
      reset_transitions.insert(t);
    }
  }

  state.enabled = raw_set;
  state.active = SchedulingAlgorithms::select_active_per_core(state.enabled, ptpn_);
  state.suspended = SchedulingAlgorithms::compute_suspended(state.enabled, state.active, ptpn_);

  for (size_t t : state.enabled) {
    if (state.active.count(t)) {
      state.clocks[t].state = ClockState::ACTIVE;
    } else if (state.suspended.count(t)) {
      state.clocks[t].state = ClockState::SUSPENDED;
    } else {
      state.clocks[t].state = ClockState::UNACTIVE;
    }
  }

  rebuild_zone_preserving_constraints(state, previous_zone,
                                      previous_transition_to_clock,
                                      previously_enabled, reset_transitions);
  sync_zone_activity(state);

  spdlog::debug("  recompute_enabled: enabled={}, active={}, suspended={}",
                state.enabled.size(), state.active.size(), state.suspended.size());
}

// =======================================================================
// 调度相关（使用 SchedulingAlgorithms）
// =======================================================================

std::set<size_t> StateClassReachabilityGraph::select_active_per_core(
    const std::set<size_t>& enabled) const {
  return SchedulingAlgorithms::select_active_per_core(enabled, ptpn_);
}

std::set<size_t> StateClassReachabilityGraph::compute_suspended(
    const std::set<size_t>& enabled,
    const std::set<size_t>& active) const {
  return SchedulingAlgorithms::compute_suspended(enabled, active, ptpn_);
}

// =======================================================================
// 抢占/恢复语义
// =======================================================================

void StateClassReachabilityGraph::suspend_transition(size_t t, StateClass& state) const {
  if (state.clocks[t].state != ClockState::ACTIVE) {
    return;
  }

  state.active.erase(t);
  state.suspended.insert(t);
  state.clocks[t].state = ClockState::SUSPENDED;
  sync_zone_activity(state);

  spdlog::debug("  suspend_transition: T{} now frozen", t);
}

void StateClassReachabilityGraph::restore_transition(size_t t, StateClass& state) const {
  if (state.clocks[t].state != ClockState::SUSPENDED) {
    return;
  }

  state.suspended.erase(t);
  state.active.insert(t);
  state.clocks[t].state = ClockState::ACTIVE;
  sync_zone_activity(state);

  spdlog::debug("  restore_transition: T{} resumed", t);
}



StateClass StateClassReachabilityGraph::canonicalize(
    const StateClass& a, const StateClass& b) const {
  return ::state_class::canonicalize(a, b, canonicalization_mode_);
}

bool StateClassReachabilityGraph::are_equivalent(
    const StateClass& a, const StateClass& b) const {
  return ::state_class::are_equivalent(a, b, canonicalization_mode_);
}

void StateClassReachabilityGraph::set_canonicalization_mode(CanonicalizationMode mode) {
  canonicalization_mode_ = mode;
}

CanonicalizationMode StateClassReachabilityGraph::get_canonicalization_mode() const {
  return canonicalization_mode_;
}

void StateClassReachabilityGraph::set_pruning_enabled(bool enabled) {
  pruning_enabled_ = enabled;
}


StateClass StateClassReachabilityGraph::create_initial_state() {
  StateClass initial;
  initial.marking = ptpn_.get_marking();
  initial.cumulative_time = 0.0;

  const size_t num_transitions = ptpn_.num_transitions();
  initial.clocks.resize(num_transitions);
  initial.state_id = next_state_id_++;

  // 初始化调度状态
  recompute_enabled_sets(initial);

  log_state_class_details(initial, "[Initial] ");

  debug("[STATE] Create initial state:");
  spdlog::debug("  Marking: {}", format_marking(initial.marking));
  spdlog::debug("  Enabled: {}", format_transitions(initial.enabled));
  spdlog::debug("  Active: {}", format_transitions(initial.active));
  if (!initial.suspended.empty()) {
    spdlog::debug("  Suspended: {}", format_transitions(initial.suspended));
  }

  return initial;
}

// ===================================================================
// 辅助方法
// ===================================================================

bool StateClassReachabilityGraph::is_transition_enabled(
    const StateClass& state, size_t trans_idx) const {
  auto& stats = const_cast<Statistics&>(stats_);
  stats.transition_enabled_checks++;
  return petri::PTPN::is_enabled(state.marking, ptpn_, trans_idx);
}

std::vector<size_t> StateClassReachabilityGraph::collect_enabled_transitions(
    const StateClass& state) const {
  std::vector<size_t> enabled;
  const size_t num_transitions = ptpn_.num_transitions();
  enabled.reserve(num_transitions);
  for (size_t t = 0; t < num_transitions; ++t) {
    if (is_transition_enabled(state, t)) {
      enabled.push_back(t);
    }
  }
  return enabled;
}

std::pair<int, int> StateClassReachabilityGraph::get_transition_time_bounds(
    const StateClass& state, size_t trans_idx) const {
  const auto& transition = ptpn_.get_transition(trans_idx);
  int earliest = transition.time_interval.earliest;
  int latest = transition.time_interval.latest;

  return {earliest, latest};
}

std::vector<size_t> StateClassReachabilityGraph::select_per_core(
    const std::set<size_t>& enabled) const {
  std::set<size_t> result = SchedulingAlgorithms::select_active_per_core(enabled, ptpn_);
  return std::vector<size_t>(result.begin(), result.end());
}

void StateClassReachabilityGraph::apply_preemption(
    const std::vector<size_t>& chosen, StateClass& state) const {
  // 使用新的调度算法处理抢占
  std::set<size_t> active = SchedulingAlgorithms::select_active_per_core(
      state.enabled, ptpn_);
  std::set<size_t> suspended = SchedulingAlgorithms::compute_suspended(
      state.enabled, active, ptpn_);
  state.active = active;
  state.suspended = suspended;

  // 更新时钟状态
  for (size_t t : state.active) {
    if (t < state.clocks.size()) {
      state.clocks[t].state = ClockState::ACTIVE;
    }
  }
  for (size_t t : state.suspended) {
    if (t < state.clocks.size()) {
      state.clocks[t].state = ClockState::SUSPENDED;
    }
  }

  sync_zone_activity(state);
}

std::set<size_t> StateClassReachabilityGraph::compute_effective_enabled(
    const std::vector<size_t>& raw_enabled) const {
  std::set<size_t> raw_set(raw_enabled.begin(), raw_enabled.end());
  return SchedulingAlgorithms::select_active_per_core(raw_set, ptpn_);
}

std::set<size_t> StateClassReachabilityGraph::compute_suspended_transitions(
    const std::vector<size_t>& raw_enabled,
    const std::set<size_t>& effective_enabled) const {
  std::set<size_t> raw_set(raw_enabled.begin(), raw_enabled.end());
  return SchedulingAlgorithms::compute_suspended(raw_set, effective_enabled, ptpn_);
}

bool StateClassReachabilityGraph::maximal_time_elapse(StateClass& state, double& dt) const {
  dt = advance_time(state);
  return dt > 0;
}

std::tuple<bool, StateClass, double> StateClassReachabilityGraph::fire_with_dbm(
    size_t trans_idx, const StateClass& from_state) const {
  if (trans_idx >= from_state.clocks.size()) {
    return {false, StateClass(), 0.0};
  }

  StateClass to = from_state.copy();
  const auto& trans = ptpn_.get_transition(trans_idx);
  const int alpha = trans.time_interval.earliest;
  const int beta = latest_bound_for_transition(trans);

  if (to.has_zone_clock_for_transition(trans_idx) && to.zone.size() > 0) {
    const size_t clock_idx =
        static_cast<size_t>(to.clock_index_for_transition(trans_idx));
    to.zone = to.zone.restrict_clock(clock_idx, alpha, beta);
    if (to.zone.size() == 0) {
      return {false, StateClass(), 0.0};
    }
    to.sync_clocks_from_zone();
  }

  const int fire_delay = compute_firing_time(to, trans_idx);
  if (fire_delay < 0) {
    return {false, StateClass(), 0.0};
  }

  if (!elapse_active_clocks(to, fire_delay)) {
    return {false, StateClass(), 0.0};
  }

  to.marking = petri::PTPN::fire(to.marking, ptpn_, trans_idx);
  if (to.marking.empty()) {
    spdlog::debug("  {}: marking empty after fire",
                  format_transitions({trans_idx}, false));
    return {false, StateClass(), 0.0};
  }

  recompute_enabled_sets_from_marking(to.marking, to, {trans_idx});

  spdlog::debug("  {}: fired successfully after delay {}, new cumulative={}",
                format_transitions({trans_idx}, false), fire_delay,
                to.cumulative_time);

  return {true, to, static_cast<double>(fire_delay)};
}

void StateClassReachabilityGraph::compute_enabled_and_clocks(StateClass& state) {
  recompute_enabled_sets(state);
}

bool StateClassReachabilityGraph::is_suspended(
    size_t trans_idx, const std::vector<size_t>& enabled) const {
  const auto& trans = ptpn_.get_transition(trans_idx);
  if (!trans.suspendable) return false;

  for (size_t other_t : enabled) {
    if (other_t == trans_idx) continue;
    const auto& other_trans = ptpn_.get_transition(other_t);
    if (other_trans.core == trans.core && other_trans.suspendable &&
        other_trans.priority > trans.priority) {
      return true;
    }
  }
  return false;
}

void StateClassReachabilityGraph::recompute_suspension(StateClass& state) const {
  recompute_enabled_sets(state);
}

// =======================================================================
// 持久化
// =======================================================================

SCVertex StateClassReachabilityGraph::find_or_add_vertex(const StateClass& state) {
  auto key = make_state_key(state);
  auto it = state_to_vertex_.find(key);
  if (it != state_to_vertex_.end()) {
    return it->second;
  }

  StateClass new_state = state;
  new_state.state_id = next_state_id_++;
  SCVertex v = boost::add_vertex(new_state, graph_);
  state_to_vertex_.emplace(make_state_key(new_state), v);
  return v;
}

bool StateClassReachabilityGraph::save_to_dot(
    const std::string& file_path) const {
  try {
    std::ofstream out(file_path);
    if (!out.is_open()) {
      return false;
    }

    out << "digraph StateClassGraph {\n";
    out << "  rankdir=LR;\n";
    out << "  node [shape=box];\n\n";

    typedef boost::graph_traits<SCGraph>::vertex_iterator SCVIterator;
    SCVIterator vi, vi_end;
    for (std::tie(vi, vi_end) = boost::vertices(graph_); vi != vi_end; ++vi) {
      const StateClass& state = boost::get(boost::vertex_name, graph_, *vi);
      out << "  s" << state.state_id << " [label=\"";
      out << "State " << state.state_id << "\\n";
      out << "M: [";
      for (size_t i = 0; i < state.marking.size(); ++i) {
        if (i > 0) out << ", ";
        out << state.marking[i];
      }
      out << "]\\n";
      out << "Active: " << state.active.size() << "\\n";
      out << "Time: " << std::fixed << std::setprecision(2) << state.cumulative_time;
      out << "\"];\n";
    }

    out << "\n";

    typedef boost::graph_traits<SCGraph>::edge_iterator SCEIterator;
    SCEIterator ei, ei_end;
    for (std::tie(ei, ei_end) = boost::edges(graph_); ei != ei_end; ++ei) {
      SCVertex src = boost::source(*ei, graph_);
      SCVertex tgt = boost::target(*ei, graph_);
      const TransitionEdge& edge = boost::get(boost::edge_name, graph_, *ei);
      const StateClass& src_state = boost::get(boost::vertex_name, graph_, src);
      const StateClass& tgt_state = boost::get(boost::vertex_name, graph_, tgt);

      out << "  s" << src_state.state_id << " -> s" << tgt_state.state_id;
      out << " [label=\"T" << edge.transition_id << "\\n@" << edge.firing_time
          << "\"];\n";
    }

    out << "}\n";
    out.close();

    return true;
  } catch (...) {
    return false;
  }
}

bool StateClassReachabilityGraph::save_to_json(
    const std::string& file_path) const {
  try {
    std::ofstream out(file_path);
    if (!out.is_open()) {
      return false;
    }

    out << "{\n";
    out << "  \"states\": [\n";

    typedef boost::graph_traits<SCGraph>::vertex_iterator SCVIterator;
    SCVIterator vi, vi_end;
    bool first_state = true;
    for (std::tie(vi, vi_end) = boost::vertices(graph_); vi != vi_end; ++vi) {
      const StateClass& state = boost::get(boost::vertex_name, graph_, *vi);
      if (!first_state) out << ",\n";
      first_state = false;

      out << "    {\n";
      out << "      \"id\": " << state.state_id << ",\n";
      out << "      \"marking\": [";
      for (size_t i = 0; i < state.marking.size(); ++i) {
        if (i > 0) out << ", ";
        out << state.marking[i];
      }
      out << "],\n";
      out << "      \"active_count\": " << state.active.size() << ",\n";
      out << "      \"cumulative_time\": " << std::fixed << std::setprecision(2)
          << state.cumulative_time << "\n";
      out << "    }";
    }

    out << "\n  ],\n";
    out << "  \"transitions\": [\n";

    typedef boost::graph_traits<SCGraph>::edge_iterator SCEIterator;
    SCEIterator ei, ei_end;
    bool first_trans = true;
    for (std::tie(ei, ei_end) = boost::edges(graph_); ei != ei_end; ++ei) {
      SCVertex src = boost::source(*ei, graph_);
      SCVertex tgt = boost::target(*ei, graph_);
      const TransitionEdge& edge = boost::get(boost::edge_name, graph_, *ei);
      const StateClass& src_state = boost::get(boost::vertex_name, graph_, src);
      const StateClass& tgt_state = boost::get(boost::vertex_name, graph_, tgt);

      if (!first_trans) out << ",\n";
      first_trans = false;

      out << "    {\n";
      out << "      \"source\": " << src_state.state_id << ",\n";
      out << "      \"target\": " << tgt_state.state_id << ",\n";
      out << "      \"transition_id\": " << edge.transition_id << ",\n";
      out << "      \"firing_time\": " << std::fixed << std::setprecision(2)
          << edge.firing_time << "\n";
      out << "    }";
    }

    out << "\n  ],\n";
    out << "  \"statistics\": {\n";
    out << "    \"total_states\": " << stats_.total_states << ",\n";
    out << "    \"total_transitions\": " << stats_.total_transitions << ",\n";
    out << "    \"enabled_transitions_count\": "
        << stats_.enabled_transitions_count << ",\n";
    out << "    \"pruned_states_count\": " << stats_.pruned_states_count
        << ",\n";
    out << "    \"dedup_hits_count\": " << stats_.dedup_hits_count << ",\n";
    out << "    \"dedup_misses_count\": " << stats_.dedup_misses_count << ",\n";
    out << "    \"transition_enabled_checks\": "
        << stats_.transition_enabled_checks << ",\n";
    out << "    \"truncated\": " << (stats_.truncated ? "true" : "false")
        << "\n";
    out << "  }\n";
    out << "}\n";

    out.close();
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace state_class