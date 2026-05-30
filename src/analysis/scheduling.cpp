#include "scheduling.h"

#include <map>

namespace state_class {

std::set<size_t> SchedulingAlgorithms::select_active_per_core(
    const std::set<size_t>& enabled,
    const petri::PTPN& ptpn) {

  // core -> 最高优先级变迁
  std::map<int, size_t> per_core_best;

  // 遍历所有使能变迁，选择每个核心上优先级最高的
  for (size_t t : enabled) {
    const auto& trans = ptpn.get_transition(t);

    // 控制变迁（core < 0）由其他机制处理，先跳过
    if (trans.core < 0) continue;

    auto it = per_core_best.find(trans.core);
    if (it == per_core_best.end()) {
      // 该核心还没有选中任何变迁
      per_core_best[trans.core] = t;
    } else {
      // 比较优先级（假设更大的数字表示更高优先级）
      const auto& current_best = ptpn.get_transition(it->second);
      if (trans.priority > current_best.priority) {
        per_core_best[trans.core] = t;
      }
    }
  }

  // 构建结果集
  std::set<size_t> result;
  for (const auto& [core, t] : per_core_best) {
    (void)core;  // 消除未使用警告
    result.insert(t);
  }

  // 添加控制变迁（core < 0）
  for (size_t t : enabled) {
    const auto& trans = ptpn.get_transition(t);
    if (trans.core < 0) {
      result.insert(t);
    }
  }

  return result;
}

std::set<size_t> SchedulingAlgorithms::compute_suspended(
    const std::set<size_t>& enabled,
    const std::set<size_t>& active,
    const petri::PTPN& ptpn) {

  std::set<size_t> result;

  for (size_t t : enabled) {
    // 已经在 active 中的不需要挂起
    if (active.count(t)) continue;

    // 检查是否应该挂起
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

  // 必须是可挂起的变迁
  if (!trans.suspendable) return false;

  // 必须在某个核心上（控制变迁不参与抢占调度）
  if (trans.core < 0) return false;

  // 检查是否存在同核心、更高优先级的活跃变迁
  const auto& higher = get_higher_priority_active(t, active, ptpn);
  return !higher.empty();
}

bool SchedulingAlgorithms::should_restore(size_t t,
                                          const std::set<size_t>& active,
                                          const petri::PTPN& ptpn) {
  const auto& trans = ptpn.get_transition(t);

  // 必须是可挂起的变迁
  if (!trans.suspendable) return false;

  // 必须在某个核心上
  if (trans.core < 0) return false;

  // 检查是否存在同核心、更高优先级的活跃变迁
  // 如果没有更高优先级活跃变迁，则应该恢复
  const auto& higher = get_higher_priority_active(t, active, ptpn);
  return higher.empty();
}

std::set<size_t> SchedulingAlgorithms::get_higher_priority_active(
    size_t t,
    const std::set<size_t>& active,
    const petri::PTPN& ptpn) {

  std::set<size_t> result;

  const auto& target_trans = ptpn.get_transition(t);

  for (size_t a : active) {
    const auto& active_trans = ptpn.get_transition(a);

    // 必须是同一个核心
    if (active_trans.core != target_trans.core) continue;

    // 必须可挂起（高优先级变迁如果是不可挂起的，也会抢占其他变迁）
    // 但实际上不可挂起的变迁也可能抢占，这里改为不检查 suspendable
    // 因为不可挂起变迁的执行同样会导致低优先级变迁被挂起

    // 必须是更高优先级
    if (active_trans.priority > target_trans.priority) {
      result.insert(a);
    }
  }

  return result;
}

}  // namespace state_class