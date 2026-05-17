#include "tdg2pn/tdg2pn.h"

#include <spdlog/spdlog.h>

namespace converter {

static void info(const std::string& msg) {
  spdlog::info("[TDG2PN] {}", msg);
}

static void warn(const std::string& msg) {
  spdlog::warn("[TDG2PN] {}", msg);
}

static void error(const std::string& msg) {
  spdlog::error("[TDG2PN] {}", msg);
}

bool TDG2PN::has_non_self_successor(const tdg::TDG& tdg,
                                    const std::string& task_name) {
  for (const auto& edge : tdg.tdg_edges) {
    std::string source, target, label, style;
    std::tie(source, target, label, style) = edge;
    if (source == task_name && target != task_name) {
      return true;
    }
  }
  return false;
}

bool TDG2PN::has_self_loop_release(const tdg::TDG& tdg,
                                   const std::string& task_name) {
  for (const auto& edge : tdg.tdg_edges) {
    std::string source, target, label, style;
    std::tie(source, target, label, style) = edge;
    if (source == task_name && target == task_name) {
      return true;
    }
  }
  return false;
}

void TDG2PN::add_consume_transition(petri::PTPN& ptpn,
                                    const std::string& task_name,
                                    size_t end_idx) {
  petri::TimeInterval interval(0, 0);
  size_t consume_trans = ptpn.add_transition(task_name + "_consume",
                                             interval, 411, 411, false);
  ptpn.set_pre_arc(end_idx, consume_trans, 1);
}

void TDG2PN::add_start_bindings(petri::PTPN& ptpn, const tdg::TDG& tdg) {
  for (const auto& start_binding : tdg.start_tasks) {
    const auto node_it = ptpn.node_start_end_map.find(start_binding.task);
    if (node_it == ptpn.node_start_end_map.end()) {
      warn("[TDG2PN] Start task not found in node map: " + start_binding.task);
      continue;
    }
    if (start_binding.tokens <= 0) {
      continue;
    }
    ptpn.set_initial_marking(node_it->second.first, start_binding.tokens);
  }
}

void TDG2PN::add_end_consumers(petri::PTPN& ptpn, const tdg::TDG& tdg) {
  std::set<std::string> consume_tasks;

  for (const auto& [vertex_name, node_type] : tdg.nodes_type) {
    if (!std::holds_alternative<APeriodicTask>(node_type)) {
      continue;
    }
    if (!has_non_self_successor(tdg, vertex_name)) {
      consume_tasks.insert(vertex_name);
    }
  }

  consume_tasks.insert(tdg.end_tasks.begin(), tdg.end_tasks.end());

  for (const auto& task_name : consume_tasks) {
    const auto node_it = ptpn.node_start_end_map.find(task_name);
    if (node_it == ptpn.node_start_end_map.end()) {
      warn("[TDG2PN] End task not found in node map: " + task_name);
      continue;
    }
    add_consume_transition(ptpn, task_name, node_it->second.second);
  }
}

void TDG2PN::add_periodic_release_bindings(petri::PTPN& ptpn,
                                           const tdg::TDG& tdg) {
  for (const auto& task_name : tdg.periodic_tasks) {
    if (has_self_loop_release(tdg, task_name)) {
      continue;
    }

    const auto node_it = ptpn.node_start_end_map.find(task_name);
    const auto type_it = tdg.nodes_type.find(task_name);
    if (node_it == ptpn.node_start_end_map.end() || type_it == tdg.nodes_type.end()) {
      warn("[TDG2PN] Periodic task not found for release binding: " + task_name);
      continue;
    }

    if (!std::holds_alternative<PeriodicTask>(type_it->second)) {
      warn("[TDG2PN] Periodic release requested for non-periodic node: " + task_name);
      continue;
    }

    const auto& task = std::get<PeriodicTask>(type_it->second);
    size_t random = ptpn.add_place(task.name + "_cfg_random", 1);
    petri::TimeInterval fire_interval(task.period_time.first,
                                      task.period_time.second);
    size_t fire = ptpn.add_transition(task.name + "_cfg_fire", fire_interval,
                                      411, 411, false);

    ptpn.set_initial_marking(random, 1);
    ptpn.set_pre_arc(random, fire, 1);
    ptpn.set_post_arc(fire, random, 1);
    ptpn.set_post_arc(fire, node_it->second.first, 1);
  }
}

void TDG2PN::transform(const tdg::TDG& tdg, petri::PTPN& ptpn) {
  try {
    info("[TDG2PN] Starting TDG to PTPN transformation...");

    // Store reference data for priority classification
    ptpn.node_start_end_map.clear();
    ptpn.node_pn_map.clear();
    ptpn.cpus_place.clear();
    ptpn.locks_place.clear();
    ptpn.node_index = 0;

    // Build tasks_config from tdg
    std::unordered_map<std::string, TaskConfig> tasks_config;
    for (const auto& task : tdg.all_task) {
      if (std::holds_alternative<APeriodicTask>(task)) {
        auto result = std::get<APeriodicTask>(task);
        TaskConfig tc = {result.core, result.priority, result.time, result.lock};
        tasks_config.insert({result.name, tc});
      } else if (std::holds_alternative<PeriodicTask>(task)) {
        auto result = std::get<PeriodicTask>(task);
        TaskConfig tc = {result.core, result.priority, result.time, result.lock};
        tasks_config.insert({result.name, tc});
      }
    }

    info("[TDG2PN] Transforming vertices...");
    transform_vertices(ptpn, tdg);

    info("[TDG2PN] Transforming edges...");
    transform_edges(ptpn, tdg);

    add_start_bindings(ptpn, tdg);
    add_periodic_release_bindings(ptpn, tdg);
    add_end_consumers(ptpn, tdg);

    if (tdg.policy == SchedulePolicy::FIXED) {
      info("[TDG2PN] Creating priority preemption relations...");
      auto core_task = classify_tdg_priority(tdg);
      add_preempt_task_matrix(ptpn, core_task, tasks_config, tdg.nodes_type);
    } else {
      info("[TDG2PN] Skipping preemption expansion for non-fixed policy");
    }

    info("[TDG2PN] Adding resources and bindings...");
    add_resources_and_bindings_matrix(ptpn, tdg);

    spdlog::info("[TDG2PN] TDG transformation completed: {} places, {} transitions",
                 ptpn.places.size(), ptpn.transitions.size());

    if (!ptpn.verify_structure()) {
      warn("[TDG2PN] Structure verification failed, continuing anyway");
    }
  } catch (const std::exception& e) {
    spdlog::error("[TDG2PN] Failed to transform TDG to PTPN: {}", e.what());
    throw;
  }
}

std::unordered_map<int, std::vector<std::string>> TDG2PN::classify_tdg_priority(const tdg::TDG& tdg) {
  std::unordered_map<int, std::vector<std::string>> core_task;

  for (const auto& task : tdg.all_task) {
    if (std::holds_alternative<APeriodicTask>(task)) {
      const auto& result = std::get<APeriodicTask>(task);
      core_task[result.core].push_back(result.name);
    } else if (std::holds_alternative<PeriodicTask>(task)) {
      const auto& result = std::get<PeriodicTask>(task);
      core_task[result.core].push_back(result.name);
    }
  }

  for (auto& [core_id, tasks] : core_task) {
    std::sort(tasks.begin(), tasks.end(), [&](const std::string& t1, const std::string& t2) {
      return tdg.tasks_priority.at(t1) < tdg.tasks_priority.at(t2);
    });
  }

  for (auto& [fst, snd] : core_task) {
    std::stringstream ss;
    ss << "[TDG2PN] Core: " << fst << " [ ";
    for (const auto& task : snd) {
      ss << task << " < ";
    }
    ss << " ]";
    spdlog::info("{}", ss.str());
  }

  return core_task;
}

void TDG2PN::transform_vertices(petri::PTPN& ptpn, const tdg::TDG& tdg) {
  for (const auto& [vertex_name, node_type] : tdg.nodes_type) {
    spdlog::debug("[TDG2PN] Processing vertex: {}", vertex_name);

    try {
      auto [start_idx, end_idx] = add_node_matrix(ptpn, node_type);
      ptpn.node_start_end_map[vertex_name] = std::make_pair(start_idx, end_idx);

      bool is_leaf = true;
      for (const auto& edge : tdg.tdg_edges) {
        std::string source, target, label, style;
        std::tie(source, target, label, style) = edge;
        if (source == vertex_name && target != vertex_name) {
          is_leaf = false;
          break;
        }
      }

      if (is_leaf && std::holds_alternative<APeriodicTask>(node_type)) {
        spdlog::debug("[TDG2PN] Leaf aperiodic task will get consume transition later: {}", vertex_name);
      }
    } catch (const std::exception& e) {
      spdlog::error("[TDG2PN] Failed to transform vertex {}: {}", vertex_name, e.what());
      throw;
    }
  }
  info("[TDG2PN] Vertex transformation completed");
}

void TDG2PN::transform_edges(petri::PTPN& ptpn, const tdg::TDG& tdg) {
  for (const auto& edge : tdg.tdg_edges) {
    try {
      std::string source_name, target_name, label, style;
      std::tie(source_name, target_name, label, style) = edge;

      if (is_self_loop_edge(source_name, target_name)) {
        handle_self_loop_edge_matrix(ptpn, label, source_name);
        continue;
      }

      if (is_dashed_edge(style)) {
        handle_dashed_edge_matrix(ptpn, source_name, target_name);
        continue;
      }

      handle_normal_edge_matrix(ptpn, source_name, target_name);
    } catch (const std::exception& exception) {
      spdlog::error("[TDG2PN] Failed to transform edge: {}", exception.what());
      throw;
    }
  }
}

bool TDG2PN::is_self_loop_edge(const std::string& source, const std::string& target) {
  return source == target;
}

bool TDG2PN::is_dashed_edge(const std::string& edge) {
  return edge.find("dashed") != std::string::npos;
}

void TDG2PN::handle_self_loop_edge_matrix(petri::PTPN& ptpn,
                                          const std::string& label,
                                          const std::string& source_name) {
  int task_period_time = std::stoi(label);
  auto task_start_end = ptpn.node_start_end_map.find(source_name);
  if (task_start_end == ptpn.node_start_end_map.end()) {
    throw std::runtime_error("Start/end nodes not found for: " + source_name);
  }

  add_monitor_matrix(ptpn, source_name, task_period_time,
                     task_start_end->second.first,
                     task_start_end->second.second);
}

void TDG2PN::handle_dashed_edge_matrix(petri::PTPN& ptpn,
                                        const std::string& source_name,
                                        const std::string& target_name) {
  // Dashed edge: tail node is start, head node is end
  
}

void TDG2PN::handle_normal_edge_matrix(petri::PTPN& ptpn,
                                        const std::string& source_name,
                                        const std::string& target_name) {
  const auto source_it = ptpn.node_start_end_map.find(source_name);
  const auto target_it = ptpn.node_start_end_map.find(target_name);

  if (source_it == ptpn.node_start_end_map.end() ||
      target_it == ptpn.node_start_end_map.end()) {
    throw std::runtime_error("Node mapping not found for edge: " + source_name +
                             " -> " + target_name);
  }

  size_t source_node = source_it->second.second;
  size_t target_node = target_it->second.first;

  spdlog::debug("[TDG2PN] source_name: {}", source_name);
  spdlog::debug("[TDG2PN] target_name: {}", target_name);

  if (source_name.substr(0, 4) == "Dist" ||
      source_name.substr(0, 4) == "Wait") {
    if (source_node < ptpn.places.size()) {
      petri::TimeInterval interval(0, 0);
      size_t middle_trans = ptpn.add_transition(source_name + "_to_" + target_name,
                                               interval, 411, 411, false);
      ptpn.set_pre_arc(source_node, middle_trans, 1);
      ptpn.set_post_arc(middle_trans, target_node, 1);
    }
    return;
  }
  if (target_name.substr(0, 4) == "Dist" ||
      target_name.substr(0, 4) == "Wait") {
    if (source_node < ptpn.places.size()) {
      petri::TimeInterval interval(0, 0);
      size_t middle_trans = ptpn.add_transition(source_name + "_to_" + target_name,
                                               interval, 411, 411, false);
      ptpn.set_pre_arc(source_node, middle_trans, 1);
      ptpn.set_post_arc(middle_trans, target_node, 1);
    }
    return;
  }

  const std::string trans_name = source_name + "_to_" + target_name;
  petri::TimeInterval interval(0, 0);
  size_t middle_trans = ptpn.add_transition(trans_name, interval, 411, 411, false);

  if (source_node < ptpn.places.size() && target_node < ptpn.places.size()) {
    ptpn.set_pre_arc(source_node, middle_trans, 1);
    ptpn.set_post_arc(middle_trans, target_node, 1);
  } else {
    spdlog::warn("[TDG2PN] Unexpected node types for edge: {} -> {}", source_name, target_name);
  }

  spdlog::debug("[TDG2PN] Added edge: {} -> {}", source_name, target_name);
}

void TDG2PN::add_resources_and_bindings_matrix(petri::PTPN& ptpn, const tdg::TDG& tdg) {
  add_cpu_resource_matrix(ptpn, tdg.num_cpus, tdg.cores_per_cpu);
  add_lock_resource_matrix(ptpn, tdg.lock_set);
  task_bind_cpu_resource_matrix(ptpn, tdg.all_task);
  task_bind_lock_resource_matrix(ptpn, tdg.all_task, tdg.task_locks_map);
}

void TDG2PN::add_cpu_resource_matrix(petri::PTPN& ptpn, int cpus, int cores_per_cpu) {
  for (int i = 0; i < cpus; i++) {
    std::string cpu_name = "core" + std::to_string(i);
    size_t c = ptpn.add_place(cpu_name, cores_per_cpu);
    ptpn.cpus_place.push_back(c);
    ptpn.set_initial_marking(c, cores_per_cpu);
  }
  info("[TDG2PN] Created core resources!");
}

void TDG2PN::add_lock_resource_matrix(petri::PTPN& ptpn, const std::set<std::string>& locks_name) {
  if (locks_name.empty()) {
    info("[TDG2PN] TDG without locks!");
    return;
  }
  for (const auto& lock_name : locks_name) {
    size_t l = ptpn.add_place(lock_name, 1);
    ptpn.locks_place.insert(std::make_pair(lock_name, l));
    ptpn.set_initial_marking(l, 1);
  }
  info("[TDG2PN] Created lock resources!");
}

void TDG2PN::task_bind_cpu_resource_matrix(petri::PTPN& ptpn,
    const std::vector<NodeType>& all_task) {
  for (const auto& task : all_task) {
    if (std::holds_alternative<APeriodicTask>(task)) {
      auto ap_task = std::get<APeriodicTask>(task);
      const int cpu_index = ap_task.core;
      auto task_pt_chains = ptpn.node_pn_map.find(ap_task.name)->second;
      if (task_pt_chains.size() >= 2) {
        ptpn.set_pre_arc(ptpn.cpus_place[cpu_index], task_pt_chains[1], 1);
        if (task_pt_chains.size() >= 5) {
          ptpn.set_post_arc(task_pt_chains[task_pt_chains.size() - 2],
                            ptpn.cpus_place[cpu_index], 1);
        }
      }
    } else if (std::holds_alternative<PeriodicTask>(task)) {
      auto p_task = std::get<PeriodicTask>(task);
      const int cpu_index = p_task.core;
      auto task_pt_chains = ptpn.node_pn_map.find(p_task.name)->second;
      if (task_pt_chains.size() >= 2) {
        ptpn.set_pre_arc(ptpn.cpus_place[cpu_index], task_pt_chains[1], 1);
        if (task_pt_chains.size() >= 5) {
          ptpn.set_post_arc(task_pt_chains[task_pt_chains.size() - 2],
                            ptpn.cpus_place[cpu_index], 1);
        }
      }
    }
  }
}

void TDG2PN::task_bind_lock_resource_matrix(petri::PTPN& ptpn,
    const std::vector<NodeType>& all_task,
    const std::map<std::string, std::vector<std::string>>& task_locks) {
  if (task_locks.empty()) {
    info("[TDG2PN] No task locks to bind");
    return;
  }

  for (const auto& task : all_task) {
    try {
      if (std::holds_alternative<APeriodicTask>(task)) {
        const auto& ap_task = std::get<APeriodicTask>(task);
        if (auto chains_it = ptpn.node_pn_map.find(ap_task.name);
            chains_it != ptpn.node_pn_map.end()) {
          bind_task_locks_matrix(ptpn, ap_task.name, ap_task.lock, chains_it->second,
                                  task_locks);
        }
      } else if (std::holds_alternative<PeriodicTask>(task)) {
        const auto& p_task = std::get<PeriodicTask>(task);
        auto chains_it = ptpn.node_pn_map.find(p_task.name);
        if (chains_it != ptpn.node_pn_map.end()) {
          bind_task_locks_matrix(ptpn, p_task.name, p_task.lock, chains_it->second,
                                  task_locks);
        }
      }
    } catch (const std::exception& e) {
      spdlog::error("[TDG2PN] Failed to process task: {}", e.what());
      throw;
    }
  }

  info("[TDG2PN] Completed lock resource binding for all tasks");
}

void TDG2PN::bind_task_locks_matrix(petri::PTPN& ptpn,
    const std::string& task_name, const std::vector<std::string>& lock_types,
    const std::vector<size_t>& task_pt_chain,
    const std::map<std::string, std::vector<std::string>>& task_locks) {
  constexpr size_t MIN_CHAIN_LENGTH = 5;
  if (task_pt_chain.size() < MIN_CHAIN_LENGTH) {
    spdlog::debug("[TDG2PN] Skip chain for {}: too short for locks", task_name);
    return;
  }

  auto task_locks_it = task_locks.find(task_name);
  if (task_locks_it == task_locks.end()) {
    spdlog::warn("[TDG2PN] No locks found for task: {}", task_name);
    return;
  }

  const size_t lock_nums = task_locks_it->second.size();

  for (size_t i = 0; i < lock_nums; i++) {
    try {
      const std::string& lock_type = lock_types[i];

      const size_t get_lock = 5 + 4 * i;
      const size_t drop_lock = task_pt_chain.size() - 4 - 2 * i;

      auto lock_it = ptpn.locks_place.find(lock_type);
      if (lock_it == ptpn.locks_place.end()) {
        throw std::runtime_error("Lock place not found: " + lock_type);
      }
      const size_t lock = lock_it->second;

      if (get_lock >= task_pt_chain.size() || drop_lock >= task_pt_chain.size()) {
        throw std::runtime_error("Task chain layout does not match lock structure for: " + task_name);
      }

      const size_t get_lock_transition = task_pt_chain[get_lock];
      const size_t drop_lock_transition = task_pt_chain[drop_lock];

      if (get_lock_transition < ptpn.transitions.size()) {
        ptpn.set_pre_arc(lock, get_lock_transition, 1);
      }
      if (drop_lock_transition < ptpn.transitions.size()) {
        ptpn.set_post_arc(drop_lock_transition, lock, 1);
      }

      spdlog::debug("[TDG2PN] Bound lock {} to task {}", lock_type, task_name);
    } catch (const std::exception& e) {
      spdlog::error("[TDG2PN] Failed to bind lock {} for task {}: {}", i, task_name, e.what());
      throw;
    }
  }
}

std::pair<size_t, size_t> TDG2PN::add_node_matrix(petri::PTPN& ptpn,
    const NodeType& node_type) {
  if (std::holds_alternative<PeriodicTask>(node_type)) {
    auto p_task = std::get<PeriodicTask>(node_type);
    return add_p_node_matrix(ptpn, p_task);
  } else if (std::holds_alternative<APeriodicTask>(node_type)) {
    auto ap_task = std::get<APeriodicTask>(node_type);
    return add_ap_node_matrix(ptpn, ap_task);
  } else if (std::holds_alternative<JoinTask>(node_type)) {
    auto [name, time] = std::get<JoinTask>(node_type);
    petri::TimeInterval interval(0, 0);
    size_t sync_trans = ptpn.add_transition("Sync" + std::to_string(ptpn.node_index++),
                                            interval, 411, 411, false);
    return std::make_pair(sync_trans, sync_trans);
  } else if (std::holds_alternative<ForkTask>(node_type)) {
    auto [name, time] = std::get<ForkTask>(node_type);
    petri::TimeInterval interval(0, 0);
    size_t dist_trans = ptpn.add_transition("Dist" + std::to_string(ptpn.node_index++),
                                            interval, 411, 411, false);
    return std::make_pair(dist_trans, dist_trans);
  } else {
    auto [name] = std::get<EmptyTask>(node_type);
    size_t empty_place = ptpn.add_place("Empty" + std::to_string(ptpn.node_index++), 1);
    return std::make_pair(empty_place, empty_place);
  }
}

std::vector<size_t> TDG2PN::add_execution_chain(petri::PTPN& ptpn,
                                                const std::string& task_name,
                                                const std::vector<std::pair<int, int>>& times,
                                                const std::vector<std::string>& locks,
                                                int priority,
                                                int core) {
  if (times.empty()) {
    throw std::runtime_error("Task has no execution segments: " + task_name);
  }

  std::vector<size_t> chain;

  size_t entry = ptpn.add_place(task_name + "entry", 1);
  petri::TimeInterval get_core_interval(0, 0);
  size_t get_core = ptpn.add_transition(task_name + "get_core", get_core_interval,
                                        priority, core, false);
  size_t ready = ptpn.add_place(task_name + "ready", 1);

  ptpn.set_pre_arc(entry, get_core, 1);
  ptpn.set_post_arc(get_core, ready, 1);

  chain.push_back(entry);
  chain.push_back(get_core);
  chain.push_back(ready);

  size_t current_place = ready;

  for (size_t i = 0; i < times.size(); ++i) {
    const auto& [start, end] = times[i];
    std::string exec_name = times.size() == 1
        ? task_name + "exec"
        : task_name + "_exec_" + std::to_string(i + 1);
    petri::TimeInterval exec_interval(start, end);
    size_t exec = ptpn.add_transition(exec_name, exec_interval, priority, core, false);

    const bool is_last_segment = (i == times.size() - 1);
    std::string next_place_name = is_last_segment
        ? task_name + "exit"
        : task_name + "_seg_" + std::to_string(i + 1) + "_done";
    size_t next_place = ptpn.add_place(next_place_name, 1);

    ptpn.set_pre_arc(current_place, exec, 1);
    ptpn.set_post_arc(exec, next_place, 1);

    chain.push_back(exec);
    chain.push_back(next_place);
    current_place = next_place;

    if (i < locks.size()) {
      petri::TimeInterval lock_interval(0, 0);
      std::string lock_name = task_name + "_lock_" + std::to_string(i + 1);
      size_t lock_transition = ptpn.add_transition(lock_name, lock_interval, priority, core, false);
      size_t hold_place = ptpn.add_place(task_name + "_hold_" + std::to_string(i + 1), 1);

      ptpn.set_pre_arc(current_place, lock_transition, 1);
      ptpn.set_post_arc(lock_transition, hold_place, 1);

      chain.push_back(lock_transition);
      chain.push_back(hold_place);
      current_place = hold_place;
    }
  }

  return chain;
}

std::pair<size_t, size_t> TDG2PN::add_p_node_matrix(petri::PTPN& ptpn, PeriodicTask& p_task) {
  std::vector<size_t> chain = add_execution_chain(ptpn, p_task.name, p_task.time,
                                                  p_task.lock, p_task.priority, p_task.core);
  size_t entry = chain.front();
  size_t exit = chain.back();

  ptpn.node_pn_map[p_task.name] = chain;

  return std::make_pair(entry, exit);
}

std::pair<size_t, size_t> TDG2PN::add_ap_node_matrix(petri::PTPN& ptpn, APeriodicTask& ap_task) {
  std::vector<size_t> chain = add_execution_chain(ptpn, ap_task.name, ap_task.time,
                                                  ap_task.lock, ap_task.priority, ap_task.core);
  size_t entry = chain.front();
  size_t exit = chain.back();

  ptpn.node_pn_map[ap_task.name] = chain;

  return std::make_pair(entry, exit);
}

void TDG2PN::add_monitor_matrix(petri::PTPN& ptpn, const std::string& task_name,
                                 int task_period_time, size_t start, size_t end) {
  size_t deadline = ptpn.add_place(task_name + "deadline", 1);
  size_t timeout = ptpn.add_place(task_name + "timeout", 1);
  size_t ok = ptpn.add_place(task_name + "ok", 1);
  size_t t_end = ptpn.add_place(task_name + "end", 1);

  petri::TimeInterval timed_interval(task_period_time, task_period_time);
  size_t timed = ptpn.add_transition(task_name + "timed", timed_interval, 411, 411, false);
  size_t ending = ptpn.add_transition(task_name + "ending", petri::TimeInterval(0, 0), 411, 411, false);
  size_t complete = ptpn.add_transition(task_name + "complete", petri::TimeInterval(0, 0), 411, 411, false);
  size_t tout = ptpn.add_transition(task_name + "out", petri::TimeInterval(0, 0), 411, 411, false);

  ptpn.set_post_arc(ending, t_end, 1);
  ptpn.set_pre_arc(t_end, complete, 1);
  ptpn.set_post_arc(complete, ok, 1);
  ptpn.set_pre_arc(deadline, ok, 1);
  ptpn.set_pre_arc(deadline, tout, 1);
  ptpn.set_post_arc(tout, timeout, 1);

  if (end < ptpn.places.size()) {
    ptpn.set_pre_arc(end, ending, 1);
  } else {
    spdlog::warn("[TDG2PN] end node is transition, cannot add monitor edge");
  }

  if (start < ptpn.places.size()) {
    ptpn.set_pre_arc(start, timed, 1);
    ptpn.set_post_arc(timed, deadline, 1);
  }
}

void TDG2PN::add_preempt_task_matrix(
    petri::PTPN& ptpn,
    const std::unordered_map<int, std::vector<std::string>>& core_task,
    const std::unordered_map<std::string, TaskConfig>& tc,
    const std::unordered_map<std::string, NodeType>& nodes_type) {
  info("[TDG2PN] Starting preemption task addition...");

  auto handle_task_preemption =
      [&](const std::string& l_t_name, const std::string& h_t_name,
          const TaskConfig& l_tc, const TaskConfig& h_tc,
          const std::vector<size_t>& l_t_pn, const std::vector<size_t>& h_t_pn,
          bool is_interrupt) {
        if (l_t_pn.size() < 5 || h_t_pn.size() < 5) {
          spdlog::warn("[TDG2PN] Task chain too short, skipping preemption: {} <- {}", l_t_name, h_t_name);
          return;
        }

        size_t l_exec = l_t_pn[3];
        if (l_exec < ptpn.transitions.size()) {
          ptpn.transitions[l_exec].suspendable = true;
        }

        size_t l_entry = l_t_pn[0];
        size_t l_preempt_place = l_t_pn[2];
        size_t h_entry = h_t_pn[0];
        size_t h_ready = h_t_pn[2];
        size_t h_exit = h_t_pn[4];

        if (is_interrupt) {
          spdlog::debug("[TDG2PN] Interrupt task preemption: {} preempts {}", h_t_name, l_t_name);

          std::string preempt_t1_name = h_t_name + "_preempt_t1_" + std::to_string(ptpn.node_index);
          std::string preempt_p1_name = h_t_name + "_preempt_p1_" + std::to_string(ptpn.node_index);
          std::string preempt_t2_name = h_t_name + "_preempt_t2_" + std::to_string(ptpn.node_index);

          petri::TimeInterval t1_interval(0, 0);
          size_t t1 = ptpn.add_transition(preempt_t1_name, t1_interval, h_tc.priority, h_tc.core, false);
          size_t p1 = ptpn.add_place(preempt_p1_name, 1);

          petri::TimeInterval t2_interval = ptpn.transitions[h_t_pn[3]].time_interval;
          size_t t2 = ptpn.add_transition(preempt_t2_name, t2_interval, h_tc.priority, h_tc.core, false);

          ptpn.set_pre_arc(h_entry, t1, 1);
          ptpn.set_pre_arc(l_preempt_place, t1, 1);
          ptpn.set_post_arc(t1, p1, 1);
          ptpn.set_pre_arc(p1, t2, 1);
          ptpn.set_post_arc(t2, h_exit, 1);
          ptpn.set_post_arc(t2, l_preempt_place, 1);

          ptpn.node_index++;
        } else {
          spdlog::debug("[TDG2PN] Normal task preemption: {} preempts {}", h_t_name, l_t_name);

          std::string preempt_name = h_t_name + "_preempt_" + l_t_name + "_" +
                                      std::to_string(ptpn.node_index);
          petri::TimeInterval preempt_interval(0, 0);
          size_t preempt_trans = ptpn.add_transition(preempt_name, preempt_interval, h_tc.priority, h_tc.core, false);

          ptpn.set_pre_arc(h_entry, preempt_trans, 1);
          ptpn.set_pre_arc(l_preempt_place, preempt_trans, 1);
          ptpn.set_post_arc(preempt_trans, h_ready, 1);
          ptpn.set_post_arc(preempt_trans, l_entry, 1);

          ptpn.node_index++;
        }

        if (!l_tc.locks.empty()) {
          constexpr size_t MIN_CHAIN_LENGTH = 9;
          if (l_t_pn.size() < MIN_CHAIN_LENGTH) {
            spdlog::debug("[TDG2PN] Task chain too short for lock preemption: {}", l_t_name);
            return;
          }

          for (size_t i = 0; i < l_tc.locks.size(); ++i) {
            if (l_tc.locks[i].find("spin") != std::string::npos) {
              break;
            }

            size_t idx = l_t_pn.size() - 2 - 2 * (i + 1);
            if (idx < ptpn.transitions.size()) {
              ptpn.transitions[idx].suspendable = true;
            }

            size_t lock_preempt_place = l_t_pn[idx - 1];

            if (is_interrupt) {
              std::string lock_preempt_t1_name = h_t_name + "_lock_preempt_t1_" + std::to_string(ptpn.node_index);
              std::string lock_preempt_p1_name = h_t_name + "_lock_preempt_p1_" + std::to_string(ptpn.node_index);
              std::string lock_preempt_t2_name = h_t_name + "_lock_preempt_t2_" + std::to_string(ptpn.node_index);

              petri::TimeInterval lock_t1_interval(0, 0);
              size_t lock_t1 = ptpn.add_transition(lock_preempt_t1_name, lock_t1_interval, h_tc.priority, h_tc.core, false);
              size_t lock_p1 = ptpn.add_place(lock_preempt_p1_name, 1);
              petri::TimeInterval lock_t2_interval = ptpn.transitions[h_t_pn[3]].time_interval;
              size_t lock_t2 = ptpn.add_transition(lock_preempt_t2_name, lock_t2_interval, h_tc.priority, h_tc.core, false);

              ptpn.set_pre_arc(h_entry, lock_t1, 1);
              ptpn.set_pre_arc(lock_preempt_place, lock_t1, 1);
              ptpn.set_post_arc(lock_t1, lock_p1, 1);
              ptpn.set_pre_arc(lock_p1, lock_t2, 1);
              ptpn.set_post_arc(lock_t2, h_exit, 1);
              ptpn.set_post_arc(lock_t2, lock_preempt_place, 1);

              ptpn.node_index++;
            } else {
              std::string lock_preempt_name = h_t_name + "_lock_preempt_" + l_t_name + "_" +
                                              std::to_string(ptpn.node_index);
              petri::TimeInterval lock_preempt_interval(0, 0);
              size_t lock_preempt_trans = ptpn.add_transition(lock_preempt_name, lock_preempt_interval, h_tc.priority, h_tc.core, false);

              ptpn.set_pre_arc(h_entry, lock_preempt_trans, 1);
              ptpn.set_pre_arc(lock_preempt_place, lock_preempt_trans, 1);
              ptpn.set_post_arc(lock_preempt_trans, h_ready, 1);
              ptpn.set_post_arc(lock_preempt_trans, lock_preempt_place, 1);

              ptpn.node_index++;
            }
          }
        }
      };

  for (const auto& [core_id, tasks] : core_task) {
    spdlog::debug("[TDG2PN] Processing preemption for core {}", core_id);

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

        auto l_t_pns_it = ptpn.node_pn_map.find(l_t_name);
        auto h_t_pns_it = ptpn.node_pn_map.find(h_t_name);

        if (l_t_pns_it == ptpn.node_pn_map.end() ||
            h_t_pns_it == ptpn.node_pn_map.end()) {
          spdlog::warn("[TDG2PN] Cannot find task chain: {} or {}", l_t_name, h_t_name);
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
          handle_task_preemption(l_t_name, h_t_name, l_tc, h_tc, l_t_pn, h_t_pn, is_interrupt);
        }

        spdlog::debug("[TDG2PN] Preemption: {} (priority={}) <- {} (priority={})",
                      l_t_name, l_tc.priority, h_t_name, h_tc.priority);
      }
    }
  }

  info("[TDG2PN] Preemption tasks added");
}

}  // namespace converter