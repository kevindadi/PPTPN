#include "analysis/graph.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <spdlog/spdlog.h>
#include <sstream>
#include <vector>

#include "json/json.h"
#include "scheduling.h"
#include "tdg/tdg.h"
#include "tdg2pn/tdg2pn.h"

namespace state_class {

namespace {
constexpr int kControlTransitionPriority = 0;

int effective_earliest_for_transition(const petri::Transition& trans) {
  return trans.time_interval.left_open ? trans.time_interval.earliest + 1
                                       : trans.time_interval.earliest;
}

int effective_latest_for_transition(const petri::Transition& trans) {
  if (trans.time_interval.latest == petri::INF) {
    return INF_TIME;
  }
  return trans.time_interval.right_open ? trans.time_interval.latest - 1
                                        : trans.time_interval.latest;
}

std::pair<int, int> effective_time_bounds_for_transition(
    const petri::Transition& trans) {
  return {effective_earliest_for_transition(trans),
          effective_latest_for_transition(trans)};
}

std::string format_int_vector(const std::vector<int>& values);
std::string format_transition_vector(const std::vector<size_t>& trans_indices,
                                     const petri::PTPN& ptpn,
                                     bool detailed);
std::string escape_dot_string(const std::string& value);
std::string escape_json_string(const std::string& value);
std::string join_lines_for_dot(const std::string& value);
std::pair<int, int> current_clock_bounds(const ReachabilityState& state, size_t t);
void sync_zone_activity(ReachabilityState& state);

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

bool elapse_active_clocks(ReachabilityState& state, int delay) {
  if (delay < 0) {
    return false;
  }

  for (size_t t : state.scheduling.active) {
    if (t >= state.timing.clocks.size()) {
      continue;
    }

    const auto [clock_lower, clock_upper] = current_clock_bounds(state, t);
    if (clock_upper != INF_TIME && clock_lower + delay > clock_upper) {
      return false;
    }
  }

  if (delay > 0 && state.timing.zone.size() > 0) {
    DBM next_zone = state.timing.zone;

    // Advance H clocks for active transitions
    for (size_t t : state.scheduling.active) {
      if (!state.has_zone_clock_for_transition(t)) {
        continue;
      }

      const size_t clock_idx =
          static_cast<size_t>(state.clock_index_for_transition(t));
      const int current_lower = state.timing.zone.get_constraint(0, clock_idx);
      int updated_lower = current_lower;
      safe_add_graph_bound(current_lower, -delay, updated_lower);
      next_zone.set_constraint(0, clock_idx, updated_lower);
    }

    // Advance W clocks for suspended transitions (differential elapse)
    for (size_t t : state.scheduling.suspended) {
      if (!state.has_zone_w_clock_for_transition(t)) {
        continue;
      }

      const size_t w_idx = static_cast<size_t>(state.w_clock_index_for_transition(t));
      const int current_w_lower = state.timing.zone.get_constraint(0, w_idx);
      int updated_w_lower = current_w_lower;
      safe_add_graph_bound(current_w_lower, -delay, updated_w_lower);
      next_zone.set_constraint(0, w_idx, updated_w_lower);
    }

    for (size_t i = 1; i < state.timing.zone.size(); ++i) {
      const size_t ti = state.transition_for_clock(i);
      int age_i = 0;
      if (state.scheduling.active.count(ti)) {
        age_i = delay;
      } else if (state.scheduling.suspended.count(ti)) {
        const auto var = state.variable_for_clock(i);
        if (var.kind == TimedVariableKind::W) {
          age_i = delay;  // W clocks also advance with time
        }
      }

      for (size_t j = 1; j < state.timing.zone.size(); ++j) {
        const size_t tj = state.transition_for_clock(j);
        int age_j = 0;
        if (state.scheduling.active.count(tj)) {
          age_j = delay;
        } else if (state.scheduling.suspended.count(tj)) {
          const auto var = state.variable_for_clock(j);
          if (var.kind == TimedVariableKind::W) {
            age_j = delay;
          }
        }
        const int delta_ij = age_i - age_j;
        if (delta_ij == 0) {
          continue;
        }

        const int current_bound = state.timing.zone.get_constraint(i, j);
        int updated_bound = current_bound;
        safe_add_graph_bound(current_bound, delta_ij, updated_bound);
        next_zone.set_constraint(i, j, updated_bound);
      }
    }

    next_zone.minimize();
    state.timing.zone = std::move(next_zone);
    state.sync_clocks_from_zone();
  } else {
    for (size_t t : state.scheduling.active) {
      if (t >= state.timing.clocks.size()) {
        continue;
      }
      state.timing.clocks[t].lower_bound += delay;
    }
  }

  state.metadata.cumulative_time += delay;
  return true;
}


// State key helper using new clocks/active/suspended structure
StateKey make_state_key(const ReachabilityState& state) {
  StateKey key;
  key.marking = state.marking;
  key.transition_to_h_clock = state.timing.transition_to_h_clock;
  key.transition_to_w_clock = state.timing.transition_to_w_clock;
  key.clock_to_variable = state.timing.clock_to_variable;
  key.transition_has_w_domain = state.timing.transition_has_w_domain;
  key.w_lower_bounds = state.timing.w_lower_bounds;
  key.zone_matrix = state.timing.zone.raw_matrix();
  key.frozen_clocks = state.timing.zone.frozen_clocks();
  key.scheduling.enabled = state.scheduling.enabled;
  key.scheduling.active = state.scheduling.active;
  key.scheduling.suspended = state.scheduling.suspended;
  return key;
}

std::pair<int, int> current_clock_bounds(const ReachabilityState& state, size_t t) {
  if (t < state.timing.clocks.size() && state.has_zone_clock_for_transition(t) &&
      state.timing.zone.size() > 0) {
    const size_t clock_idx =
        static_cast<size_t>(state.clock_index_for_transition(t));
    return {-state.timing.zone.get_constraint(0, clock_idx),
            state.timing.zone.get_constraint(clock_idx, 0)};
  }

  if (t < state.timing.clocks.size()) {
    return {state.timing.clocks[t].lower_bound, state.timing.clocks[t].upper_bound};
  }

  return {0, INF_TIME};
}

bool is_uninitialized_clock(const TransitionClock& clock) {
  return clock.state == ClockState::UNACTIVE && clock.lower_bound == 0 &&
         clock.upper_bound == INF_TIME;
}

void rebuild_zone_preserving_constraints(
    ReachabilityState& state, const DBM& previous_zone,
    const std::vector<int>& previous_transition_to_h_clock,
    const std::vector<int>& previous_transition_to_w_clock,
    const std::vector<bool>& previous_transition_has_w_domain,
    const std::set<size_t>& previously_enabled,
    const std::set<size_t>& reset_transitions) {
  state.timing.transition_to_h_clock.assign(state.timing.clocks.size(), -1);
  state.timing.transition_to_w_clock.assign(state.timing.clocks.size(), -1);
  state.timing.clock_to_variable.clear();
  state.timing.clock_to_variable.push_back({TimedVariableKind::ZERO, INVALID_TRANSITION_ID});
  state.timing.transition_has_w_domain.resize(state.timing.clocks.size(), false);

  DBM next_zone(1);
  for (size_t t : state.scheduling.enabled) {
    if (t >= state.timing.clocks.size()) {
      continue;
    }

    const size_t h_idx = next_zone.add_clock();
    state.timing.transition_to_h_clock[t] = static_cast<int>(h_idx);
    state.timing.clock_to_variable.push_back({TimedVariableKind::H, t});

    if (state.timing.transition_has_w_domain[t]) {
      const size_t w_idx = next_zone.add_clock();
      state.timing.transition_to_w_clock[t] = static_cast<int>(w_idx);
      state.timing.clock_to_variable.push_back({TimedVariableKind::W, t});
    }
  }

  const auto has_previous_h_clock = [&](size_t t) {
    return previously_enabled.count(t) && !reset_transitions.count(t) &&
           t < previous_transition_to_h_clock.size() &&
           previous_transition_to_h_clock[t] > 0 &&
           static_cast<size_t>(previous_transition_to_h_clock[t]) < previous_zone.size();
  };
  const auto has_previous_w_clock = [&](size_t t) {
    return t < previous_transition_has_w_domain.size() && previous_transition_has_w_domain[t] &&
           t < previous_transition_to_w_clock.size() &&
           previous_transition_to_w_clock[t] > 0 &&
           static_cast<size_t>(previous_transition_to_w_clock[t]) < previous_zone.size();
  };

  for (size_t t : state.scheduling.enabled) {
    if (t >= state.timing.transition_to_h_clock.size() || state.timing.transition_to_h_clock[t] <= 0) {
      continue;
    }

    const size_t new_h = static_cast<size_t>(state.timing.transition_to_h_clock[t]);
    if (has_previous_h_clock(t)) {
      const size_t old_h = static_cast<size_t>(previous_transition_to_h_clock[t]);
      next_zone.set_constraint(0, new_h, previous_zone.get_constraint(0, old_h));
      next_zone.set_constraint(new_h, 0, previous_zone.get_constraint(old_h, 0));
    } else {
      const auto& clock = state.timing.clocks[t];
      next_zone.set_constraint(0, new_h, -clock.lower_bound);
      next_zone.set_constraint(new_h, 0, clock.upper_bound);
    }

    if (state.timing.transition_has_w_domain[t] && state.timing.transition_to_w_clock[t] > 0) {
      const size_t new_w = static_cast<size_t>(state.timing.transition_to_w_clock[t]);
      if (state.scheduling.active.count(t)) {
        next_zone.set_constraint(0, new_w, 0);
        next_zone.set_constraint(new_w, 0, INF_TIME);
      } else if (has_previous_w_clock(t)) {
        const size_t old_w = static_cast<size_t>(previous_transition_to_w_clock[t]);
        next_zone.set_constraint(0, new_w, previous_zone.get_constraint(0, old_w));
        next_zone.set_constraint(new_w, 0, previous_zone.get_constraint(old_w, 0));
      } else {
        next_zone.set_constraint(0, new_w, -state.w_lower_bound(t));
        next_zone.set_constraint(new_w, 0, INF_TIME);
      }
    }
  }

  for (size_t ti : state.scheduling.enabled) {
    if (has_previous_h_clock(ti)) {
      const size_t new_hi = static_cast<size_t>(state.timing.transition_to_h_clock[ti]);
      const size_t old_hi = static_cast<size_t>(previous_transition_to_h_clock[ti]);
      for (size_t tj : state.scheduling.enabled) {
        if (!has_previous_h_clock(tj)) {
          continue;
        }

        const size_t new_hj = static_cast<size_t>(state.timing.transition_to_h_clock[tj]);
        const size_t old_hj = static_cast<size_t>(previous_transition_to_h_clock[tj]);
        next_zone.set_constraint(new_hi, new_hj,
                                 previous_zone.get_constraint(old_hi, old_hj));
      }
    }

    if (state.timing.transition_has_w_domain[ti] && has_previous_w_clock(ti) &&
        state.timing.transition_to_w_clock[ti] > 0) {
      const size_t new_wi = static_cast<size_t>(state.timing.transition_to_w_clock[ti]);
      const size_t old_wi = static_cast<size_t>(previous_transition_to_w_clock[ti]);

      for (size_t tj : state.scheduling.enabled) {
        if (has_previous_h_clock(tj)) {
          const size_t new_hj = static_cast<size_t>(state.timing.transition_to_h_clock[tj]);
          const size_t old_hj = static_cast<size_t>(previous_transition_to_h_clock[tj]);
          next_zone.set_constraint(new_wi, new_hj,
                                   previous_zone.get_constraint(old_wi, old_hj));
          next_zone.set_constraint(new_hj, new_wi,
                                   previous_zone.get_constraint(old_hj, old_wi));
        }
        if (state.timing.transition_has_w_domain[tj] && has_previous_w_clock(tj) &&
            state.timing.transition_to_w_clock[tj] > 0) {
          const size_t new_wj = static_cast<size_t>(state.timing.transition_to_w_clock[tj]);
          const size_t old_wj = static_cast<size_t>(previous_transition_to_w_clock[tj]);
          next_zone.set_constraint(new_wi, new_wj,
                                   previous_zone.get_constraint(old_wi, old_wj));
        }
      }
    }
  }

  next_zone.minimize();
  state.timing.zone = std::move(next_zone);
}

void recompute_enabled_sets_from_marking_impl(
    const petri::PTPN& ptpn, const std::vector<int>& marking,
    ReachabilityState& state,
    const std::set<size_t>& force_reset_transitions) {
  const std::vector<int> previous_marking = state.marking;
  state.marking = marking;

  std::vector<size_t> raw_enabled;
  const size_t num_transitions = ptpn.num_transitions();
  const std::set<size_t> previously_enabled = state.scheduling.enabled;
  const DBM previous_zone = state.timing.zone;
  const std::vector<int> previous_transition_to_h_clock =
      state.timing.transition_to_h_clock;
  const std::vector<int> previous_transition_to_w_clock =
      state.timing.transition_to_w_clock;
  const std::vector<bool> previous_transition_has_w_domain =
      state.timing.transition_has_w_domain;

  for (size_t t = 0; t < num_transitions; ++t) {
    if (petri::PTPN::is_enabled(marking, ptpn, t)) {
      raw_enabled.push_back(t);
    }
  }

  const std::set<size_t> raw_set(raw_enabled.begin(), raw_enabled.end());

  if (state.timing.clocks.size() < num_transitions) {
    state.timing.clocks.resize(num_transitions);
  }

  std::set<size_t> reset_transitions = force_reset_transitions;
  std::set<size_t> preserved_transitions;
  for (size_t t = 0; t < num_transitions; ++t) {
    if (!raw_set.count(t)) {
      state.timing.clocks[t] = TransitionClock();
      if (t < state.timing.w_lower_bounds.size()) {
        state.timing.w_lower_bounds[t] = 0;
      }
      continue;
    }

    const bool needs_initialization =
        !previously_enabled.count(t) ||
        is_uninitialized_clock(state.timing.clocks[t]) ||
        force_reset_transitions.count(t);

    if (needs_initialization) {
      const auto& trans = ptpn.get_transition(t);
      state.timing.clocks[t].lower_bound = 0;
      state.timing.clocks[t].upper_bound = effective_latest_for_transition(trans);
      state.timing.clocks[t].state = ClockState::UNACTIVE;
      reset_transitions.insert(t);
    } else {
      preserved_transitions.insert(t);
    }
  }

  state.scheduling.enabled = raw_set;
  state.scheduling.active = state.scheduling.enabled;
  state.scheduling.suspended.clear();

  state.timing.transition_has_w_domain.assign(num_transitions, false);
  if (state.timing.w_lower_bounds.size() < num_transitions) {
    state.timing.w_lower_bounds.resize(num_transitions, 0);
  }

  for (size_t t = 0; t < state.timing.clocks.size(); ++t) {
    if (!state.scheduling.enabled.count(t)) {
      state.timing.clocks[t].state = ClockState::UNACTIVE;
      state.timing.w_lower_bounds[t] = 0;
      continue;
    }

    const auto& trans = ptpn.get_transition(t);
    state.timing.transition_has_w_domain[t] = trans.suspendable;
    state.timing.clocks[t].state = ClockState::ACTIVE;
    state.timing.w_lower_bounds[t] = 0;
  }

  rebuild_zone_preserving_constraints(state, previous_zone,
                                      previous_transition_to_h_clock,
                                      previous_transition_to_w_clock,
                                      previous_transition_has_w_domain,
                                      previously_enabled, reset_transitions);
  sync_zone_activity(state);

  spdlog::debug(
      "[SCHED] recompute_enabled_sets_from_marking: previous_marking={}, new_marking={}, raw_enabled={}, reset={}, preserved={}",
      format_int_vector(previous_marking),
      format_int_vector(state.marking),
      format_transition_vector(raw_enabled, ptpn, true),
      format_transition_vector(
          std::vector<size_t>(reset_transitions.begin(), reset_transitions.end()), ptpn,
          true),
      format_transition_vector(
          std::vector<size_t>(preserved_transitions.begin(),
                              preserved_transitions.end()),
          ptpn, true));
  spdlog::debug("[SCHED] previously_enabled={}",
                format_transition_vector(
                    std::vector<size_t>(previously_enabled.begin(),
                                        previously_enabled.end()),
                    ptpn, true));
  spdlog::debug("[SCHED] enabled={}, active={}, suspended={}",
                format_transition_vector(
                    std::vector<size_t>(state.scheduling.enabled.begin(),
                                        state.scheduling.enabled.end()),
                    ptpn, true),
                format_transition_vector(
                    std::vector<size_t>(state.scheduling.active.begin(),
                                        state.scheduling.active.end()),
                    ptpn, true),
                format_transition_vector(
                    std::vector<size_t>(state.scheduling.suspended.begin(),
                                        state.scheduling.suspended.end()),
                    ptpn, true));
  spdlog::debug("[DBM] previous zone:\n{}", previous_zone.to_string());
}

void sync_zone_activity(ReachabilityState& state) {
  if (state.timing.zone.size() == 0) {
    return;
  }

  for (size_t t : state.scheduling.enabled) {
    if (state.has_zone_clock_for_transition(t)) {
      const size_t clock_idx =
          static_cast<size_t>(state.clock_index_for_transition(t));
      if (state.scheduling.active.count(t)) {
        state.timing.zone.unfreeze_clock(clock_idx);
      } else {
        state.timing.zone.freeze_clock(clock_idx);
      }
    }

    // W clocks always advance (never frozen)
    if (state.has_zone_w_clock_for_transition(t)) {
      const size_t w_idx = static_cast<size_t>(state.w_clock_index_for_transition(t));
      state.timing.zone.unfreeze_clock(w_idx);
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

std::string format_int_vector(const std::vector<int>& values) {
  std::string result = "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      result += ", ";
    }
    result += std::to_string(values[i]);
  }
  result += "]";
  return result;
}

std::string escape_dot_string(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

std::string escape_json_string(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

std::string join_lines_for_dot(const std::string& value) {
  std::string joined = value;
  size_t pos = 0;
  while ((pos = joined.find('\n', pos)) != std::string::npos) {
    joined.replace(pos, 1, "\\n");
    pos += 2;
  }
  return joined;
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

std::string format_transition_clock_summary(const ReachabilityState& state,
                                           const petri::PTPN& ptpn) {
  std::ostringstream oss;
  bool first = true;

  for (size_t t : state.scheduling.enabled) {
    if (t >= state.timing.clocks.size()) continue;

    if (!first) oss << "; ";
    first = false;

    const auto& trans = ptpn.get_transition(t);
    const auto& clock = state.timing.clocks[t];
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

std::string StateClassReachabilityGraph::format_transition_label(
    size_t transition_id) const {
  if (transition_id >= ptpn_.num_transitions()) {
    return "T" + std::to_string(transition_id);
  }

  const auto& trans = ptpn_.get_transition(transition_id);
  std::string result = "T" + std::to_string(transition_id) + "(" + trans.name;
  result += ", priority=" + std::to_string(trans.priority);
  result += ", core=" + std::to_string(trans.core);
  if (trans.suspendable) {
    result += ", suspendable";
  }
  result += ")";
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
    result += detailed ? format_transition_label(t) : "T" + std::to_string(t);
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

std::string StateClassReachabilityGraph::format_named_dbm(
    const ReachabilityState& state) const {
  if (state.timing.zone.size() == 0) {
    return "DBM(empty)";
  }

  std::vector<std::string> labels(state.timing.zone.size());
  labels[0] = "x0";
  size_t cell_width = 6;
  for (size_t clock_idx = 1; clock_idx < state.timing.zone.size(); ++clock_idx) {
    const TimedVariableRef variable = state.variable_for_clock(clock_idx);
    if (variable.kind == TimedVariableKind::ZERO ||
        variable.transition_id == std::numeric_limits<size_t>::max()) {
      labels[clock_idx] = "x" + std::to_string(clock_idx);
    } else {
      const std::string prefix = variable.kind == TimedVariableKind::W ? "w" : "h";
      labels[clock_idx] = prefix + "(" + format_transition_label(variable.transition_id) + ")";
      if (state.timing.zone.is_frozen(clock_idx)) {
        labels[clock_idx] += "[frozen]";
      }
    }
    cell_width = std::max(cell_width, labels[clock_idx].size() + 2);
  }
  cell_width = std::max(cell_width, std::string("Frozen clocks").size() + 2);

  std::ostringstream oss;
  oss << "DBM(size=" << state.timing.zone.size() << ")\n";
  oss << std::left << std::setw(static_cast<int>(cell_width)) << " " << "|";
  for (const auto& label : labels) {
    oss << " " << std::left << std::setw(static_cast<int>(cell_width)) << label << "|";
  }
  oss << "\n";

  for (size_t i = 0; i < state.timing.zone.size(); ++i) {
    oss << std::left << std::setw(static_cast<int>(cell_width)) << labels[i] << "|";
    for (size_t j = 0; j < state.timing.zone.size(); ++j) {
      const int value = state.timing.zone.get_constraint(i, j);
      const std::string rendered =
          value == INF_TIME ? std::string("inf") : std::to_string(value);
      oss << " " << std::left << std::setw(static_cast<int>(cell_width)) << rendered
          << "|";
    }
    oss << "\n";
  }

  oss << "Frozen clocks: ";
  if (state.timing.zone.frozen_clocks().empty()) {
    oss << "(none)";
  } else {
    bool first = true;
    for (size_t clock_idx : state.timing.zone.frozen_clocks()) {
      if (!first) {
        oss << ", ";
      }
      first = false;
      if (clock_idx < labels.size()) {
        oss << labels[clock_idx];
      } else {
        oss << "x" << clock_idx;
      }
    }
  }

  return oss.str();
}


std::string StateClassReachabilityGraph::format_state_dump(
    const ReachabilityState& state) const {
  std::ostringstream oss;
  oss << "State " << state.metadata.state_id << "\n";
  oss << "  Cumulative time: " << state.metadata.cumulative_time << "\n";
  oss << "  Places: " << format_places(state.marking) << "\n";
  oss << "  Enabled: " << format_transitions(state.scheduling.enabled) << "\n";
  oss << "  Active: " << format_transitions(state.scheduling.active) << "\n";
  oss << "  Suspended: " << format_transitions(state.scheduling.suspended) << "\n";
  oss << "  Clocks: " << format_transition_clock_summary(state, ptpn_) << "\n";
  oss << "  Zone:\n" << format_named_dbm(state);
  return oss.str();
}

void StateClassReachabilityGraph::log_state_class_details(
    const ReachabilityState& state, const std::string& prefix) const {
  spdlog::debug("{}{}", prefix, format_state_dump(state));
}

StateClassReachabilityGraph::StateClassReachabilityGraph(const petri::PTPN& ptpn)
    : ptpn_(ptpn), next_state_id_(0), pruning_enabled_(false) {}

size_t StateClassReachabilityGraph::build(size_t max_states) {
  stats_ = Statistics();
  graph_.clear();
  state_to_vertex_.clear();
  next_state_id_ = 0;

  ReachabilityState s0 = create_initial_state();
  s0 = canonicalize(s0, s0);

  SCVertex s0_vertex = find_or_add_vertex(s0);
  initial_vertex_ = s0_vertex;
  stats_.total_states++;

  std::vector<ReachabilityState> frontier{s0};
  size_t max_frontier_size = frontier.size();

  while (!frontier.empty()) {
    if (stats_.total_states >= max_states) {
      stats_.truncated = true;
      break;
    }

    std::vector<ReachabilityState> next_frontier;

    for (const auto& frontier_state : frontier) {
      ReachabilityState cur = frontier_state;
      SCVertex u = find_or_add_vertex(cur);
      const ReachabilityState& graph_cur = boost::get(boost::vertex_name, graph_, u);
      cur.metadata.state_id = graph_cur.metadata.state_id;

      log_state_class_details(cur,
                              "[STATE][Expand state " + std::to_string(cur.metadata.state_id) + "] ");

      const StateExpansionResult result = expand_state_candidates(cur);
      add_expansion_stats(stats_, result);

      for (const auto& candidate : result.candidates) {
        spdlog::debug("[STATE] State {} --{}@{}--> candidate",
                      cur.metadata.state_id, format_transition_label(candidate.transition_id),
                      candidate.edge.firing_time);
        log_state_class_details(candidate.state, "[STATE][Candidate] ");

        auto state_it = state_to_vertex_.find(make_state_key(candidate.state));
        SCVertex v;
        if (state_it != state_to_vertex_.end()) {
          v = state_it->second;
          const ReachabilityState& existing_state = boost::get(boost::vertex_name, graph_, v);
          stats_.dedup_hits_count++;
          spdlog::debug("[DEDUP] candidate merged into existing state {}",
                        existing_state.metadata.state_id);
          log_state_class_details(
              existing_state,
              "[DEDUP][Existing state " +
                  std::to_string(existing_state.metadata.state_id) + "] ");
        } else {
          if (stats_.total_states >= max_states) {
            stats_.truncated = true;
            break;
          }

          v = find_or_add_vertex(candidate.state);
          const ReachabilityState& new_graph_state = boost::get(boost::vertex_name, graph_, v);
          ReachabilityState queued_state = candidate.state;
          queued_state.metadata.state_id = new_graph_state.metadata.state_id;
          next_frontier.push_back(queued_state);
          max_frontier_size = std::max(max_frontier_size, next_frontier.size());
          stats_.total_states++;
          stats_.dedup_misses_count++;
          spdlog::debug("[STATE] new state {} added to graph/frontier",
                        new_graph_state.metadata.state_id);
          log_state_class_details(
              new_graph_state,
              "[STATE][New state " + std::to_string(new_graph_state.metadata.state_id) + "] ");
        }

        boost::add_edge(u, v, candidate.edge, graph_);
        stats_.total_transitions++;
      }

      spdlog::debug(
          "[STATE] State {}: chosen={}, fired={}, frontier size={}, total states={}",
          cur.metadata.state_id, result.chosen_count, result.fired_count,
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

int StateClassReachabilityGraph::compute_firing_time(const ReachabilityState& state,
                                                    size_t t) const {
  if (t >= state.timing.clocks.size()) {
    return -1;
  }

  if (!state.scheduling.enabled.count(t) || state.scheduling.suspended.count(t)) {
    return -1;
  }

  const auto& trans = ptpn_.get_transition(t);
  const auto [alpha, beta] = effective_time_bounds_for_transition(trans);
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
    const ReachabilityState& cur) {
  const size_t enabled_checks_before = stats_.transition_enabled_checks;

  StateExpansionResult result;

  ReachabilityState scheduled = cur.copy();
  recompute_enabled_sets(scheduled);
  log_state_class_details(scheduled, "[SCHED][Recomputed] ");

  if (pruning_enabled_ && scheduled.scheduling.active.empty()) {
    spdlog::debug("[SCHED][Prune] no active transitions after recompute");
    log_state_class_details(scheduled, "[SCHED][Prune state] ");
    result.pruned_states_count++;
    result.transition_enabled_checks +=
        stats_.transition_enabled_checks - enabled_checks_before;
    return result;
  }

  int tau_min = INF_TIME;
  for (size_t t : scheduled.scheduling.active) {
    const int tau = compute_firing_time(scheduled, t);
    if (tau >= 0) {
      tau_min = std::min(tau_min, tau);
    }
  }

  if (tau_min == INF_TIME) {
    spdlog::debug("[SCHED][Prune] no finite firing time for active set {}",
                  format_transitions(scheduled.scheduling.active));
    if (pruning_enabled_) {
      result.pruned_states_count++;
    }
    result.transition_enabled_checks +=
        stats_.transition_enabled_checks - enabled_checks_before;
    return result;
  }

  std::set<size_t> firable_now;
  for (size_t t : scheduled.scheduling.active) {
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
    spdlog::debug("[SCHED][Prune] empty schedulable set from firable_now={}",
                  format_transitions(firable_now));
    if (pruning_enabled_) {
      result.pruned_states_count++;
    }
    result.transition_enabled_checks +=
        stats_.transition_enabled_checks - enabled_checks_before;
    return result;
  }

  spdlog::debug("[SCHED] Earliest firing time: {}", tau_min);
  spdlog::debug("[SCHED] firable_now={}", format_transitions(firable_now));
  spdlog::debug("[SCHED] schedulable={}", format_transitions(schedulable));

  for (size_t chosen : schedulable) {
    auto [ok, nxt, tau] = fire_with_time(chosen, scheduled);
    if (!ok) {
      if (pruning_enabled_) {
        spdlog::debug("[FIRE] {} failed", format_transition_label(chosen));
        result.pruned_states_count++;
      } else {
        spdlog::debug("[FIRE] {} failed [pruning disabled]",
                      format_transition_label(chosen));
      }
      continue;
    }

    ReachabilityState canonical_nxt = canonicalize(nxt, nxt);
    log_state_class_details(canonical_nxt, "[FIRE][Canonical successor] ");
    result.candidates.push_back({canonical_nxt,
                                 TransitionEdge(static_cast<int>(chosen), tau), chosen});
    result.fired_count++;
  }

  result.transition_enabled_checks +=
      stats_.transition_enabled_checks - enabled_checks_before;
  return result;
}


double StateClassReachabilityGraph::advance_time(ReachabilityState& state) const {
  log_state_class_details(state, "[TIME][Before advance] ");

  int min_delay = INF_TIME;
  for (size_t t : state.scheduling.active) {
    const int tau = compute_firing_time(state, t);
    if (tau >= 0) {
      min_delay = std::min(min_delay, tau);
    }
  }

  if (min_delay == INF_TIME) {
    spdlog::debug("[TIME] advance_time skipped: no finite firing time");
    return 0.0;
  }

  if (!elapse_active_clocks(state, min_delay)) {
    spdlog::debug("[TIME] advance_time failed for dt={}", min_delay);
    return 0.0;
  }

  spdlog::debug("[TIME] advance_time: dt={}, new cumulative={}", min_delay,
                state.metadata.cumulative_time);
  log_state_class_details(state, "[TIME][After advance] ");

  return static_cast<double>(min_delay);
}

std::tuple<bool, ReachabilityState, double> StateClassReachabilityGraph::fire_with_time(
    size_t t, const ReachabilityState& from) const {
  return fire_with_dbm(t, from);
}

void StateClassReachabilityGraph::recompute_enabled_sets(ReachabilityState& state) const {
  recompute_enabled_sets_from_marking(state.marking, state);
}

void StateClassReachabilityGraph::recompute_enabled_sets_from_marking(
    const std::vector<int>& marking, ReachabilityState& state) const {
  recompute_enabled_sets_from_marking(marking, state, {});
}

void StateClassReachabilityGraph::recompute_enabled_sets_from_marking(
    const std::vector<int>& marking, ReachabilityState& state,
    const std::set<size_t>& force_reset_transitions) const {
  recompute_enabled_sets_from_marking_impl(ptpn_, marking, state,
                                           force_reset_transitions);
}



std::set<size_t> StateClassReachabilityGraph::select_active_per_core(
    const std::set<size_t>& enabled) const {
  return SchedulingAlgorithms::select_active_per_core(enabled, ptpn_);
}

std::set<size_t> StateClassReachabilityGraph::compute_suspended(
    const std::set<size_t>& enabled,
    const std::set<size_t>& active) const {
  return SchedulingAlgorithms::compute_suspended(enabled, active, ptpn_);
}



void StateClassReachabilityGraph::suspend_transition(size_t t, ReachabilityState& state) const {
  if (state.timing.clocks[t].state != ClockState::ACTIVE) {
    return;
  }

  spdlog::debug("[SCHED] suspend {}", format_transition_label(t));
  log_state_class_details(state, "[SCHED][Before suspend] ");

  state.scheduling.active.erase(t);
  state.scheduling.suspended.insert(t);
  state.timing.clocks[t].state = ClockState::SUSPENDED;
  sync_zone_activity(state);

  log_state_class_details(state, "[SCHED][After suspend] ");
}

void StateClassReachabilityGraph::restore_transition(size_t t, ReachabilityState& state) const {
  if (state.timing.clocks[t].state != ClockState::SUSPENDED) {
    return;
  }

  spdlog::debug("[SCHED] restore {}", format_transition_label(t));
  log_state_class_details(state, "[SCHED][Before restore] ");

  state.scheduling.suspended.erase(t);
  state.scheduling.active.insert(t);
  state.timing.clocks[t].state = ClockState::ACTIVE;
  if (state.has_zone_w_clock_for_transition(t)) {
    const size_t w_idx = static_cast<size_t>(state.w_clock_index_for_transition(t));
    state.timing.zone.reset_clock(w_idx);
  }
  sync_zone_activity(state);

  log_state_class_details(state, "[SCHED][After restore] ");
}



ReachabilityState StateClassReachabilityGraph::canonicalize(
    const ReachabilityState& a, const ReachabilityState& b) const {
  return ::state_class::canonicalize(a, b, canonicalization_mode_);
}

bool StateClassReachabilityGraph::are_equivalent(
    const ReachabilityState& a, const ReachabilityState& b) const {
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


ReachabilityState StateClassReachabilityGraph::create_initial_state() {
  ReachabilityState initial;
  initial.marking = ptpn_.get_marking();
  initial.metadata.cumulative_time = 0.0;

  const size_t num_transitions = ptpn_.num_transitions();
  initial.timing.clocks.resize(num_transitions);
  initial.metadata.state_id = next_state_id_++;

  recompute_enabled_sets(initial);

  log_state_class_details(initial, "[Initial] ");

  return initial;
}



std::vector<size_t> StateClassReachabilityGraph::select_per_core(
    const std::set<size_t>& enabled) const {
  std::set<size_t> result = SchedulingAlgorithms::select_active_per_core(enabled, ptpn_);
  return std::vector<size_t>(result.begin(), result.end());
}

void StateClassReachabilityGraph::apply_preemption(
    const std::vector<size_t>& chosen, ReachabilityState& state) const {
  std::set<size_t> active;
  for (size_t t : chosen) {
    if (state.scheduling.enabled.count(t)) {
      active.insert(t);
    }
  }

  state.scheduling.active = std::move(active);
  state.scheduling.suspended = SchedulingAlgorithms::compute_suspended(
      state.scheduling.enabled, state.scheduling.active, ptpn_);

  sync_zone_activity(state);
}

std::tuple<bool, ReachabilityState, double> StateClassReachabilityGraph::fire_with_dbm(
    size_t trans_idx, const ReachabilityState& from_state) const {
  if (trans_idx >= from_state.timing.clocks.size()) {
    return {false, ReachabilityState(), 0.0};
  }

  spdlog::debug("[FIRE] attempt {}", format_transition_label(trans_idx));
  log_state_class_details(from_state, "[FIRE][From state] ");

  ReachabilityState to = from_state.copy();
  const auto& trans = ptpn_.get_transition(trans_idx);
  const auto [alpha, beta] = effective_time_bounds_for_transition(trans);

  const int fire_delay = compute_firing_time(to, trans_idx);
  if (fire_delay < 0) {
    spdlog::debug("[FIRE] {} not firable from current state",
                  format_transition_label(trans_idx));
    return {false, ReachabilityState(), 0.0};
  }

  if (!elapse_active_clocks(to, fire_delay)) {
    spdlog::debug("[FIRE] {} failed during elapse_active_clocks(dt={})",
                  format_transition_label(trans_idx), fire_delay);
    return {false, ReachabilityState(), 0.0};
  }
  spdlog::debug("[DBM] after elapse_active_clocks(dt={}) for {}:\n{}", fire_delay,
                format_transition_label(trans_idx), format_named_dbm(to));

  if (to.has_zone_clock_for_transition(trans_idx) && to.timing.zone.size() > 0) {
    const size_t clock_idx =
        static_cast<size_t>(to.clock_index_for_transition(trans_idx));
    to.timing.zone = to.timing.zone.restrict_clock(clock_idx, alpha, beta);
    if (to.timing.zone.size() == 0) {
      spdlog::debug("[FIRE][DBM] {} restriction produced empty zone",
                    format_transition_label(trans_idx));
      return {false, ReachabilityState(), 0.0};
    }
    to.sync_clocks_from_zone();
    spdlog::debug("[DBM] after restrict_clock on {}:\n{}",
                  format_transition_label(trans_idx), format_named_dbm(to));
  }

  const std::vector<int> before_marking = to.marking;
  to.marking = petri::PTPN::fire(to.marking, ptpn_, trans_idx);
  if (to.marking.empty()) {
    spdlog::debug("[FIRE] {} produced empty marking, before={}, after={}",
                  format_transition_label(trans_idx), format_places(before_marking),
                  format_marking(to.marking));
    return {false, ReachabilityState(), 0.0};
  }

  spdlog::debug("[FIRE] {} marking: {} -> {}", format_transition_label(trans_idx),
                format_places(before_marking), format_places(to.marking));

  recompute_enabled_sets_from_marking(to.marking, to, {trans_idx});
  log_state_class_details(to, "[FIRE][Successor after recompute] ");

  spdlog::debug("[FIRE] {} fired successfully after delay {}, new cumulative={}",
                format_transition_label(trans_idx), fire_delay,
                to.metadata.cumulative_time);

  return {true, to, static_cast<double>(fire_delay)};
}


void StateClassReachabilityGraph::recompute_suspension(ReachabilityState& state) const {
  const std::set<size_t> previously_active = state.scheduling.active;

  recompute_enabled_sets(state);

  std::vector<size_t> chosen;
  chosen.reserve(previously_active.size());
  for (size_t t : previously_active) {
    if (state.scheduling.enabled.count(t)) {
      chosen.push_back(t);
    }
  }

  if (chosen.empty() && !state.scheduling.enabled.empty()) {
    const std::vector<size_t> fallback = select_per_core(state.scheduling.enabled);
    chosen.assign(fallback.begin(), fallback.end());
  }

  apply_preemption(chosen, state);
}



SCVertex StateClassReachabilityGraph::find_or_add_vertex(const ReachabilityState& state) {
  auto key = make_state_key(state);
  auto it = state_to_vertex_.find(key);
  if (it != state_to_vertex_.end()) {
    return it->second;
  }

  ReachabilityState new_state = state;
  new_state.metadata.state_id = next_state_id_++;
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
      const ReachabilityState& state = boost::get(boost::vertex_name, graph_, *vi);
      const std::string state_dump = format_state_dump(state);
      std::ostringstream summary;
      summary << "State " << state.metadata.state_id << "\n";
      summary << "t=" << state.metadata.cumulative_time << "\n";
      summary << "Places: " << format_places(state.marking) << "\n";
      summary << "Active: " << format_transitions(state.scheduling.active) << "\n";
      if (!state.scheduling.suspended.empty()) {
        summary << "Suspended: " << format_transitions(state.scheduling.suspended)
                << "\n";
      }
      summary << "Enabled=" << state.scheduling.enabled.size()
              << ", clocks=" << state.timing.zone.size();
      out << "  s" << state.metadata.state_id << " [label=\"";
      out << escape_dot_string(join_lines_for_dot(summary.str()));
      out << "\", tooltip=\"" << escape_dot_string(state_dump) << "\"];\n";
    }

    out << "\n";

    typedef boost::graph_traits<SCGraph>::edge_iterator SCEIterator;
    SCEIterator ei, ei_end;
    for (std::tie(ei, ei_end) = boost::edges(graph_); ei != ei_end; ++ei) {
      SCVertex src = boost::source(*ei, graph_);
      SCVertex tgt = boost::target(*ei, graph_);
      const TransitionEdge& edge = boost::get(boost::edge_name, graph_, *ei);
      const ReachabilityState& src_state = boost::get(boost::vertex_name, graph_, src);
      const ReachabilityState& tgt_state = boost::get(boost::vertex_name, graph_, tgt);
      const std::string edge_label =
          format_transition_label(edge.transition_id) + "\\n@" +
          std::to_string(edge.firing_time);

      out << "  s" << src_state.metadata.state_id << " -> s"
          << tgt_state.metadata.state_id;
      out << " [label=\"" << escape_dot_string(edge_label) << "\"];\n";
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
      const ReachabilityState& state = boost::get(boost::vertex_name, graph_, *vi);
      if (!first_state) out << ",\n";
      first_state = false;

      out << "    {\n";
      out << "      \"id\": " << state.metadata.state_id << ",\n";
      out << "      \"marking\": [";
      for (size_t i = 0; i < state.marking.size(); ++i) {
        if (i > 0) out << ", ";
        out << state.marking[i];
      }
      out << "],\n";
      out << "      \"marking_named\": \""
          << escape_json_string(format_places(state.marking)) << "\",\n";
      out << "      \"enabled\": \""
          << escape_json_string(format_transitions(state.scheduling.enabled)) << "\",\n";
      out << "      \"active\": \""
          << escape_json_string(format_transitions(state.scheduling.active)) << "\",\n";
      out << "      \"suspended\": \""
          << escape_json_string(format_transitions(state.scheduling.suspended)) << "\",\n";
      out << "      \"active_count\": " << state.scheduling.active.size() << ",\n";
      out << "      \"cumulative_time\": " << std::fixed << std::setprecision(2)
          << state.metadata.cumulative_time << ",\n";
      out << "      \"clock_summary\": \""
          << escape_json_string(format_transition_clock_summary(state, ptpn_))
          << "\",\n";
      out << "      \"zone\": \"" << escape_json_string(format_named_dbm(state))
          << "\",\n";
      out << "      \"dump\": \"" << escape_json_string(format_state_dump(state))
          << "\"\n";
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
      const ReachabilityState& src_state = boost::get(boost::vertex_name, graph_, src);
      const ReachabilityState& tgt_state = boost::get(boost::vertex_name, graph_, tgt);

      if (!first_trans) out << ",\n";
      first_trans = false;

      out << "    {\n";
      out << "      \"source\": " << src_state.metadata.state_id << ",\n";
      out << "      \"target\": " << tgt_state.metadata.state_id << ",\n";
      out << "      \"transition_id\": " << edge.transition_id << ",\n";
      out << "      \"transition_label\": \""
          << escape_json_string(format_transition_label(edge.transition_id)) << "\",\n";
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