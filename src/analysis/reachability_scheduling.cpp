#include "analysis/state.h"

#include <map>
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

}  // namespace

std::vector<size_t> StateClassReachabilityGraph::select_per_core(
    const std::set<size_t>& enabled) const {
  std::map<int, std::vector<size_t>> per_core_group;
  std::map<int, int> per_core_best_priority;

  for (size_t t : enabled) {
    const auto& transition = ptpn_.get_transition(t);
    int core = transition.core;
    if (core < 0) {
      continue;
    }

    auto it = per_core_best_priority.find(core);
    if (it == per_core_best_priority.end() ||
        transition.priority > it->second) {
      per_core_best_priority[core] = transition.priority;
      per_core_group[core] = {t};
    } else if (transition.priority == it->second) {
      per_core_group[core].push_back(t);
    }
  }

  std::vector<size_t> chosen;
  for (const auto& [core, group] : per_core_group) {
    for (size_t t : group) {
      chosen.push_back(t);
    }
  }

  for (size_t t : enabled) {
    const auto& transition = ptpn_.get_transition(t);
    if (transition.core < 0) {
      chosen.push_back(t);
    }
  }

  spdlog::debug("  Per-core scheduling groups ({} total):", chosen.size());
  for (const auto& [core, group] : per_core_group) {
    spdlog::debug("    Core {}: {} transition(s)", core, group.size());
  }
  int control_count = 0;
  for (size_t t : enabled) {
    if (ptpn_.get_transition(t).core < 0) {
      control_count++;
    }
  }
  if (control_count > 0) {
    spdlog::debug("    Core -1 (control): {} transition(s)", control_count);
  }

  return chosen;
}

void StateClassReachabilityGraph::apply_preemption(
    const std::vector<size_t>& chosen, StateClass& state) const {
  state.suspended.clear();

  size_t num_transitions = ptpn_.num_transitions();
  for (size_t u = 0; u < num_transitions; ++u) {
    const auto& transition_u = ptpn_.get_transition(u);

    if (!transition_u.suspendable) {
      continue;
    }

    for (size_t t : chosen) {
      const auto& transition_t = ptpn_.get_transition(t);

      if (transition_t.core == transition_u.core &&
          has_higher_priority(transition_t, transition_u)) {
        state.suspended.insert(u);

        size_t clock_idx = u + 1;
        if (clock_idx < state.Z1.size() && clock_idx < state.Z2.size()) {
          state.Z1.copy_clock_constraints(clock_idx, state.Z2);
          state.Z1.freeze_clock(clock_idx);
          state.Z2.freeze_clock(clock_idx);
        }

        spdlog::debug("    {}: preempted by {}, freeze",
                      format_transitions({u}, false),
                      format_transitions({t}, false));
        break;
      }
    }
  }
}

bool StateClassReachabilityGraph::is_suspended(
    size_t trans_idx, const std::vector<size_t>& enabled) const {
  const auto& transition = ptpn_.get_transition(trans_idx);

  if (!transition.suspendable) {
    return false;
  }

  for (size_t other_t : enabled) {
    if (other_t == trans_idx) {
      continue;
    }

    const auto& other_trans = ptpn_.get_transition(other_t);
    if (other_trans.core == transition.core && !other_trans.suspendable &&
        has_higher_priority(other_trans, transition)) {
      return true;
    }
  }

  return false;
}

bool StateClassReachabilityGraph::maximal_time_elapse(StateClass& state,
                                                      double& dt) const {
  int ub_star = INF_TIME;

  size_t num_clocks = state.Z1.size();
  size_t num_transitions = ptpn_.num_transitions();

  for (size_t i = 1; i < num_clocks && i <= num_transitions; ++i) {
    if (state.Z1.is_frozen(i)) {
      continue;
    }

    int ub = state.Z1.get_constraint(i, 0);
    if (ub != INF_TIME && ub < ub_star) {
      ub_star = ub;
    }
  }

  num_clocks = state.Z2.size();
  for (size_t i = 1; i < num_clocks && i <= num_transitions; ++i) {
    if (state.Z2.is_frozen(i)) {
      continue;
    }

    int ub = state.Z2.get_constraint(i, 0);
    if (ub != INF_TIME && ub < ub_star) {
      ub_star = ub;
    }
  }

  if (ub_star == INF_TIME || ub_star <= 0) {
    dt = 0;
    return false;
  }

  state.Z1.elapse_time(ub_star);
  state.Z2.elapse_time(ub_star);
  state.cumulative_time += ub_star;
  dt = ub_star;

  spdlog::debug("  Maximal time elapse: dt = {}", dt);

  return true;
}

}  // namespace state_class
