//
// Created by 张凯文 on 2024/3/22.
//
#include "priority_time_petri_net.h"

// TODO: need rebuild
vertex_ptpn add_place(PTPN &pn, const string &name, int token) {
  PTPNVertex element = {name, token};
  return boost::add_vertex(element, pn);
}
// TODO: need rebuild
vertex_ptpn add_transition(PTPN &pn, std::string name, PTPNTransition pnt) {
  PTPNVertex element = {name, pnt};
  return boost::add_vertex(element, pn);
}

void PriorityTimePetriNet::init() {
  ptpn_dp.property("node_id", get(&PTPNVertex::name, ptpn));
  ptpn_dp.property("label", get(&PTPNVertex::label, ptpn));
  ptpn_dp.property("shape", get(&PTPNVertex::shape, ptpn));
  ptpn_dp.property("label", get(&PTPNEdge::label, ptpn));

  boost::ref_property_map<PTPN *, std::string> gname_pn(
      get_property(ptpn, boost::graph_name));
  ptpn_dp.property("name", gname_pn);
}

// 转换主函数
// 首先对所有节点进行转换
// 然后绑定每个任务的 CPU
// 根据优先级,建立抢占变迁
// 绑定锁
void PriorityTimePetriNet::transform_tdg_to_ptpn(TDG &tdg) {
  BOOST_FOREACH (TDG_RAP::vertex_descriptor v, vertices(tdg.tdg)) {
    // 根据保存用的类型进行转换
    BOOST_LOG_TRIVIAL(debug) << tdg.tdg[v].name;
    NodeType node_type = tdg.nodes_type.find(tdg.tdg[v].name)->second;
    auto res = add_node_ptpn(node_type);
  }
  BOOST_LOG_TRIVIAL(info) << "Transform Vertex Complete!";
  // 遍历边
  BOOST_FOREACH (TDG_RAP::edge_descriptor e, edges(tdg.tdg)) {
    auto source_name = tdg.tdg[source(e, tdg.tdg)].name;
    auto target_name = tdg.tdg[target(e, tdg.tdg)].name;
    // 前后节点相同的边为
    if (source_name == target_name) {
      // 找到前后节点,建立看门狗网结构
      int task_period_time = stoi(tdg.tdg[e].label);
      auto task_start_end = node_start_end_map.find(source_name)->second;
      add_monitor_ptpn(source_name, task_period_time, task_start_end.first,
                       task_start_end.second);
      continue;
    }
    // 边类型为虚线
    if (tdg.tdg[e].style.find("dashed") != string::npos) {
      // 虚线链接的尾节点为开始节点,头节点为结束节点

      continue;
    }
    // 普通边链接
    auto source_node = node_start_end_map[source_name].second;
    auto target_node = node_start_end_map[target_name].first;
    add_edge(source_node, target_node, ptpn);
    BOOST_LOG_TRIVIAL(debug) << "Edge: " << source_node << "->" << target_node;
  }
  add_cpu_resource(6);
  add_lock_resource(tdg.lock_set);
  task_bind_cpu_resource(tdg.all_task);
  task_bind_lock_resource(tdg.all_task, tdg.task_locks_map);
  BOOST_LOG_TRIVIAL(info) << "petri net P+T num: " << boost::num_vertices(ptpn);
  BOOST_LOG_TRIVIAL(info) << "petri net F num: " << boost::num_edges(ptpn);
}

void PriorityTimePetriNet::add_lock_resource(const set<string> &locks_name) {
  if (locks_name.empty()) {
    BOOST_LOG_TRIVIAL(info) << "TDG_RAP without locks!";
    return;
  }
  for (const auto &lock_name : locks_name) {
    PTPNVertex lock_place = {lock_name, 1};
    vertex_ptpn l = add_vertex(lock_place, ptpn);
    locks_place.insert(make_pair(lock_name, l));
  }
  BOOST_LOG_TRIVIAL(info) << "create lock resource!";
}

void PriorityTimePetriNet::add_cpu_resource(int nums) {
  for (int i = 0; i < nums; i++) {
    string cpu_name = "core" + std::to_string(i);
    // vertex_tpn c = add_vertex(PetriNetElement{core_name, core_name, "circle",
    // 1}, time_petri_net);
    PTPNVertex cpu_place = {cpu_name, 1};
    vertex_ptpn c = add_vertex(cpu_place, ptpn);
    cpus_place.push_back(c);
  }
  BOOST_LOG_TRIVIAL(info) << "create core resource!";
}

// 根据任务类型绑定不同位置的 CPU
void PriorityTimePetriNet::task_bind_cpu_resource(vector<NodeType> &all_task) {
  for (const auto &task : all_task) {
    if (holds_alternative<APeriodicTask>(task)) {
      auto ap_task = get<APeriodicTask>(task);
      int cpu_index = ap_task.core;
      auto task_pt_chains = node_pn_map.find(ap_task.name)->second;
      // Random -> Trigger -> Start -> Get CPU -> Run -> Drop CPU -> End
      add_edge(cpus_place[cpu_index], task_pt_chains[3], ptpn);
      add_edge(task_pt_chains[task_pt_chains.size() - 2], cpus_place[cpu_index],
               ptpn);
    } else if (holds_alternative<PeriodicTask>(task)) {
      auto p_task = get<PeriodicTask>(task);
      int cpu_index = p_task.core;
      auto task_pt_chains = node_pn_map.find(p_task.name)->second;
      // Start -> Get CPU -> Run -> Drop CPU -> End
      add_edge(cpus_place[cpu_index], task_pt_chains[1], ptpn);
      add_edge(task_pt_chains[task_pt_chains.size() - 2], cpus_place[cpu_index],
               ptpn);
    } else {
      continue;
    }
  }
}

// 根据任务种锁的数量和类型绑定
void PriorityTimePetriNet::task_bind_lock_resource(
    vector<NodeType> &all_task, std::map<string, vector<string>> &task_locks) {
  for (const auto &task : all_task) {
    if (holds_alternative<APeriodicTask>(task)) {
      auto ap_task = get<APeriodicTask>(task);
      int cpu_index = ap_task.core;
      auto all_task_pt_chains = task_pn_map.find(ap_task.name)->second;
      for (const auto &task_pt_chain : all_task_pt_chains) {
        if (task_pt_chain.size() < 7) {
          // 7 为不含锁的 P-T 链长度
          break;
        } else {
          size_t lock_nums = task_locks.find(ap_task.name)->second.size();
          for (int i = 0; i < lock_nums; i++) {
            // 按照锁的次序获得锁的类型
            std::string lock_type = ap_task.lock[i];
            vertex_ptpn get_lock = task_pt_chain[(5 + 2 * i)];
            vertex_ptpn drop_lock =
                task_pt_chain[(task_pt_chain.size() - 4 - 2 * i)];
            vertex_ptpn lock = locks_place.find(lock_type)->second;
            add_edge(lock, get_lock, ptpn);
            add_edge(drop_lock, lock, ptpn);
          }
        }
      }
    } else if (holds_alternative<PeriodicTask>(task)) {
      auto p_task = get<PeriodicTask>(task);
      int cpu_index = p_task.core;
      auto all_task_pt_chains = task_pn_map.find(p_task.name)->second;
      for (const auto &task_pt_chain : all_task_pt_chains) {
        if (task_pt_chain.size() < 7) {
          // 7 为不含锁的 P-T 链长度
          break;
        } else {
          size_t lock_nums = task_locks.find(p_task.name)->second.size();
          for (int i = 0; i < lock_nums; i++) {
            // 按照锁的次序获得锁的类型
            std::string lock_type = p_task.lock[i];
            vertex_ptpn get_lock = task_pt_chain[(3 + 2 * i)];
            vertex_ptpn drop_lock =
                task_pt_chain[(task_pt_chain.size() - 4 - 2 * i)];
            vertex_ptpn lock = locks_place.find(lock_type)->second;
            add_edge(lock, get_lock, ptpn);
            add_edge(drop_lock, lock, ptpn);
          }
        }
      }
    } else {
      continue;
    }
  }
}

// 映射规则主函数,包含不同类型,便于之后扩展
// 根据不同类型进行相应的转换,并返回转换后的开始和结束节点,以进行前后链接
pair<vertex_ptpn, vertex_ptpn> PriorityTimePetriNet::add_node_ptpn(
    NodeType node_type) {
  if (holds_alternative<PeriodicTask>(node_type)) {
    PeriodicTask p_task = get<PeriodicTask>(node_type);
    auto result = add_p_node_ptpn(p_task);
    node_start_end_map.insert(make_pair(p_task.name, result));
    BOOST_LOG_TRIVIAL(info)
        << p_task.name << "'s petri net start node: " << result.first
        << " end node: " << result.second;
    return result;
  } else if (holds_alternative<APeriodicTask>(node_type)) {
    APeriodicTask ap_task = get<APeriodicTask>(node_type);
    auto result = add_ap_node_ptpn(ap_task);
    node_start_end_map.insert(make_pair(ap_task.name, result));
    BOOST_LOG_TRIVIAL(info)
        << ap_task.name << "'s petri net start node: " << result.first
        << " end node: " << result.second;
    return result;
  } else if (holds_alternative<SyncTask>(node_type)) {
    SyncTask ap_task = get<SyncTask>(node_type);
    string t_name = ap_task.name;
    vertex_ptpn result =
        add_transition(ptpn, "Sync" + to_string(node_index),
                       PTPNTransition{100, make_pair(0, 0), INT_MAX});
    node_index += 1;
    node_start_end_map.insert(make_pair(t_name, make_pair(result, result)));
    BOOST_LOG_TRIVIAL(info) << t_name << ": "
                            << "type: SYNC";
    return {result, result};
  } else if (holds_alternative<DistTask>(node_type)) {
    DistTask ap_task = get<DistTask>(node_type);
    string t_name = ap_task.name;
    vertex_ptpn result =
        add_transition(ptpn, "Dist" + to_string(node_index),
                       PTPNTransition{100, make_pair(0, 0), INT_MAX});
    node_index += 1;
    node_start_end_map.insert(make_pair(t_name, make_pair(result, result)));
    BOOST_LOG_TRIVIAL(info) << t_name << ": "
                            << "type: DIST";
    return {result, result};
  } else {
    EmptyTask ap_task = get<EmptyTask>(node_type);
    string t_name = ap_task.name;
    vertex_ptpn result = add_place(ptpn, "Empty" + to_string(node_index), 0);
    node_index += 1;
    node_start_end_map.insert(make_pair(t_name, make_pair(result, result)));
    BOOST_LOG_TRIVIAL(info) << t_name << ": "
                            << "type: EMPTY";
    return {result, result};
  }
}

// 针对非周期的偶发任务和中断任务建模
pair<vertex_ptpn, vertex_ptpn> PriorityTimePetriNet::add_ap_node_ptpn(
    APeriodicTask &ap_task) {
  pair<vertex_ptpn, vertex_ptpn> result;
  string t_name = ap_task.name;
  string task_random_period = t_name + "random";
  string task_fire = t_name + "fire";
  string task_entry = t_name + "entry";
  string task_get = t_name + "get_core";
  string task_ready = t_name + "ready";
  string task_lock = t_name + "get_lock";
  string task_deal = t_name + "deal";
  string task_drop = t_name + "drop_lock";
  string task_unlock = t_name + "unlocked";
  string task_exec = t_name + "exec";
  string task_exit = t_name + "exit";

  // 保存该任务的转换节点序列,即 P-T 链
  vector<vertex_ptpn> task_pt_chain;
  vector<vector<vertex_ptpn>> task_pt_chains;
  // 任务属性结构
  int task_priority = ap_task.priority;
  int task_core = ap_task.core;
  // 任务临界区外执行时间
  auto task_exec_time = ap_task.time[(ap_task.time.size() - 1)];

  vertex_ptpn random = add_place(ptpn, task_random_period, 1);
  vertex_ptpn fire = add_transition(
      ptpn, task_fire,
      PTPNTransition{task_priority, make_pair(0, ap_task.period), task_core});
  vertex_ptpn entry = add_place(ptpn, task_entry, 0);
  vertex_ptpn get_core = add_transition(
      ptpn, task_get,
      PTPNTransition{task_priority, std::make_pair(0, 0), task_core});
  vertex_ptpn ready = add_place(ptpn, task_ready, 0);

  vertex_ptpn exec =
      add_transition(ptpn, task_exec,
                     PTPNTransition{task_priority, task_exec_time, task_core});
  vertex_ptpn exit = add_place(ptpn, task_exit, 0);

  add_edge(random, fire, ptpn);
  add_edge(fire, entry, ptpn);
  add_edge(entry, get_core, ptpn);
  add_edge(get_core, ready, ptpn);
  add_edge(exec, exit, ptpn);
  //  task_pt_chain.push_back(random);
  //  task_pt_chain.push_back(fire);
  task_pt_chain.push_back(entry);
  task_pt_chain.push_back(get_core);
  task_pt_chain.push_back(ready);
  result.first = random;
  result.second = exit;
  if (ap_task.lock.empty()) {
    add_edge(ready, exec, ptpn);
  } else {
    size_t lock_nums = ap_task.lock.size();
    for (int i = 0; i < lock_nums; i++) {
      auto lock_name = ap_task.lock[i];
      vertex_ptpn get_lock =
          add_transition(ptpn, task_lock.append(lock_name),
                         PTPNTransition{task_priority, {0, 0}, task_core});
      vertex_ptpn deal = add_place(ptpn, task_deal + lock_name, 0);
      add_edge(task_pt_chain.back(), get_lock, ptpn);
      task_pt_chain.push_back(get_lock);
      task_pt_chain.push_back(deal);
      add_edge(get_lock, deal, ptpn);
    }
    // 释放锁的步骤
    for (int j = (int)(lock_nums - 1); j >= 0; j--) {
      auto lock_name = ap_task.lock[j];
      auto t = ap_task.time[j];
      vertex_ptpn drop_lock =
          add_transition(ptpn, task_drop.append(lock_name),
                         PTPNTransition{task_priority, t, task_core});
      vertex_ptpn unlocked = add_place(ptpn, task_unlock + lock_name, 0);
      add_edge(task_pt_chain.back(), drop_lock, ptpn);
      add_edge(drop_lock, unlocked, ptpn);
      if (lock_nums == 1) {
        // add_edge(link, drop_lock, time_petri_net);
        add_edge(unlocked, exec, ptpn);
      } else if (j == 0) {
        add_edge(unlocked, exec, ptpn);
      }
      task_pt_chain.push_back(drop_lock);
      task_pt_chain.push_back(unlocked);
    }
  }
  task_pt_chain.push_back(exec);
  task_pt_chain.push_back(exit);
  task_pt_chains.push_back(task_pt_chain);

  // Record每个任务的映射节点
  node_pn_map.insert(make_pair(t_name, task_pt_chain));
  task_pn_map.insert(make_pair(t_name, task_pt_chains));
  return result;
}

// 对于普通任务或者带周期的任务建模
// 普通任务和周期任务的区别在于是否有截止时间约束
pair<vertex_ptpn, vertex_ptpn> PriorityTimePetriNet::add_p_node_ptpn(
    PeriodicTask &p_task) {
  pair<vertex_ptpn, vertex_ptpn> result;
  string t_name = p_task.name;
  string task_entry = t_name + "entry";
  string task_get = t_name + "get_core";
  string task_ready = t_name + "ready";
  string task_lock = t_name + "get_lock";
  string task_deal = t_name + "deal";
  string task_drop = t_name + "drop_lock";
  string task_unlock = t_name + "unlocked";
  string task_exec = t_name + "exec";
  string task_exit = t_name + "exit";

  // 保存该任务的转换节点序列,即 P-T 链
  vector<vertex_ptpn> task_pt_chain;
  vector<vector<vertex_ptpn>> task_pt_chains;
  // 任务属性结构
  int task_priority = p_task.priority;
  int task_core = p_task.core;
  // 任务临界区外执行时间
  auto task_exec_time = p_task.time[(p_task.time.size() - 1)];

  vertex_ptpn entry = add_place(ptpn, task_entry, 0);
  vertex_ptpn get_core = add_transition(
      ptpn, task_get,
      PTPNTransition{task_priority, std::make_pair(0, 0), task_core});
  vertex_ptpn ready = add_place(ptpn, task_ready, 0);

  vertex_ptpn exec =
      add_transition(ptpn, task_exec,
                     PTPNTransition{task_priority, task_exec_time, task_core});
  vertex_ptpn exit = add_place(ptpn, task_exit, 0);

  add_edge(entry, get_core, ptpn);
  add_edge(get_core, ready, ptpn);
  add_edge(exec, exit, ptpn);
  task_pt_chain.push_back(entry);
  task_pt_chain.push_back(get_core);
  task_pt_chain.push_back(ready);
  result.first = entry;
  result.second = exit;
  if (p_task.lock.empty()) {
    add_edge(ready, exec, ptpn);
  } else {
    int lock_nums = (int)p_task.lock.size();
    for (int i = 0; i < lock_nums; i++) {
      auto lock_name = p_task.lock[i];
      vertex_ptpn get_lock =
          add_transition(ptpn, task_lock.append(lock_name),
                         PTPNTransition{task_priority, {0, 0}, task_core});
      vertex_ptpn deal = add_place(ptpn, task_deal + lock_name, 0);
      add_edge(task_pt_chain.back(), get_lock, ptpn);
      task_pt_chain.push_back(get_lock);
      task_pt_chain.push_back(deal);
      add_edge(get_lock, deal, ptpn);
    }
    // 释放锁的步骤
    for (int j = (lock_nums - 1); j >= 0; j--) {
      auto lock_name = p_task.lock[j];
      auto t = p_task.time[j];
      vertex_ptpn drop_lock =
          add_transition(ptpn, task_drop.append(lock_name),
                         PTPNTransition{task_priority, t, task_core});
      vertex_ptpn unlocked = add_place(ptpn, task_unlock + lock_name, 0);
      add_edge(task_pt_chain.back(), drop_lock, ptpn);
      add_edge(drop_lock, unlocked, ptpn);
      if (lock_nums == 1) {
        add_edge(unlocked, exec, ptpn);
      } else if (j == 0) {
        add_edge(unlocked, exec, ptpn);
      }
      task_pt_chain.push_back(drop_lock);
      task_pt_chain.push_back(unlocked);
    }
  }
  task_pt_chain.push_back(exec);
  task_pt_chain.push_back(exit);
  task_pt_chains.push_back(task_pt_chain);

  // Record每个任务的映射节点
  node_pn_map.insert(make_pair(t_name, task_pt_chain));
  task_pn_map.insert(make_pair(t_name, task_pt_chains));
  return result;
}

// 增加看门狗子网结构,只链接每个周期任务的开始和结束节点
// 检测每个路径(包括抢占路径)的任务执行流
void PriorityTimePetriNet::add_monitor_ptpn(const string &task_name,
                                            int task_period_time,
                                            vertex_ptpn start,
                                            vertex_ptpn end) {
  // namespace
  string place_deadline = task_name + "deadline";
  string place_timeout = task_name + "timeout";
  string place_ok = task_name + "ok";
  string place_t_end = task_name + "end";

  string transition_t_ending = task_name + "ending";
  string transition_timed = task_name + "timed";
  string transition_t_ok = task_name + "complete";
  string transition_t_out = task_name + "out";

  auto period_time = make_pair(task_period_time, task_period_time);
  vertex_ptpn task_timed = add_transition(
      ptpn, transition_timed, PTPNTransition{256, period_time, 256});
  vertex_ptpn task_deadline = add_place(ptpn, place_deadline, 0);
  // 下面两个变迁在255 处理器上, ok的优先级高于 out, 以表示到达周期后优先触发 ok

  vertex_ptpn task_complete =
      add_transition(ptpn, transition_t_ok, PTPNTransition{256, {0, 0}, 255});
  vertex_ptpn task_tout =
      add_transition(ptpn, transition_t_out, PTPNTransition{255, {0, 0}, 255});

  vertex_ptpn task_tend = add_transition(ptpn, transition_t_ending,
                                         PTPNTransition{256, {0, 0}, 256});
  vertex_ptpn task_t_end = add_place(ptpn, place_t_end, 0);
  vertex_ptpn task_ok = add_place(ptpn, place_ok, 0);
  vertex_ptpn task_timeout = add_place(ptpn, place_timeout, 0);

  add_edge(end, task_tend, ptpn);
  add_edge(task_tend, task_t_end, ptpn);
  add_edge(task_t_end, task_complete, ptpn);
  add_edge(task_complete, task_ok, ptpn);
  add_edge(task_deadline, task_ok, ptpn);

  add_edge(task_deadline, task_tout, ptpn);
  add_edge(task_tout, task_timeout, ptpn);
  add_edge(start, task_timed, ptpn);
  add_edge(task_timed, task_deadline, ptpn);
}

// 为每个任务添加抢占的执行序列
// 根据任务的分配的处理器资源,对每个处理器上的任务从低优先级开始 (t1, t2,
// t3,...) t2 抢占 t1, t3 抢占 t1 和 t2,以此类推
void PriorityTimePetriNet::add_preempt_task_ptpn(
    const std::unordered_map<int, vector<string>> &core_task,
    const std::unordered_map<string, TaskConfig> &tc,
    const std::unordered_map<string, NodeType> &nodes_type) {
  for (auto c_task : core_task) {
    BOOST_LOG_TRIVIAL(debug) << "core index: " << c_task.first;
    for (auto l_t = c_task.second.begin(); l_t != c_task.second.end() - 1;
         l_t++) {
      vector<string>::iterator h_t;
      string l_t_name = *l_t;
      TaskConfig l_tc = tc.find(*l_t)->second;
      TaskConfig h_tc = tc.find(*h_t)->second;
      for (h_t = l_t + 1; h_t != c_task.second.end(); h_t++) {
        string h_t_name = *h_t;
        if (l_tc.priority == h_tc.priority) {
          continue;
        }
        BOOST_LOG_TRIVIAL(debug) << "priority " << *l_t << ": " << l_tc.priority
                                 << *h_t << ": " << h_tc.priority;
        auto l_t_pns = task_pn_map.find(*l_t);
        auto h_t_pns = task_pn_map.find(*h_t);

        // 对低优先级的任务的每条执行流进行抢占
        for (auto l_t_pn : l_t_pns->second) {
          int task_pn_size = l_t_pn.size();
          ptpn[l_t_pn[task_pn_size - 2]].pnt.is_handle = true;
          create_task_priority(l_t_name, l_t_pn[task_pn_size - 3],
                               l_t_pn[task_pn_size - 2], l_t_pn[0],
                               l_t_pn.back(), nodes_type.find(*h_t)->second);

          if (!l_tc.locks.empty()) {
            size_t lock_nums = l_tc.locks.size();
            for (int i = 0; i < lock_nums; i++) {
              if (l_tc.locks[i].find("spin") != string::npos) {
                break;
              }
              ptpn[l_t_pn[task_pn_size - 2 - 2 * (i + 1)]].pnt.is_handle = true;
              create_task_priority(
                  l_t_name, l_t_pn[task_pn_size - 3 - 2 * (i + 1)],
                  l_t_pn[task_pn_size - 2 - 2 * (i + 1)], l_t_pn[0],
                  l_t_pn.back(), nodes_type.find(*h_t)->second);
            }
          }
        }
      }
    }
  }
}

void PriorityTimePetriNet::create_task_priority(
    const std::string &name, vertex_ptpn preempt_vertex, size_t handle_t,
    vertex_ptpn start, vertex_ptpn end, NodeType task_type) {
  ptpn[handle_t].pnt.is_handle = true;
  string task_get = name + "get_core" + std::to_string(node_index);
  string task_ready = name + "ready" + std::to_string(node_index);
  string task_lock = name + "get_lock" + std::to_string(node_index);
  string task_deal = name + "deal" + std::to_string(node_index);
  string task_drop = name + "drop_lock" + std::to_string(node_index);
  string task_unlock = name + "unlocked" + std::to_string(node_index);
  string task_exec = name + "exec" + std::to_string(node_index);
  // get task prority and execute time
  std::vector<std::vector<vertex_ptpn>> task_preempt_path;
  std::vector<vertex_ptpn> node;
  node.push_back(start);
  if (holds_alternative<APeriodicTask>(task_type)) {
    APeriodicTask task = get<APeriodicTask>(task_type);
    vertex_ptpn get_core = add_transition(
        ptpn, task_get,
        PTPNTransition{task.priority, std::make_pair(0, 0), task.core});
    vertex_ptpn ready = add_place(ptpn, task_ready, 0);
    vertex_ptpn exec = add_transition(
        ptpn, task_exec,
        PTPNTransition{task.priority, task.time.back(), task.core});
    add_edge(start, get_core, ptpn);
    add_edge(preempt_vertex, get_core, ptpn);
    add_edge(get_core, ready, ptpn);
    add_edge(exec, end, ptpn);
    add_edge(exec, preempt_vertex, ptpn);
    node.push_back(start);
    node.push_back(get_core);
    node.push_back(ready);

    if (task.lock.empty()) {
      add_edge(ready, exec, ptpn);
      node_index += 1;
    } else {
      for (int j = 0; j < task.lock.size(); j++) {
        auto lock_name = task.lock[j];
        std::string gl = task_lock + lock_name + to_string(node_index);
        vertex_ptpn get_lock =
            add_transition(ptpn, gl, PTPNTransition{256, {0, 0}, task.core});
        vertex_ptpn deal = add_place(ptpn, task_deal + lock_name, 0);
        add_edge(node.back(), get_lock, ptpn);
        node.push_back(get_lock);
        node.push_back(deal);
        add_edge(get_lock, deal, ptpn);
      }
      for (int k = (int)(task.lock.size() - 1); k >= 0; k--) {
        auto lock_name = task.lock[k];
        auto t = task.time[k];
        std::string dl = task_drop + lock_name + to_string(node_index);
        vertex_ptpn drop_lock =
            add_transition(ptpn, dl, PTPNTransition{256, t, task.core});
        vertex_ptpn unlocked = add_place(ptpn, task_unlock + lock_name, 0);
        add_edge(node.back(), drop_lock, ptpn);
        add_edge(drop_lock, unlocked, ptpn);
        if (task.lock.size() == 1) {
          add_edge(unlocked, exec, ptpn);
        } else if (k == 0) {
          add_edge(unlocked, exec, ptpn);
        }
        node.push_back(drop_lock);
        node.push_back(unlocked);
      }
      node_index += 1;
    }
    node.push_back(exec);
    node.push_back(end);

    task_pn_map.find(task.name)->second.push_back(node);
  } else if (holds_alternative<PeriodicTask>(task_type)) {
    PeriodicTask task = get<PeriodicTask>(task_type);
    vertex_ptpn get_core = add_transition(
        ptpn, task_get,
        PTPNTransition{task.priority, std::make_pair(0, 0), task.core});
    vertex_ptpn ready = add_place(ptpn, task_ready, 0);
    vertex_ptpn exec = add_transition(
        ptpn, task_exec,
        PTPNTransition{task.priority, task.time.back(), task.core});
    add_edge(start, get_core, ptpn);
    add_edge(preempt_vertex, get_core, ptpn);
    add_edge(get_core, ready, ptpn);
    add_edge(exec, end, ptpn);
    add_edge(exec, preempt_vertex, ptpn);
    node.push_back(start);
    node.push_back(get_core);
    node.push_back(ready);

    if (task.lock.empty()) {
      add_edge(ready, exec, ptpn);
      node_index += 1;
    } else {
      for (int j = 0; j < task.lock.size(); j++) {
        auto lock_name = task.lock[j];
        std::string gl = task_lock + lock_name + to_string(node_index);
        vertex_ptpn get_lock =
            add_transition(ptpn, gl, PTPNTransition{256, {0, 0}, task.core});
        vertex_ptpn deal = add_place(ptpn, task_deal + lock_name, 0);
        add_edge(node.back(), get_lock, ptpn);
        node.push_back(get_lock);
        node.push_back(deal);
        add_edge(get_lock, deal, ptpn);
      }
      for (int k = (int)(task.lock.size() - 1); k >= 0; k--) {
        auto lock_name = task.lock[k];
        auto t = task.time[k];
        std::string dl = task_drop + lock_name + to_string(node_index);
        vertex_ptpn drop_lock =
            add_transition(ptpn, dl, PTPNTransition{256, t, task.core});
        vertex_ptpn unlocked = add_place(ptpn, task_unlock + lock_name, 0);
        add_edge(node.back(), drop_lock, ptpn);
        add_edge(drop_lock, unlocked, ptpn);
        if (task.lock.size() == 1) {
          add_edge(unlocked, exec, ptpn);
        } else if (k == 0) {
          add_edge(unlocked, exec, ptpn);
        }
        node.push_back(drop_lock);
        node.push_back(unlocked);
      }
      node_index += 1;
    }
    node.push_back(exec);
    node.push_back(end);

    task_pn_map.find(task.name)->second.push_back(node);
  } else {
    BOOST_LOG_TRIVIAL(error) << "unreachable!";
  }
}
