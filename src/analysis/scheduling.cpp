#include "scheduling.h"

#include <map>

namespace state_class {

std::set<size_t> SchedulingAlgorithms::select_active_per_core(
    const std::set<size_t>& enabled, const petri::PTPN& ptpn) {
  std::map<int, int> per_core_max_priority;

  for (size_t t : enabled) {
    const auto& trans = ptpn.get_transition(t);
    if (trans.core < 0) {
      continue;
    }

    auto it = per_core_max_priority.find(trans.core);
    if (it == per_core_max_priority.end()) {
      per_core_max_priority[trans.core] = trans.priority;
    } else if (trans.priority > it->second) {
      it->second = trans.priority;
    }
  }

  std::set<size_t> result;
  for (size_t t : enabled) {
    const auto& trans = ptpn.get_transition(t);
    if (trans.core < 0) {
      result.insert(t);
      continue;
    }

    const int max_priority = per_core_max_priority[trans.core];
    if (trans.priority == max_priority) {
      result.insert(t);
    }
  }

  return result;
}

size_t SchedulingAlgorithms::select_one_transition(
    const std::set<size_t>& schedulable, const petri::PTPN& ptpn) {
  size_t chosen = *schedulable.begin();
  int best_priority = ptpn.get_transition(chosen).priority;

  for (size_t t : schedulable) {
    const int priority = ptpn.get_transition(t).priority;
    if (priority > best_priority || (priority == best_priority && t < chosen)) {
      chosen = t;
      best_priority = priority;
    }
  }

  return chosen;
}

std::set<size_t> SchedulingAlgorithms::compute_suspended(
    const std::set<size_t>& enabled, const std::set<size_t>& active,
    const petri::PTPN& ptpn) {
  std::set<size_t> result;

  for (size_t t : enabled) {
    if (active.count(t)) continue;

    if (should_suspend(t, active, ptpn)) {
      result.insert(t);
    }
  }

  return result;
}

bool SchedulingAlgorithms::should_suspend(size_t t,
                                          const std::set<size_t>& active,
                                          const petri::PTPN& ptpn) {
  const auto& trans = ptpn.get_transition(t);

  if (!trans.suspendable) return false;

  if (trans.core < 0) return false;

  const auto& higher = get_higher_priority_active(t, active, ptpn);
  return !higher.empty();
}

bool SchedulingAlgorithms::should_restore(size_t t,
                                          const std::set<size_t>& active,
                                          const petri::PTPN& ptpn) {
  const auto& trans = ptpn.get_transition(t);

  if (!trans.suspendable) return false;

  if (trans.core < 0) return false;

  const auto& higher = get_higher_priority_active(t, active, ptpn);
  return higher.empty();
}

std::set<size_t> SchedulingAlgorithms::get_higher_priority_active(
    size_t t, const std::set<size_t>& active, const petri::PTPN& ptpn) {
  std::set<size_t> result;

  const auto& target_trans = ptpn.get_transition(t);

  for (size_t a : active) {
    const auto& active_trans = ptpn.get_transition(a);

    if (active_trans.core != target_trans.core) continue;

    if (active_trans.priority > target_trans.priority) {
      result.insert(a);
    }
  }

  return result;
}

}  // namespace state_class
