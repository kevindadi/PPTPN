#ifndef ANALYSIS_SCHEDULING_TASK_TEMPLATE_H
#define ANALYSIS_SCHEDULING_TASK_TEMPLATE_H

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../../petri/petri.h"

namespace scheduling {

/**
 * TaskTemplate - 任务模板生成器
 *
 * 提供从任务描述到 Petri网的标准化转换。
 *
 * 每个任务节点映射为：
 *   entry --[get_core@0,0]--> ready --[exec@wcet]--> exit --[to_next@0,0]--> next_entry
 */
class TaskTemplate {
 public:
  struct TaskNode {
    std::string name;
    int priority = 100;
    int core = 0;
    std::pair<int, int> wcet = {1, 10};  // [min, max]
    std::vector<std::string> locks; // 锁需求
  };

  /**
   * build_petri_net - 从任务描述构建 Petri 网
   */
  static void build_petri_net(
      petri::PTPN& ptpn,
      const std::vector<TaskNode>& tasks,
      const std::map<std::string, std::string>& dependencies);

  /**
   * add_task_chain - 添加单个任务链
   * @return {entry_place_idx, exit_place_idx}
   */
  static std::pair<size_t, size_t> add_task_chain(
      petri::PTPN& ptpn,
      const TaskNode& task);

  /**
   * add_preemption_paths - 添加抢占路径
   *
   * 对于每个核心，按优先级排序任务，生成抢占/恢复路径。
   */
  static void add_preemption_paths(petri::PTPN& ptpn);

 private:
  static void add_lock_places(petri::PTPN& ptpn,
                              const std::vector<TaskNode>& tasks);
  static void bind_task_to_locks(petri::PTPN& ptpn,
                                 const TaskNode& task,
                                 size_t ready_place,
                                 size_t exec_place);
};

}  // namespace scheduling

#endif  // ANALYSIS_SCHEDULING_TASK_TEMPLATE_H