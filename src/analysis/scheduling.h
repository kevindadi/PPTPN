#ifndef ANALYSIS_SCHEDULING_H
#define ANALYSIS_SCHEDULING_H

#include <set>

#include "petri/petri.h"

namespace state_class {

/**
 * SchedulingAlgorithms - 调度相关算法
 *
 * 核心功能：
 * - 选择每个核心上最高优先级的变迁
 * - 计算应该挂起的变迁集合
 * - 判断变迁是否应该挂起/恢复
 */
class SchedulingAlgorithms {
 public:
  /**
   * select_active_per_core - 选择每个核心上最高优先级变迁
   *
   * 从使能变迁集合中，为每个核心选择优先级最高的变迁。
   * 控制变迁（core < 0）直接包含在结果中。
   *
   * @param enabled 使能变迁集合
   * @param ptpn PTPN 网
   * @return 每个核心上应激活的最高优先级变迁集合
   */
  static std::set<size_t> select_active_per_core(
      const std::set<size_t>& enabled, const petri::PTPN& ptpn);

  /**
   * compute_suspended - 计算应该挂起的变迁集合
   *
   * 对于 enabled 中非 active 的可挂起变迁，检查是否存在同核心、
   * 更高优先级的活跃变迁。如果存在，则该变迁应该被挂起。
   *
   * @param enabled 使能变迁集合
   * @param active 活跃变迁集合
   * @param ptpn PTPN 网
   * @return 应该挂起的变迁集合
   */
  static std::set<size_t> compute_suspended(
      const std::set<size_t>& enabled,
      const std::set<size_t>& active,
      const petri::PTPN& ptpn);

  /**
   * should_suspend - 检查变迁是否应该挂起
   *
   * 判断变迁 t 是否应该被挂起：
   * 1. t 必须是可挂起的（suspendable == true）
   * 2. t 必须在某个核心上（core >= 0）
   * 3. 存在同核心、更高优先级的活跃变迁
   *
   * @param t 变迁索引
   * @param active 活跃变迁集合
   * @param ptpn PTPN 网
   * @return 如果应该挂起返回 true
   */
  static bool should_suspend(size_t t,
                             const std::set<size_t>& active,
                             const petri::PTPN& ptpn);

  /**
   * should_restore - 检查变迁是否应该恢复
   *
   * 判断挂起的变迁 t 是否应该恢复执行：
   * 1. t 当前是挂起状态
   * 2. 同核心上没有更高优先级的活跃变迁
   *
   * @param t 变迁索引
   * @param active 活跃变迁集合
   * @param ptpn PTPN 网
   * @return 如果应该恢复返回 true
   */
  static bool should_restore(size_t t,
                            const std::set<size_t>& active,
                            const petri::PTPN& ptpn);

  /**
   * get_higher_priority_active - 获取同核心更高优先级活跃变迁
   *
   * @param t 变迁索引
   * @param active 活跃变迁集合
   * @param ptpn PTPN 网
   * @return 同核心更高优先级活跃变迁集合
   */
  static std::set<size_t> get_higher_priority_active(
      size_t t,
      const std::set<size_t>& active,
      const petri::PTPN& ptpn);
};

}  // namespace state_class

#endif  // ANALYSIS_SCHEDULING_H