#include "scheduler.h"

#include <map>
#include <climits>

namespace scheduling {

std::set<size_t> SchedulingAlgorithms::select_active_per_core(
    const std::set<size_t>& enabled,
    const petri::PTPN& ptpn) {

  std::map<int, int> per_core_max_priority;

  // 第一遍：找每个核心的最大优先级
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

  // 第二遍：选择每个核心上最高优先级的变迁
  for (size_t t : enabled) {
    const auto& trans = ptpn.get_transition(t);

    if (trans.core < 0) {
      // 控制变迁：全部保留
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
    const std::set<size_t>& schedulable,
    const petri::PTPN& ptpn) {
  if (schedulable.empty()) {
    throw std::runtime_error("Cannot select from empty set");
  }

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
    const std::set<size_t>& enabled,
    const std::set<size_t>& active,
    const petri::PTPN& ptpn) {

  std::set<size_t> result;

  for (size_t t : enabled) {
    if (active.count(t)) continue;  // 已在 active 中

    if (should_suspend(t, active, ptpn)) {
      result.insert(t);
    }
  }

  return result;
}

bool SchedulingAlgorithms::should_suspend(
    size_t t,
    const std::set<size_t>& active,
    const petri::PTPN& ptpn) {

  const auto& trans = ptpn.get_transition(t);

  if (!trans.suspendable) return false;
  if (trans.core < 0) return false;

  // 检查是否存在同核心、更高优先级的活跃变迁
  for (size_t a : active) {
    const auto& active_trans = ptpn.get_transition(a);
    if (active_trans.core == trans.core &&
        active_trans.priority > trans.priority) {
      return true;
    }
  }

  return false;
}

bool SchedulingAlgorithms::should_restore(
    size_t t,
    const std::set<size_t>& active,
    const petri::PTPN& ptpn) {

  const auto& trans = ptpn.get_transition(t);

  if (!trans.suspendable) return false;
  if (trans.core < 0) return false;

  // 检查是否存在同核心、更高优先级的活跃变迁
  for (size_t a : active) {
    const auto& active_trans = ptpn.get_transition(a);
    if (active_trans.core == trans.core &&
        active_trans.priority > trans.priority) {
      return false;  // 仍有更高优先级，不恢复
    }
  }

  return true;  // 无更高优先级，可恢复
}

}  // namespace scheduling