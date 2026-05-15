#include "matrix_ptpn.h"

#include <boost/graph/graph_traits.hpp>
#include <map>
#include <spdlog/spdlog.h>
#include <unordered_map>

#include "clap.h"
#include "dag.h"

using namespace boost;

namespace matrix_ptpn {

static void info(const std::string& msg) {
  spdlog::info("[MATRIX_PTPN] {}", msg);
}

static void warn(const std::string& msg) {
  spdlog::warn("[MATRIX_PTPN] {}", msg);
}

static void error(const std::string& msg) {
  spdlog::error("[MATRIX_PTPN] {}", msg);
}

void MatrixPTPN::transform_tdg_to_matrix_ptpn(TDG& tdg) {
  try {
    info("[MATRIX_PTPN] 开始从 TDG 转换到矩阵形式 PTPN...");

    info("[MATRIX_PTPN] 开始转换顶点...");
    transform_vertices_from_tdg(tdg);

    info("[MATRIX_PTPN] 开始转换边...");
    transform_edges_from_tdg(tdg);

    info("[MATRIX_PTPN] 开始创建优先级抢占关系...");
    add_preempt_task_matrix(tdg.classify_priority(), tdg.tasks_config,
                            tdg.nodes_type);

    info("[MATRIX_PTPN] 开始添加资源和绑定...");
    add_resources_and_bindings_matrix(tdg);

    spdlog::info("[MATRIX_PTPN] TDG 转换完成,共 {} 个库所, {} 个变迁",
                 places.size(), transitions.size());

    if (!verify_structure()) {
      warn("[MATRIX_PTPN] 结构验证失败,但继续执行");
    }
  } catch (const std::exception& e) {
    spdlog::error("[MATRIX_PTPN] Failed to transform TDG to Matrix PTPN: {}", e.what());
    throw;
  }
}

void MatrixPTPN::transform_vertices_from_tdg(TDG& tdg) {
  BOOST_FOREACH (const TDG_RAP::vertex_descriptor v, vertices(tdg.tdg)) {
    const string& vertex_name = tdg.tdg[v].name;
    spdlog::debug("[MATRIX_PTPN] Processing vertex: {}", vertex_name);

    try {
      auto node_type_it = tdg.nodes_type.find(vertex_name);
      if (node_type_it == tdg.nodes_type.end()) {
        throw std::runtime_error("Node type not found for: " + vertex_name);
      }

      auto [start_idx, end_idx] = add_node_matrix(node_type_it->second);
      node_start_end_map[vertex_name] = std::make_pair(start_idx, end_idx);

      if (out_degree(v, tdg.tdg) == 0) {
        if (holds_alternative<APeriodicTask>(node_type_it->second)) {
          TimeInterval interval(0, 0);
          size_t consume_trans = add_transition(vertex_name + "_consume",
                                                interval, 411, 411, false);
          set_pre_arc(end_idx, consume_trans, 1);
          spdlog::debug("[MATRIX_PTPN] Added consume token transition for APerodicTask: {} (from exit place {})",
                        vertex_name, end_idx);
        }
      }
    } catch (const std::exception& e) {
      spdlog::error("[MATRIX_PTPN] Failed to transform vertex {}: {}", vertex_name, e.what());
      throw;
    }
  }
  info("[MATRIX_PTPN] Vertex transformation completed");
}

void MatrixPTPN::transform_edges_from_tdg(TDG& tdg) {
  BOOST_FOREACH (TDG_RAP::edge_descriptor e, edges(tdg.tdg)) {
    try {
      const string& source_name = tdg.tdg[source(e, tdg.tdg)].name;
      const string& target_name = tdg.tdg[target(e, tdg.tdg)].name;

      if (is_self_loop_edge(source_name, target_name)) {
        handle_self_loop_edge_matrix(tdg, &e, source_name);
        continue;
      }

      if (is_dashed_edge(tdg.tdg[e].style)) {
        handle_dashed_edge_matrix(source_name, target_name);
        continue;
      }

      handle_normal_edge_matrix(source_name, target_name);
    } catch (const std::exception& exception) {
      spdlog::error("[MATRIX_PTPN] Failed to transform edge: {}", exception.what());
      throw;
    }
  }
}

bool MatrixPTPN::is_self_loop_edge(const string& source, const string& target) {
  return source == target;
}

bool MatrixPTPN::is_dashed_edge(const string& edge) {
  return edge.find("dashed") != string::npos;
}

void MatrixPTPN::handle_self_loop_edge_matrix(TDG& tdg, void* edge_desc,
                                              const string& source_name) {
  TDG_RAP::edge_descriptor e =
      *static_cast<TDG_RAP::edge_descriptor*>(edge_desc);
  int task_period_time = std::stoi(tdg.tdg[e].label);
  auto task_start_end = node_start_end_map.find(source_name);
  if (task_start_end == node_start_end_map.end()) {
    throw std::runtime_error("Start/end nodes not found for: " + source_name);
  }

  add_monitor_matrix(source_name, task_period_time,
                     task_start_end->second.first,
                     task_start_end->second.second);
}

void MatrixPTPN::handle_dashed_edge_matrix(const string& source_name,
                                           const string& target_name) {
  // 虚线链接的尾节点为开始节点,头节点为结束节点
  // TODO: 实现虚线边的处理逻辑
}

void MatrixPTPN::handle_normal_edge_matrix(const string& source_name,
                                           const string& target_name) {
  const auto source_it = node_start_end_map.find(source_name);
  const auto target_it = node_start_end_map.find(target_name);

  if (source_it == node_start_end_map.end() ||
      target_it == node_start_end_map.end()) {
    throw std::runtime_error("Node mapping not found for edge: " + source_name +
                             " -> " + target_name);
  }

  size_t source_node = source_it->second.second;
  size_t target_node = target_it->second.first;

  spdlog::debug("[MATRIX_PTPN] source_name: {}", source_name);
  spdlog::debug("[MATRIX_PTPN] target_name: {}", target_name);

  if (source_name.substr(0, 4) == "Dist" ||
      source_name.substr(0, 4) == "Wait") {
    if (source_node < places.size()) {
      TimeInterval interval(0, 0);
      size_t middle_trans = add_transition(source_name + "_to_" + target_name,
                                           interval, 411, 411, false);
      set_pre_arc(source_node, middle_trans, 1);
      set_post_arc(middle_trans, target_node, 1);
    }
    return;
  }
  if (target_name.substr(0, 4) == "Dist" ||
      target_name.substr(0, 4) == "Wait") {
    if (source_node < places.size()) {
      TimeInterval interval(0, 0);
      size_t middle_trans = add_transition(source_name + "_to_" + target_name,
                                           interval, 411, 411, false);
      set_pre_arc(source_node, middle_trans, 1);
      set_post_arc(middle_trans, target_node, 1);
    }
    return;
  }

  const string trans_name = source_name + "_to_" + target_name;
  TimeInterval interval(0, 0);
  size_t middle_trans = add_transition(trans_name, interval, 411, 411, false);

  if (source_node < places.size() && target_node < places.size()) {
    set_pre_arc(source_node, middle_trans, 1);
    set_post_arc(middle_trans, target_node, 1);
  } else {
    spdlog::warn("[MATRIX_PTPN] Unexpected node types for edge: {} -> {}", source_name, target_name);
  }

  spdlog::debug("[MATRIX_PTPN] Added edge: {} -> {}", source_name, target_name);
}

void MatrixPTPN::add_resources_and_bindings_matrix(TDG& tdg) {
  add_cpu_resource_matrix(tdg.num_cpus, tdg.cores_per_cpu);

  add_lock_resource_matrix(tdg.lock_set);

  task_bind_cpu_resource_matrix(tdg.all_task);

  task_bind_lock_resource_matrix(tdg.all_task, tdg.task_locks_map);
}

void MatrixPTPN::add_cpu_resource_matrix(int cpus, int cores_per_cpu) {
  for (int i = 0; i < cpus; i++) {
    string cpu_name = "core" + std::to_string(i);
    size_t c = add_place(cpu_name, cores_per_cpu);
    cpus_place.push_back(c);
    set_initial_marking(c, cores_per_cpu);
  }
  info("[MATRIX_PTPN] create core resource!");
}

void MatrixPTPN::add_lock_resource_matrix(const set<string>& locks_name) {
  if (locks_name.empty()) {
    info("[MATRIX_PTPN] TDG_RAP without locks!");
    return;
  }
  for (const auto& lock_name : locks_name) {
    size_t l = add_place(lock_name, 1);
    locks_place.insert(make_pair(lock_name, l));
    set_initial_marking(l, 1);
  }
  info("[MATRIX_PTPN] create lock resource!");
}

void MatrixPTPN::task_bind_cpu_resource_matrix(
    const vector<NodeType>& all_task) {
  for (const auto& task : all_task) {
    if (std::holds_alternative<APeriodicTask>(task)) {
      auto ap_task = std::get<APeriodicTask>(task);
      const int cpu_index = ap_task.core;
      auto task_pt_chains = node_pn_map.find(ap_task.name)->second;
      if (task_pt_chains.size() >= 2) {
        set_pre_arc(cpus_place[cpu_index], task_pt_chains[1], 1);
        if (task_pt_chains.size() >= 5) {
          set_post_arc(task_pt_chains[task_pt_chains.size() - 2],
                       cpus_place[cpu_index], 1);
        }
      }
    } else if (std::holds_alternative<PeriodicTask>(task)) {
      auto p_task = std::get<PeriodicTask>(task);
      const int cpu_index = p_task.core;
      auto task_pt_chains = node_pn_map.find(p_task.name)->second;
      if (task_pt_chains.size() >= 2) {
        set_pre_arc(cpus_place[cpu_index], task_pt_chains[1], 1);
        if (task_pt_chains.size() >= 5) {
          set_post_arc(task_pt_chains[task_pt_chains.size() - 2],
                       cpus_place[cpu_index], 1);
        }
      }
    }
  }
}

void MatrixPTPN::task_bind_lock_resource_matrix(
    const vector<NodeType>& all_task,
    std::map<string, vector<string>>& task_locks) {
  if (task_locks.empty()) {
    info("[MATRIX_PTPN] No task locks to bind");
    return;
  }

  for (const auto& task : all_task) {
    try {
      if (std::holds_alternative<APeriodicTask>(task)) {
        const auto& ap_task = std::get<APeriodicTask>(task);
        if (auto chains_it = node_pn_map.find(ap_task.name);
            chains_it != node_pn_map.end()) {
          bind_task_locks_matrix(ap_task.name, ap_task.lock, chains_it->second,
                                 task_locks);
        }
      } else if (std::holds_alternative<PeriodicTask>(task)) {
        const auto& p_task = std::get<PeriodicTask>(task);
        auto chains_it = node_pn_map.find(p_task.name);
        if (chains_it != node_pn_map.end()) {
          bind_task_locks_matrix(p_task.name, p_task.lock, chains_it->second,
                                 task_locks);
        }
      }
    } catch (const std::exception& e) {
      spdlog::error("[MATRIX_PTPN] Failed to process task: {}", e.what());
      throw;
    }
  }

  info("[MATRIX_PTPN] Completed lock resource binding for all tasks");
}

void MatrixPTPN::bind_task_locks_matrix(
    const string& task_name, const vector<string>& lock_types,
    const vector<size_t>& task_pt_chain,
    std::map<string, vector<string>>& task_locks) {
  constexpr size_t MIN_CHAIN_LENGTH = 5;
  if (task_pt_chain.size() < MIN_CHAIN_LENGTH) {
    spdlog::debug("[MATRIX_PTPN] Skip chain for {}: too short for locks", task_name);
    return;
  }

  auto task_locks_it = task_locks.find(task_name);
  if (task_locks_it == task_locks.end()) {
    spdlog::warn("[MATRIX_PTPN] No locks found for task: {}", task_name);
    return;
  }

  const size_t lock_nums = task_locks_it->second.size();

  for (size_t i = 0; i < lock_nums; i++) {
    try {
      const string& lock_type = lock_types[i];

      const size_t get_lock = task_pt_chain[3 + 2 * i];
      const size_t drop_lock =
          task_pt_chain[task_pt_chain.size() - 2 - 2 * (i + 1)];

      auto lock_it = locks_place.find(lock_type);
      if (lock_it == locks_place.end()) {
        throw std::runtime_error("Lock place not found: " + lock_type);
      }
      const size_t lock = lock_it->second;

      if (get_lock < transitions.size()) {
        set_pre_arc(lock, get_lock, 1);
      }
      if (drop_lock < transitions.size()) {
        set_post_arc(drop_lock, lock, 1);
      }

      spdlog::debug("[MATRIX_PTPN] Bound lock {} to task {}", lock_type, task_name);
    } catch (const std::exception& e) {
      spdlog::error("[MATRIX_PTPN] Failed to bind lock {} for task {}: {}", i, task_name, e.what());
      throw;
    }
  }
}

bool MatrixPTPN::verify_structure() const {
  bool is_valid = true;

  if (!Pre.empty() && !transitions.empty()) {
    size_t expected_cols = transitions.size();
    for (size_t p = 0; p < Pre.size(); ++p) {
      if (Pre[p].size() != expected_cols) {
        spdlog::error("[MATRIX_PTPN] Pre矩阵第 {} 行维度不一致", p);
        is_valid = false;
      }
    }
  }

  if (!Post.empty() && !places.empty()) {
    size_t expected_cols = places.size();
    for (size_t t = 0; t < Post.size(); ++t) {
      if (Post[t].size() != expected_cols) {
        spdlog::error("[MATRIX_PTPN] Post矩阵第 {} 行维度不一致", t);
        is_valid = false;
      }
    }
  }

  if (M0.size() != places.size()) {
    error("[MATRIX_PTPN] 初始标识维度与库所数量不一致");
    is_valid = false;
  }

  for (size_t t = 0; t < transitions.size(); ++t) {
    if (!transitions[t].time_interval.is_valid()) {
      spdlog::error("[MATRIX_PTPN] 变迁 T{} 的时间区间无效", t);
      is_valid = false;
    }
  }

  if (is_valid) {
    info("[MATRIX_PTPN] 结构验证通过");
  }

  return is_valid;
}

std::pair<size_t, size_t> MatrixPTPN::add_node_matrix(
    const NodeType& node_type) {
  if (holds_alternative<PeriodicTask>(node_type)) {
    auto p_task = get<PeriodicTask>(node_type);
    return add_p_node_matrix(p_task);
  } else if (holds_alternative<APeriodicTask>(node_type)) {
    auto ap_task = get<APeriodicTask>(node_type);
    return add_ap_node_matrix(ap_task);
  } else if (holds_alternative<JoinTask>(node_type)) {
    auto [name, time] = get<JoinTask>(node_type);
    TimeInterval interval(0, 0);
    size_t sync_trans = add_transition("Sync" + std::to_string(node_index++),
                                       interval, 411, 411, false);
    return std::make_pair(sync_trans, sync_trans);
  } else if (holds_alternative<ForkTask>(node_type)) {
    auto [name, time] = get<ForkTask>(node_type);
    TimeInterval interval(0, 0);
    size_t dist_trans = add_transition("Dist" + std::to_string(node_index++),
                                       interval, 411, 411, false);
    return std::make_pair(dist_trans, dist_trans);
  } else {
    auto [name] = get<EmptyTask>(node_type);
    size_t empty_place = add_place("Empty" + std::to_string(node_index++), 1);
    return std::make_pair(empty_place, empty_place);
  }
}

std::pair<size_t, size_t> MatrixPTPN::add_p_node_matrix(PeriodicTask& p_task) {
  size_t entry = add_place(p_task.name + "entry", 1);
  TimeInterval get_core_interval(0, 0);
  size_t get_core = add_transition(p_task.name + "get_core", get_core_interval,
                                   p_task.priority, p_task.core, false);
  size_t ready = add_place(p_task.name + "ready", 1);
  TimeInterval exec_interval(p_task.time.front().first,
                             p_task.time.front().second);
  size_t exec = add_transition(p_task.name + "exec", exec_interval,
                               p_task.priority, p_task.core, false);
  size_t exit = add_place(p_task.name + "exit", 1);

  size_t random = add_place(p_task.name + "random", 1);
  TimeInterval fire_interval(p_task.period_time.first,
                             p_task.period_time.second);
  size_t fire =
      add_transition(p_task.name + "fire", fire_interval, 411, 411, false);

  set_initial_marking(random, 1);

  set_pre_arc(random, fire, 1);
  set_post_arc(fire, random, 1);
  set_post_arc(fire, entry, 1);
  set_pre_arc(entry, get_core, 1);
  set_post_arc(get_core, ready, 1);
  set_pre_arc(ready, exec, 1);
  set_post_arc(exec, exit, 1);

  std::vector<size_t> chain = {entry, get_core, ready, exec, exit};
  node_pn_map[p_task.name] = chain;

  return std::make_pair(entry, exit);
}

std::pair<size_t, size_t> MatrixPTPN::add_ap_node_matrix(
    APeriodicTask& ap_task) {
  size_t entry = add_place(ap_task.name + "entry", 1);
  TimeInterval get_core_interval(0, 0);
  size_t get_core = add_transition(ap_task.name + "get_core", get_core_interval,
                                   ap_task.priority, ap_task.core, false);
  size_t ready = add_place(ap_task.name + "ready", 1);
  TimeInterval exec_interval(ap_task.time.front().first,
                             ap_task.time.front().second);
  size_t exec = add_transition(ap_task.name + "exec", exec_interval,
                               ap_task.priority, ap_task.core, false);
  size_t exit = add_place(ap_task.name + "exit", 1);

  set_pre_arc(entry, get_core, 1);
  set_post_arc(get_core, ready, 1);
  set_pre_arc(ready, exec, 1);
  set_post_arc(exec, exit, 1);

  std::vector<size_t> chain = {entry, get_core, ready, exec, exit};
  node_pn_map[ap_task.name] = chain;

  return std::make_pair(entry, exit);
}

void MatrixPTPN::add_monitor_matrix(const std::string& task_name,
                                    int task_period_time, size_t start,
                                    size_t end) {
  size_t deadline = add_place(task_name + "deadline", 1);
  size_t timeout = add_place(task_name + "timeout", 1);
  size_t ok = add_place(task_name + "ok", 1);
  size_t t_end = add_place(task_name + "end", 1);

  TimeInterval timed_interval(task_period_time, task_period_time);
  size_t timed =
      add_transition(task_name + "timed", timed_interval, 411, 411, false);
  size_t ending =
      add_transition(task_name + "ending", TimeInterval(0, 0), 411, 411, false);
  size_t complete = add_transition(task_name + "complete", TimeInterval(0, 0),
                                   411, 411, false);
  size_t tout =
      add_transition(task_name + "out", TimeInterval(0, 0), 411, 411, false);

  set_post_arc(ending, t_end, 1);
  set_pre_arc(t_end, complete, 1);
  set_post_arc(complete, ok, 1);
  set_pre_arc(deadline, ok, 1);
  set_pre_arc(deadline, tout, 1);
  set_post_arc(tout, timeout, 1);

  if (end < places.size()) {
    set_pre_arc(end, ending, 1);
  } else {
    spdlog::warn("[MATRIX_PTPN] end node is transition, cannot add monitor edge");
  }

  if (start < places.size()) {
    set_pre_arc(start, timed, 1);
    set_post_arc(timed, deadline, 1);
  }
}

void MatrixPTPN::add_preempt_task_matrix(
    const std::unordered_map<int, std::vector<std::string>>& core_task,
    const std::unordered_map<std::string, TaskConfig>& tc,
    const std::unordered_map<std::string, NodeType>& nodes_type) {
  info("[MATRIX_PTPN] 开始添加抢占任务...");

  auto handle_task_preemption =
      [&](const std::string& l_t_name, const std::string& h_t_name,
          const TaskConfig& l_tc, const TaskConfig& h_tc,
          const std::vector<size_t>& l_t_pn, const std::vector<size_t>& h_t_pn,
          bool is_interrupt) {
        if (l_t_pn.size() < 5 || h_t_pn.size() < 5) {
          spdlog::warn("[MATRIX_PTPN] 任务链长度不足,跳过抢占: {} <- {}", l_t_name, h_t_name);
          return;
        }

        size_t l_exec = l_t_pn[3];
        if (l_exec < transitions.size()) {
          transitions[l_exec].suspendable = true;
        }

        size_t l_entry = l_t_pn[0];
        size_t l_preempt_place = l_t_pn[2];
        size_t l_handle_trans = l_t_pn[3];
        size_t h_entry = h_t_pn[0];
        size_t h_ready = h_t_pn[2];
        size_t h_exit = h_t_pn[4];

        if (is_interrupt) {
          spdlog::debug("[MATRIX_PTPN] 中断任务抢占: {} 抢占 {}", h_t_name, l_t_name);

          std::string preempt_t1_name =
              h_t_name + "_preempt_t1_" + std::to_string(node_index);
          std::string preempt_p1_name =
              h_t_name + "_preempt_p1_" + std::to_string(node_index);
          std::string preempt_t2_name =
              h_t_name + "_preempt_t2_" + std::to_string(node_index);

          TimeInterval t1_interval(0, 0);
          size_t t1 = add_transition(preempt_t1_name, t1_interval,
                                     h_tc.priority, h_tc.core, false);
          size_t p1 = add_place(preempt_p1_name, 1);

          TimeInterval t2_interval = transitions[h_t_pn[3]].time_interval;
          size_t t2 = add_transition(preempt_t2_name, t2_interval,
                                     h_tc.priority, h_tc.core, false);

          set_pre_arc(h_entry, t1, 1);
          set_pre_arc(l_preempt_place, t1, 1);
          set_post_arc(t1, p1, 1);
          set_pre_arc(p1, t2, 1);
          set_post_arc(t2, h_exit, 1);
          set_post_arc(t2, l_preempt_place, 1);

          node_index++;
        } else {
          spdlog::debug("[MATRIX_PTPN] 正常任务抢占: {} 抢占 {}", h_t_name, l_t_name);

          std::string preempt_name = h_t_name + "_preempt_" + l_t_name + "_" +
                                     std::to_string(node_index);
          TimeInterval preempt_interval(0, 0);
          size_t preempt_trans = add_transition(
              preempt_name, preempt_interval, h_tc.priority, h_tc.core, false);

          set_pre_arc(h_entry, preempt_trans, 1);
          set_pre_arc(l_preempt_place, preempt_trans, 1);
          set_post_arc(preempt_trans, h_ready, 1);
          set_post_arc(preempt_trans, l_entry, 1);

          node_index++;
        }

        if (!l_tc.locks.empty()) {
          constexpr size_t MIN_CHAIN_LENGTH = 9;
          if (l_t_pn.size() < MIN_CHAIN_LENGTH) {
            spdlog::debug("[MATRIX_PTPN] 任务链长度不足,跳过锁抢占: {}", l_t_name);
            return;
          }

          for (size_t i = 0; i < l_tc.locks.size(); ++i) {
            if (l_tc.locks[i].find("spin") != std::string::npos) {
              break;
            }

            size_t idx = l_t_pn.size() - 2 - 2 * (i + 1);
            if (idx < transitions.size()) {
              transitions[idx].suspendable = true;
            }

            size_t lock_preempt_place = l_t_pn[idx - 1];

            if (is_interrupt) {
              std::string lock_preempt_t1_name =
                  h_t_name + "_lock_preempt_t1_" + std::to_string(node_index);
              std::string lock_preempt_p1_name =
                  h_t_name + "_lock_preempt_p1_" + std::to_string(node_index);
              std::string lock_preempt_t2_name =
                  h_t_name + "_lock_preempt_t2_" + std::to_string(node_index);

              TimeInterval lock_t1_interval(0, 0);
              size_t lock_t1 =
                  add_transition(lock_preempt_t1_name, lock_t1_interval,
                                 h_tc.priority, h_tc.core, false);
              size_t lock_p1 = add_place(lock_preempt_p1_name, 1);
              TimeInterval lock_t2_interval =
                  transitions[h_t_pn[3]].time_interval;
              size_t lock_t2 =
                  add_transition(lock_preempt_t2_name, lock_t2_interval,
                                 h_tc.priority, h_tc.core, false);

              set_pre_arc(h_entry, lock_t1, 1);
              set_pre_arc(lock_preempt_place, lock_t1, 1);
              set_post_arc(lock_t1, lock_p1, 1);
              set_pre_arc(lock_p1, lock_t2, 1);
              set_post_arc(lock_t2, h_exit, 1);
              set_post_arc(lock_t2, lock_preempt_place, 1);

              node_index++;
            } else {
              std::string lock_preempt_name = h_t_name + "_lock_preempt_" +
                                              l_t_name + "_" +
                                              std::to_string(node_index);
              TimeInterval lock_preempt_interval(0, 0);
              size_t lock_preempt_trans =
                  add_transition(lock_preempt_name, lock_preempt_interval,
                                 h_tc.priority, h_tc.core, false);

              set_pre_arc(h_entry, lock_preempt_trans, 1);
              set_pre_arc(lock_preempt_place, lock_preempt_trans, 1);
              set_post_arc(lock_preempt_trans, h_ready, 1);
              set_post_arc(lock_preempt_trans, lock_preempt_place, 1);

              node_index++;
            }
          }
        }
      };

  for (const auto& [core_id, tasks] : core_task) {
    spdlog::debug("[MATRIX_PTPN] 处理核心 {} 的抢占关系", core_id);

    for (size_t i = 0; i < tasks.size(); ++i) {
      for (size_t j = i + 1; j < tasks.size(); ++j) {
        const std::string& l_t_name = tasks[i];
        const std::string& h_t_name = tasks[j];

        auto l_t_it = tc.find(l_t_name);
        auto h_t_it = tc.find(h_t_name);

        if (l_t_it == tc.end() || h_t_it == tc.end()) {
          continue;
        }

        const TaskConfig& l_tc = l_t_it->second;
        const TaskConfig& h_tc = h_t_it->second;

        if (l_tc.priority == h_tc.priority) {
          continue;
        }

        auto l_t_pns_it = node_pn_map.find(l_t_name);
        auto h_t_pns_it = node_pn_map.find(h_t_name);

        if (l_t_pns_it == node_pn_map.end() ||
            h_t_pns_it == node_pn_map.end()) {
          spdlog::warn("[MATRIX_PTPN] 找不到任务链: {} 或 {}", l_t_name, h_t_name);
          continue;
        }

        const std::vector<size_t>& h_t_pn = h_t_pns_it->second;

        bool is_interrupt = false;
        auto node_type_it = nodes_type.find(h_t_name);
        if (node_type_it != nodes_type.end() &&
            std::holds_alternative<PeriodicTask>(node_type_it->second)) {
          const auto& p_task = std::get<PeriodicTask>(node_type_it->second);
          is_interrupt = (p_task.task_type == TaskType::INTERRUPT);
        }

        for (const auto& l_t_pn : {l_t_pns_it->second}) {
          handle_task_preemption(l_t_name, h_t_name, l_tc, h_tc, l_t_pn, h_t_pn,
                                 is_interrupt);
        }

        spdlog::debug("[MATRIX_PTPN] 抢占关系: {} (priority={}) <- {} (priority={})",
                      l_t_name, l_tc.priority, h_t_name, h_tc.priority);
      }
    }
  }

  info("[MATRIX_PTPN] 抢占任务添加完成");
}

}  // namespace matrix_ptpn