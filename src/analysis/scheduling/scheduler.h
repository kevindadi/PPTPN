#ifndef ANALYSIS_SCHEDULING_SCHEDULER_H
#define ANALYSIS_SCHEDULING_SCHEDULER_H

#include <set>
#include <string>

#include "../../petri/petri.h"

namespace scheduling {

/**
 * SchedulingAlgorithms - 调度相关算法
 *
 * 核心功能:
 * - 选择每个核心上最高优先级的变迁
 * - 计算应该挂起的变迁集合
 * - 选择单个变迁发生
 */
class SchedulingAlgorithms {
 public:
  /**
   * select_active_per_core - 每个核心上最高优先级变迁集合
   *
   * 对候选集合中每个核心 k>=0，保留所有满足 pi(t)=max_{u in E_k} pi(u) 的变迁。
   * 控制变迁（core < 0）凡在候选集中则全部保留。
   */
  static std::set<size_t> select_active_per_core(
      const std::set<size_t>& enabled,
      const petri::PTPN& ptpn);

  /**
   * select_one_transition - 从可调度集合中选一个变迁发生
   *
   * 选取最高优先级者；并列时取编号最小者。
   */
  static size_t select_one_transition(
      const std::set<size_t>& schedulable,
      const petri::PTPN& ptpn);

  /**
   * compute_suspended - 计算应该挂起的变迁集合
   *
   * 对于 enabled 中非 active 的可挂起变迁，检查是否存在同核心、
   *更高优先级的活跃变迁。如果存在，则该变迁应该被挂起。
   */
  static std::set<size_t> compute_suspended(
      const std::set<size_t>& enabled,
      const std::set<size_t>& active,
      const petri::PTPN& ptpn);

  /**
   * should_suspend - 检查变迁是否应该挂起
   */
  static bool should_suspend(
      size_t t,
      const std::set<size_t>& active,
      const petri::PTPN& ptpn);

  /**
   * should_restore - 检查变迁是否应该恢复
   */
  static bool should_restore(
      size_t t,
      const std::set<size_t>& active,
      const petri::PTPN& ptpn);
};

}  // namespace scheduling

#endif  // ANALYSIS_SCHEDULING_SCHEDULER_H