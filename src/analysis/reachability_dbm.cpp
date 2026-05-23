#include "analysis/state.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace state_class {

namespace {

constexpr int kControlTransitionPriority = 0;

bool has_higher_priority(const petri::Transition& lhs,
                         const petri::Transition& rhs) {
  if (lhs.priority == kControlTransitionPriority && lhs.core < 0) {
    return false;
  }
  if (rhs.priority == kControlTransitionPriority && rhs.core < 0) {
    return true;
  }
  return lhs.priority > rhs.priority;
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

}  // namespace

std::pair<DBM, DBM> StateClassReachabilityGraph::time_advance(
    const StateClass& state) const {
  DBM z1_up = state.Z1;
  DBM z2_up = state.Z2;

  DBM invariants = get_invariants_for(state.marking);

  size_t num_clocks = z1_up.size();
  size_t num_transitions = ptpn_.num_transitions();

  size_t relaxed_count = 0;

  for (size_t i = 1; i < num_clocks && i <= num_transitions; ++i) {
    size_t trans_idx = i - 1;

    bool is_enabled = false;
    if (trans_idx < num_transitions) {
      is_enabled = petri::PTPN::is_enabled(state.marking, ptpn_, trans_idx);
    }

    if (!is_enabled) {
      continue;
    }

    if (z1_up.is_frozen(i)) {
      spdlog::debug("    Clock{}({}): frozen, skip", i,
                    format_transitions({trans_idx}, false));
      continue;
    }

    const auto& transition = ptpn_.get_transition(trans_idx);
    bool is_exact_time =
        (transition.time_interval.earliest == transition.time_interval.latest &&
         transition.time_interval.latest != petri::INF);

    if (is_exact_time && !transition.suspendable) {
      spdlog::debug("    Clock{}({}): exact time constraint", i,
                    format_transitions({trans_idx}, false));
      continue;
    }

    int current_upper = z1_up.get_constraint(i, 0);
    if (current_upper != INF_TIME) {
      z1_up.set_constraint(i, 0, INF_TIME);
      relaxed_count++;
      spdlog::debug("    Clock{}({}): relaxed {} -> INF", i,
                    format_transitions({trans_idx}, false), current_upper);
    }
  }

  spdlog::debug("  Time advance: relaxed {} clocks", relaxed_count);

  if (invariants.size() > 0 && z1_up.size() == invariants.size()) {
    z1_up = z1_up.intersection(invariants);
  }

  z1_up.minimize();
  z2_up.minimize();

  return {z1_up, z2_up};
}

bool StateClassReachabilityGraph::check_dbm_time_intersection(
    const DBM& z1, size_t trans_idx) const {
  size_t clock_idx = trans_idx + 1;
  if (clock_idx >= z1.size()) {
    return false;
  }

  DBM restricted = restrict_for_firing(z1, trans_idx);
  return !restricted.is_empty();
}

DBM StateClassReachabilityGraph::restrict_for_firing(const DBM& z,
                                                     size_t trans_idx) const {
  const auto& transition = ptpn_.get_transition(trans_idx);
  int alpha = transition.time_interval.earliest;
  int beta = transition.time_interval.latest == petri::INF
                 ? INF_TIME
                 : transition.time_interval.latest;

  return z.restrict_for_firing(trans_idx, alpha, beta);
}

double StateClassReachabilityGraph::compute_firing_time(
    const DBM& z1_up, size_t trans_idx) const {
  const auto& transition = ptpn_.get_transition(trans_idx);

  size_t clock_idx = trans_idx + 1;
  if (clock_idx >= z1_up.size()) {
    return static_cast<double>(transition.time_interval.earliest);
  }

  int alpha = transition.time_interval.earliest;
  int beta = transition.time_interval.latest == petri::INF
                 ? INF_TIME
                 : transition.time_interval.latest;

  int dbm_lower = -z1_up.get_constraint(0, clock_idx);
  int firing_time_int = std::max(alpha, dbm_lower);

  if (beta != INF_TIME && firing_time_int > beta) {
    firing_time_int = beta;
  }

  return static_cast<double>(firing_time_int);
}

StateClass StateClassReachabilityGraph::canonicalize(
    const StateClass& state) const {
  StateClass canonical = state;
  canonical.Z1.minimize();
  canonical.Z2.minimize();
  return canonical;
}

std::set<size_t> StateClassReachabilityGraph::compute_effective_enabled(
    const std::vector<size_t>& raw_enabled) const {
  std::set<size_t> raw_enabled_set(raw_enabled.begin(), raw_enabled.end());
  std::vector<size_t> chosen = select_per_core(raw_enabled_set);
  return std::set<size_t>(chosen.begin(), chosen.end());
}

std::set<size_t> StateClassReachabilityGraph::compute_suspended_transitions(
    const std::vector<size_t>& raw_enabled,
    const std::set<size_t>& effective_enabled) const {
  std::set<size_t> suspended;
  for (size_t t : raw_enabled) {
    if (effective_enabled.find(t) != effective_enabled.end()) {
      continue;
    }

    const auto& transition = ptpn_.get_transition(t);
    if (!transition.suspendable || transition.core < 0) {
      continue;
    }

    for (size_t chosen : effective_enabled) {
      const auto& chosen_transition = ptpn_.get_transition(chosen);
      if (chosen_transition.core < 0) {
        continue;
      }
      if (chosen_transition.core == transition.core &&
          has_higher_priority(chosen_transition, transition)) {
        suspended.insert(t);
        break;
      }
    }
  }
  return suspended;
}

void StateClassReachabilityGraph::reconcile_timing_domains(
    StateClass& state,
    const std::set<size_t>& previous_effective_enabled,
    const std::set<size_t>& previous_suspended) const {
  const size_t num_transitions = ptpn_.num_transitions();

  if (state.Z1.size() < num_transitions + 1) {
    state.Z1.resize(num_transitions + 1);
  }
  if (state.Z2.size() < num_transitions + 1) {
    state.Z2.resize(num_transitions + 1);
  }

  std::vector<bool> relevant_flags(num_transitions, false);
  for (size_t t : state.enabled) {
    relevant_flags[t] = true;
  }
  for (size_t t : state.suspended) {
    relevant_flags[t] = true;
  }

  for (size_t t = 0; t < num_transitions; ++t) {
    const size_t clock_idx = t + 1;
    const bool was_effective =
        previous_effective_enabled.find(t) != previous_effective_enabled.end();
    const bool was_suspended =
        previous_suspended.find(t) != previous_suspended.end();
    const bool is_effective = state.enabled.find(t) != state.enabled.end();
    const bool is_suspended_now = state.suspended.find(t) != state.suspended.end();
    const bool is_relevant = relevant_flags[t];

    if (!is_relevant) {
      if (clock_idx < state.Z1.size()) {
        state.Z1.forget_clock(clock_idx);
        state.Z1.unfreeze_clock(clock_idx);
      }
      if (clock_idx < state.Z2.size()) {
        state.Z2.forget_clock(clock_idx);
        state.Z2.unfreeze_clock(clock_idx);
      }
      continue;
    }

    const auto& transition = ptpn_.get_transition(t);
    const int alpha = transition.time_interval.earliest;
    const int beta = transition.time_interval.latest == petri::INF
                         ? INF_TIME
                         : transition.time_interval.latest;
    const bool became_relevant = (!was_effective && !was_suspended) &&
                                 (is_effective || is_suspended_now);

    if (transition.suspendable) {
      if (became_relevant) {
        state.Z2.set_constraint(0, clock_idx, alpha > 0 ? -alpha : 0);
        state.Z2.set_constraint(clock_idx, 0, beta);
      }

      if (clock_idx < state.Z1.size()) {
        state.Z1.forget_clock(clock_idx);
        state.Z1.unfreeze_clock(clock_idx);
      }

      if (is_suspended_now) {
        state.Z2.freeze_clock(clock_idx);
      } else {
        state.Z2.unfreeze_clock(clock_idx);
      }
    } else if (is_effective) {
      if (became_relevant) {
        state.Z1.set_constraint(0, clock_idx, alpha > 0 ? -alpha : 0);
        state.Z1.set_constraint(clock_idx, 0, beta);
      }

      if (clock_idx < state.Z2.size()) {
        state.Z2.forget_clock(clock_idx);
        state.Z2.unfreeze_clock(clock_idx);
      }
      state.Z1.unfreeze_clock(clock_idx);
    }
  }

  state.Z1.minimize();
  state.Z2.minimize();
}

void StateClassReachabilityGraph::rebuild_post_fire_timing_domains(
    StateClass& state, const StateClass& source_state, size_t fired_transition,
    const std::set<size_t>& previous_effective_enabled,
    const std::set<size_t>& previous_suspended) const {
  const size_t num_transitions = ptpn_.num_transitions();

  if (state.Z1.size() < num_transitions + 1) {
    state.Z1.resize(num_transitions + 1);
  }
  if (state.Z2.size() < num_transitions + 1) {
    state.Z2.resize(num_transitions + 1);
  }

  for (size_t t = 0; t < num_transitions; ++t) {
    const size_t clock_idx = t + 1;
    const bool was_effective =
        previous_effective_enabled.find(t) != previous_effective_enabled.end();
    const bool was_suspended =
        previous_suspended.find(t) != previous_suspended.end();
    const bool is_effective = state.enabled.find(t) != state.enabled.end();
    const bool is_suspended_now = state.suspended.find(t) != state.suspended.end();
    const bool was_relevant = was_effective || was_suspended;
    const bool is_relevant = is_effective || is_suspended_now;
    const bool preserved = t != fired_transition && was_relevant && is_relevant;
    const auto& transition = ptpn_.get_transition(t);

    if (!is_relevant) {
      state.Z1.forget_clock(clock_idx);
      state.Z1.unfreeze_clock(clock_idx);
      state.Z2.forget_clock(clock_idx);
      state.Z2.unfreeze_clock(clock_idx);
      continue;
    }

    if (transition.suspendable) {
      state.Z1.forget_clock(clock_idx);
      state.Z1.unfreeze_clock(clock_idx);

      if (preserved) {
        source_state.Z2.copy_clock_constraints(clock_idx, state.Z2);
      } else {
        state.Z2.reset_clock(clock_idx);
      }

      if (is_suspended_now) {
        state.Z2.freeze_clock(clock_idx);
      } else {
        state.Z2.unfreeze_clock(clock_idx);
      }
    } else {
      state.Z2.forget_clock(clock_idx);
      state.Z2.unfreeze_clock(clock_idx);

      if (preserved) {
        source_state.Z1.copy_clock_constraints(clock_idx, state.Z1);
      } else {
        state.Z1.reset_clock(clock_idx);
      }

      state.Z1.unfreeze_clock(clock_idx);
    }
  }

  state.Z1.minimize();
  state.Z2.minimize();
}

void StateClassReachabilityGraph::normalize_scheduling_state(
    StateClass& state) const {
  const std::set<size_t> previous_effective_enabled = state.enabled;
  const std::set<size_t> previous_suspended = state.suspended;

  const std::vector<size_t> raw_enabled = collect_enabled_transitions(state);
  state.enabled = compute_effective_enabled(raw_enabled);
  state.suspended = compute_suspended_transitions(raw_enabled, state.enabled);

  spdlog::debug("  Raw enabled: {}", format_transition_vector(raw_enabled, ptpn_));
  spdlog::debug("  Effective enabled: {}", format_transitions(state.enabled));
  spdlog::debug("  Suspended: {}", format_transitions(state.suspended));

  reconcile_timing_domains(state, previous_effective_enabled,
                           previous_suspended);
}

void StateClassReachabilityGraph::recompute_suspension(
    StateClass& state) const {
  normalize_scheduling_state(state);
}

DBM StateClassReachabilityGraph::get_invariants_for(
    const std::vector<int>& marking) const {
  size_t num_transitions = ptpn_.num_transitions();
  DBM invariants(num_transitions + 1);
  return invariants;
}

StateClass StateClassReachabilityGraph::fire_transition(const StateClass& state,
                                                        size_t trans_idx,
                                                        double firing_time) {
  spdlog::debug("    Fire transition {} @time {}",
                format_transitions({trans_idx}, false), firing_time);

  StateClass new_state = state;
  new_state.marking = petri::PTPN::fire(state.marking, ptpn_, trans_idx);

  spdlog::debug("      Marking: {} -> {}", format_marking(state.marking),
                format_marking(new_state.marking));

  new_state.cumulative_time = state.cumulative_time + firing_time;

  const auto& transition = ptpn_.get_transition(trans_idx);
  size_t clock_idx = trans_idx + 1;

  if (transition.suspendable) {
    spdlog::debug("      Reset Z2 clock {}", clock_idx);
    new_state.Z2.reset_clock(clock_idx);
  } else {
    spdlog::debug("      Reset Z1 clock {}", clock_idx);
    new_state.Z1.reset_clock(clock_idx);
  }

  update_dbm_constraints(new_state);
  return new_state;
}

void StateClassReachabilityGraph::update_dbm_constraints(StateClass& state) {
  size_t num_transitions = ptpn_.num_transitions();

  if (state.Z1.size() < num_transitions + 1) {
    state.Z1.resize(num_transitions + 1);
  }
  if (state.Z2.size() < num_transitions + 1) {
    state.Z2.resize(num_transitions + 1);
  }

  std::vector<size_t> enabled = collect_enabled_transitions(state);
  std::vector<bool> enabled_flags(num_transitions, false);
  for (size_t t : enabled) {
    enabled_flags[t] = true;
  }

  bool z1_changed = false;
  bool z2_changed = false;
  size_t cleared_count = 0;
  size_t initialized_count = 0;

  for (size_t t = 0; t < num_transitions; ++t) {
    if (!enabled_flags[t]) {
      size_t clock_idx = t + 1;

      if (clock_idx < state.Z1.size()) {
        int upper = state.Z1.get_constraint(clock_idx, 0);
        int lower = state.Z1.get_constraint(0, clock_idx);
        if (upper != INF_TIME || lower != 0) {
          state.Z1.forget_clock(clock_idx);
          z1_changed = true;
          cleared_count++;
        }
      }
      if (clock_idx < state.Z2.size()) {
        int upper = state.Z2.get_constraint(clock_idx, 0);
        int lower = state.Z2.get_constraint(0, clock_idx);
        if (upper != INF_TIME || lower != 0) {
          state.Z2.forget_clock(clock_idx);
          z2_changed = true;
        }
      }

      state.Z1.unfreeze_clock(clock_idx);
      state.Z2.unfreeze_clock(clock_idx);
    }
  }

  if (cleared_count > 0) {
    spdlog::debug("    Cleared {} clocks", cleared_count);
  }

  for (size_t trans_idx : enabled) {
    const auto& transition = ptpn_.get_transition(trans_idx);
    size_t clock_idx = trans_idx + 1;

    bool clock_just_reset = false;
    bool clock_exists = false;

    if (transition.suspendable) {
      if (clock_idx < state.Z2.size()) {
        clock_exists = true;
        int upper = state.Z2.get_constraint(clock_idx, 0);
        int lower = -state.Z2.get_constraint(0, clock_idx);
        if (upper == INF_TIME && lower == 0) {
          clock_just_reset = true;
        }
      }
    } else {
      if (clock_idx < state.Z1.size()) {
        clock_exists = true;
        int upper = state.Z1.get_constraint(clock_idx, 0);
        int lower = -state.Z1.get_constraint(0, clock_idx);
        if (upper == INF_TIME && lower == 0) {
          clock_just_reset = true;
        }
      }
    }

    if (!clock_exists || clock_just_reset) {
      initialized_count++;
      std::string latest_str = transition.time_interval.latest == petri::INF
                                   ? "inf"
                                   : std::to_string(transition.time_interval.latest);
      spdlog::debug("    Initialize{} clock: [{}, {}]",
                    format_transitions({trans_idx}, false),
                    transition.time_interval.earliest, latest_str);

      if (transition.suspendable) {
        if (transition.time_interval.earliest > 0) {
          state.Z2.set_constraint(0, clock_idx,
                                  -transition.time_interval.earliest);
        } else {
          state.Z2.set_constraint(0, clock_idx, 0);
        }
        if (transition.time_interval.latest != petri::INF) {
          state.Z2.set_constraint(clock_idx, 0,
                                  transition.time_interval.latest);
        } else {
          state.Z2.set_constraint(clock_idx, 0, INF_TIME);
        }
        z2_changed = true;
      } else {
        if (transition.time_interval.earliest > 0) {
          state.Z1.set_constraint(0, clock_idx,
                                  -transition.time_interval.earliest);
        } else {
          state.Z1.set_constraint(0, clock_idx, 0);
        }
        if (transition.time_interval.latest != petri::INF) {
          state.Z1.set_constraint(clock_idx, 0,
                                  transition.time_interval.latest);
        } else {
          state.Z1.set_constraint(clock_idx, 0, INF_TIME);
        }
        z1_changed = true;
      }
    }
  }

  if (initialized_count > 0) {
    spdlog::debug("    Initialized {} new enabled clocks", initialized_count);
  }

  if (z1_changed) {
    state.Z1.minimize();
  }
  if (z2_changed) {
    state.Z2.minimize();
  }
}

}  // namespace state_class
