#include "task_template.h"

#include <algorithm>
#include <map>

namespace scheduling {

void TaskTemplate::build_petri_net(
    petri::PTPN& ptpn,
    const std::vector<TaskNode>& tasks,
    const std::map<std::string, std::string>& dependencies) {

  // 添加核心库所
  std::set<int> cores;
  for (const auto& task : tasks) {
    cores.insert(task.core);
  }
  for (int core : cores) {
    size_t p = ptpn.add_place("core_" + std::to_string(core));
    ptpn.set_initial_marking(p, 1);  // 初始时核心空闲
  }

  // 添加锁库所
  add_lock_places(ptpn, tasks);

  // 为每个任务添加任务链
  std::map<std::string, std::pair<size_t, size_t>> task_places;
  for (const auto& task : tasks) {
    auto [entry, exit] = add_task_chain(ptpn, task);
    task_places[task.name] = {entry, exit};
  }

  // 添加依赖关系
  for (const auto& [from_task, to_task] : dependencies) {
    if (task_places.count(from_task) && task_places.count(to_task)) {
      size_t from_exit = task_places[from_task].second;
      size_t to_entry = task_places[to_task].first;

      size_t t = ptpn.add_transition(from_task + "_to_" + to_task,
                                     {0, 0}, 0, -1, false);
      ptpn.set_pre_arc(from_exit, t);
      ptpn.set_post_arc(t, to_entry);
    }
  }

  // 添加抢占路径
  add_preemption_paths(ptpn);
}

std::pair<size_t, size_t> TaskTemplate::add_task_chain(
    petri::PTPN& ptpn,
    const TaskNode& task) {

  // 创建库所
  size_t p_entry = ptpn.add_place(task.name + "_entry");
  size_t p_ready = ptpn.add_place(task.name + "_ready");
  size_t p_exec = ptpn.add_place(task.name + "_exec");
  size_t p_exit = ptpn.add_place(task.name + "_exit");
  size_t p_core = ptpn.add_place(task.name + "_core_held");

  // 创建变迁
  // t_get_core: [0,0]，获取核心
  size_t t_get_core = ptpn.add_transition(
      task.name + "_get_core",
      {0, 0},
      task.priority,
      task.core,
      false);

  // t_exec: [wcet_min, wcet_max]，执行（可挂起）
  size_t t_exec = ptpn.add_transition(
      task.name + "_exec",
      {task.wcet.first, task.wcet.second},
      task.priority,
      task.core,
      true);

  // t_release_core: [0,0]，释放核心
  size_t t_release_core = ptpn.add_transition(
      task.name + "_release_core",
      {0, 0},
      task.priority,
      task.core,
      false);

  // 设置前向弧
  ptpn.set_pre_arc(p_entry, t_get_core);
  ptpn.set_pre_arc(p_exec, t_exec);
  ptpn.set_pre_arc(p_exec, t_release_core);

  // 设置后向弧
  ptpn.set_post_arc(t_get_core, p_ready);
  ptpn.set_post_arc(t_get_core, p_core);
  ptpn.set_post_arc(t_exec, p_exec);
  ptpn.set_post_arc(t_exec, p_exit);
  ptpn.set_post_arc(t_release_core, p_exec);
  ptpn.set_post_arc(t_release_core, p_core);

  // 初始时在 entry 有 token
  ptpn.set_initial_marking(p_entry, 1);

  // 绑定锁
  if (!task.locks.empty()) {
    bind_task_to_locks(ptpn, task, p_ready, p_exec);
  }

  return {p_entry, p_exit};
}

void TaskTemplate::add_lock_places(
    petri::PTPN& ptpn,
    const std::vector<TaskNode>& tasks) {

  std::set<std::string> all_locks;
  for (const auto& task : tasks) {
    for (const auto& lock : task.locks) {
      all_locks.insert(lock);
    }
  }

  for (const auto& lock : all_locks) {
    size_t p = ptpn.add_place("lock_" + lock);
    ptpn.set_initial_marking(p, 1);  // 初始时锁可用
  }
}

void TaskTemplate::bind_task_to_locks(
    petri::PTPN& ptpn,
    const TaskNode& task,
    size_t ready_place,
    size_t exec_place) {

  for (const auto& lock : task.locks) {
    // 创建获取锁的变迁
    size_t t_acquire = ptpn.add_transition(
        task.name + "_acquire_" + lock,
        {0, 0},
        task.priority,
        task.core,
        false);

    // 创建释放锁的变迁
    size_t t_release = ptpn.add_transition(
        task.name + "_release_" + lock,
        {0, 0},
        task.priority,
        task.core,
        false);

    // 简化实现：实际需要查找锁库所索引并设置弧
    // 这里省略具体实现
  }
}

void TaskTemplate::add_preemption_paths(petri::PTPN& ptpn) {
  // 按核心分组任务
  std::map<int, std::vector<size_t>> tasks_by_core;

  for (size_t t = 0; t < ptpn.num_transitions(); ++t) {
    const auto& trans = ptpn.get_transition(t);
    if (trans.core >= 0 && trans.suspendable) {
      tasks_by_core[trans.core].push_back(t);
    }
  }

  // 对每个核心，按优先级排序，生成抢占路径
  for (auto& [core, transitions] : tasks_by_core) {
    // 按优先级降序排序
    std::sort(transitions.begin(), transitions.end(),
              [&ptpn](size_t a, size_t b) {
                return ptpn.get_transition(a).priority >
                       ptpn.get_transition(b).priority;
              });

    // 生成抢占路径（简化版本）
    // 实际实现需要添加抢占/恢复库所和变迁
  }
}

}  // namespace scheduling