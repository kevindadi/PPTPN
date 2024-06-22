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

/// \brief 转换主函数, 首先对所有节点进行转换,然后绑定每个任务的 CPU
//// 根据优先级,建立抢占变迁, 绑定锁
/// \param tdg
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
  // add_preempt_task_ptpn();
  task_bind_cpu_resource(tdg.all_task);
  task_bind_lock_resource(tdg.all_task, tdg.task_locks_map);
  BOOST_LOG_TRIVIAL(info) << "petri net P+T num: " << boost::num_vertices(ptpn);
  BOOST_LOG_TRIVIAL(info) << "petri net F num: " << boost::num_edges(ptpn);
}

/// 为锁资源创建库所
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

/// 为处理器资源创建库所
void PriorityTimePetriNet::add_cpu_resource(int nums) {
  for (int i = 0; i < nums; i++) {
    string cpu_name = "core" + std::to_string(i);
    // vertex_tpn c = add_vertex(PetriNetElement{core_name, core_name, "circle",
    // 1}, ptpn);
    PTPNVertex cpu_place = {cpu_name, 1};
    vertex_ptpn c = add_vertex(cpu_place, ptpn);
    cpus_place.push_back(c);
  }
  BOOST_LOG_TRIVIAL(info) << "create core resource!";
}

/// 根据任务类型绑定不同位置的 CPU
void PriorityTimePetriNet::task_bind_cpu_resource(vector<NodeType> &all_task) {
  for (const auto &task : all_task) {
    if (holds_alternative<APeriodicTask>(task)) {
      auto ap_task = get<APeriodicTask>(task);
      int cpu_index = ap_task.core;
      auto task_pt_chains = node_pn_map.find(ap_task.name)->second;
      // Random -> Trigger -> Start -> Get CPU -> Run -> Drop CPU -> End
      add_edge(cpus_place[cpu_index], task_pt_chains[1], ptpn);
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

/// 根据任务种锁的数量和类型绑定
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
            vertex_ptpn get_lock = task_pt_chain[(3 + 2 * i)];
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

/// 映射规则主函数,包含不同类型,便于之后扩展
/// 根据不同类型进行相应的转换,并返回转换后的开始和结束节点,以进行前后链接
pair<vertex_ptpn, vertex_ptpn>
PriorityTimePetriNet::add_node_ptpn(NodeType node_type) {

  if (holds_alternative<PeriodicTask>(node_type)) {
    PeriodicTask p_task = get<PeriodicTask>(node_type);
    auto result = add_p_node_ptpn(p_task);
    node_start_end_map.insert(make_pair(p_task.name, result));
    BOOST_LOG_TRIVIAL(info)
        << p_task.name << "'s petri net start node: " << result.first
        << " end node: " << result.second;
  } else if (holds_alternative<APeriodicTask>(node_type)) {
    APeriodicTask ap_task = get<APeriodicTask>(node_type);
    auto result = add_ap_node_ptpn(ap_task);
    node_start_end_map.insert(make_pair(ap_task.name, result));
    BOOST_LOG_TRIVIAL(info)
        << ap_task.name << "'s petri net start node: " << result.first
        << " end node: " << result.second;
  } else if (holds_alternative<SyncTask>(node_type)) {
    SyncTask ap_task = get<SyncTask>(node_type);
    string t_name = ap_task.name;
    vertex_ptpn result =
        add_transition(ptpn, "Sync" + to_string(node_index),
                       PTPNTransition{100, make_pair(0, 0), INT_MAX});
    node_index += 1;
    node_start_end_map.insert(make_pair(t_name, make_pair(result, result)));
    BOOST_LOG_TRIVIAL(info) << t_name << ": " << "type: SYNC";
  } else if (holds_alternative<DistTask>(node_type)) {
    DistTask ap_task = get<DistTask>(node_type);
    string t_name = ap_task.name;
    vertex_ptpn result =
        add_transition(ptpn, "Dist" + to_string(node_index),
                       PTPNTransition{100, make_pair(0, 0), INT_MAX});
    node_index += 1;
    node_start_end_map.insert(make_pair(t_name, make_pair(result, result)));
    BOOST_LOG_TRIVIAL(info) << t_name << ": " << "type: DIST";
  } else {
    EmptyTask ap_task = get<EmptyTask>(node_type);
    string t_name = ap_task.name;
    vertex_ptpn result = add_place(ptpn, "Empty" + to_string(node_index), 0);
    node_index += 1;
    node_start_end_map.insert(make_pair(t_name, make_pair(result, result)));
    BOOST_LOG_TRIVIAL(info) << t_name << ": " << "type: EMPTY";
  }
}

/// 针对非周期的偶发任务和中断任务建模
pair<vertex_ptpn, vertex_ptpn>
PriorityTimePetriNet::add_p_node_ptpn(PeriodicTask &p_task) {
  pair<vertex_ptpn, vertex_ptpn> result;
  string t_name = p_task.name;
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
  int task_priority = p_task.priority;
  int task_core = p_task.core;
  // 任务临界区外执行时间
  auto task_exec_time = p_task.time[(p_task.time.size() - 1)];

  vertex_ptpn random = add_place(ptpn, task_random_period, 1);
  vertex_ptpn fire = add_transition(
      ptpn, task_fire,
      PTPNTransition{task_priority, p_task.period_time, task_core});
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
  result.first = entry;
  result.second = exit;
  if (p_task.lock.empty()) {
    add_edge(ready, exec, ptpn);
  } else {
    size_t lock_nums = p_task.lock.size();
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
    for (int j = (int)(lock_nums - 1); j >= 0; j--) {
      auto lock_name = p_task.lock[j];
      auto t = p_task.time[j];
      vertex_ptpn drop_lock =
          add_transition(ptpn, task_drop.append(lock_name),
                         PTPNTransition{task_priority, t, task_core});
      vertex_ptpn unlocked = add_place(ptpn, task_unlock + lock_name, 0);
      add_edge(task_pt_chain.back(), drop_lock, ptpn);
      add_edge(drop_lock, unlocked, ptpn);
      if (lock_nums == 1) {
        // add_edge(link, drop_lock, ptpn);
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

/// 对于普通任务或者带周期的任务建模
/// 普通任务和周期任务的区别在于是否有截止时间约束
pair<vertex_ptpn, vertex_ptpn>
PriorityTimePetriNet::add_ap_node_ptpn(APeriodicTask &ap_task) {
  pair<vertex_ptpn, vertex_ptpn> result;
  string t_name = ap_task.name;
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
  if (ap_task.lock.empty()) {
    add_edge(ready, exec, ptpn);
  } else {
    int lock_nums = (int)ap_task.lock.size();
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
    for (int j = (lock_nums - 1); j >= 0; j--) {
      auto lock_name = ap_task.lock[j];
      auto t = ap_task.time[j];
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

/// 增加看门狗子网结构,只链接每个周期任务的开始和结束节点
/// 检测每个路径(包括抢占路径)的任务执行流
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

/// \brief
/// 为每个任务添加抢占的执行序列,根据任务的分配的处理器资源,对每个处理器上的任务从低优先级开始
/// (t1, t2,
///  t3,...) t2 抢占 t1, t3 抢占 t1 和 t2,以此类推
/// \param core_task 分配到同一处理器的任务名称数组
/// \param tc 任务属性映射
/// \param nodes_type 任务类型映射
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
        auto l_t_pns = task_pn_map.find(*l_t);
        auto h_t_pns = task_pn_map.find(*h_t);
        vector<vertex_ptpn> h_t_pn = h_t_pns->second[0];
        // 需要判断抢占和被抢占任务的类型
        NodeType node_type = nodes_type.at(h_t_name);
        if (holds_alternative<PeriodicTask>(node_type)) {
          PeriodicTask p_task = get<PeriodicTask>(node_type);
          if (p_task.task_type == TaskType::INTERRUPT) {
            // 如果是中断任务，就要多建立执行序列
            // 对低优先级的任务的每条执行流进行抢占
            for (auto l_t_pn : l_t_pns->second) {
              // 如果高优先级任务不是中断,执行 hlf 抢占, 否则, 执行 lgj 抢占
              int task_pn_size = l_t_pn.size();
              ptpn[l_t_pn[task_pn_size - 2]].pnt.is_handle = true;
              create_task_priority(
                  h_t_name, l_t_pn[task_pn_size - 3], l_t_pn[task_pn_size - 2],
                  l_t_pn[0], l_t_pn.back(), nodes_type.find(*h_t)->second);

              if (!l_tc.locks.empty()) {
                size_t lock_nums = l_tc.locks.size();
                for (int i = 0; i < lock_nums; i++) {
                  if (l_tc.locks[i].find("spin") != string::npos) {
                    break;
                  }
                  ptpn[l_t_pn[task_pn_size - 2 - 2 * (i + 1)]].pnt.is_handle =
                      true;
                  create_task_priority(
                      h_t_name, l_t_pn[task_pn_size - 3 - 2 * (i + 1)],
                      l_t_pn[task_pn_size - 2 - 2 * (i + 1)], l_t_pn[0],
                      l_t_pn.back(), nodes_type.find(*h_t)->second);
                }
              }
            }
            // 处理中断任务的抢占序列后返回上一层 for
            // 循环,下面继续处理非中断抢占
            continue;
          }
        }

        for (auto l_t_pn : l_t_pns->second) {
          // 如果高优先级任务不是中断,执行 hlf 抢占, 否则, 执行 lgj 抢占
          int task_pn_size = l_t_pn.size();
          ptpn[l_t_pn[task_pn_size - 2]].pnt.is_handle = true;
          create_hlf_task_priority(
              h_t_name, l_t_pn[task_pn_size - 3], l_t_pn[task_pn_size - 2],
              h_t_pn[0], l_t_pn[0], h_t_pn[2], h_tc.priority, h_tc.core);

          if (!l_tc.locks.empty()) {
            size_t lock_nums = l_tc.locks.size();
            for (int i = 0; i < lock_nums; i++) {
              if (l_tc.locks[i].find("spin") != string::npos) {
                break;
              }
              ptpn[l_t_pn[task_pn_size - 2 - 2 * (i + 1)]].pnt.is_handle = true;
              create_hlf_task_priority(
                  h_t_name, l_t_pn[task_pn_size - 3 - 2 * (i + 1)],
                  l_t_pn[task_pn_size - 2 - 2 * (i + 1)], h_t_pn[0], l_t_pn[0],
                  h_t_pn[2], h_tc.priority, h_tc.core);
            }
          }
        }
        BOOST_LOG_TRIVIAL(debug) << "priority " << *l_t << ": " << l_tc.priority
                                 << *h_t << ": " << h_tc.priority;
      }
    }
  }
}

/// \brief 根据软件学报论文创建抢占变迁
/// \param name 高优先级任务的名字
/// \param preempt_vertex 发生抢占的库所
/// \param handle_t 可挂起的变迁
/// \param start 高优先级的开始库所
/// \param end 高优先级的结束库所
/// \param task_type 高优先级任务类型
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

/// \brief 根据 ACM 论文创建的抢占变迁
/// \param name 高优先级任务的名字
/// \param preempt_vertex 发生抢占的库所
/// \param handle_t 可挂起的变迁
/// \param h_start 高优先级任务的开始库所
/// \param l_start 低优先级任务的开始库所
/// \param h_ready 高优先级任务获得处理器资源后的库所
/// \param task_priority 高优先级任务的优先级
/// \param task_core 高优先级任务的处理器资源
void PriorityTimePetriNet::create_hlf_task_priority(
    const std::string &name, vertex_ptpn preempt_vertex, size_t handle_t,
    vertex_ptpn h_start, vertex_ptpn l_start, vertex_ptpn h_ready,
    int task_priority, int task_core) {
  ptpn[handle_t].pnt.is_handle = true;
  string task_get = name + "get_core" + std::to_string(node_index);

  vertex_ptpn get_core = add_transition(
      ptpn, task_get,
      PTPNTransition{task_priority, std::make_pair(0, 0), task_core});

  add_edge(h_start, get_core, ptpn);
  add_edge(preempt_vertex, get_core, ptpn);
  add_edge(get_core, h_ready, ptpn);
  add_edge(get_core, l_start, ptpn);

  node_index += 1;
}

StateClass PriorityTimePetriNet::get_initial_state_class() {
  BOOST_LOG_TRIVIAL(info) << "get_initial_state_class";
  Marking initial_markings;
  //  std::set<std::size_t> h_t,H_t;
  //  std::unordered_map<size_t, int> enabled_t_time;
  std::set<T_wait> all_t;
  boost::graph_traits<PTPN>::vertex_iterator vi, vi_end;
  boost::graph_traits<PTPN>::in_edge_iterator in_i, in_end;
  for (boost::tie(vi, vi_end) = boost::vertices(ptpn); vi != vi_end; ++vi) {
    if (ptpn[*vi].shape == "circle") {
      if (ptpn[*vi].token == 1) {
        initial_markings.indexes.insert(index(*vi));
        initial_markings.labels.insert(ptpn[*vi].label);
      }
    } else {
      bool flag = true;
      for (boost::tie(in_i, in_end) = boost::in_edges(*vi, ptpn);
           in_i != in_end; ++in_i) {
        auto source_vertex = source(*in_i, ptpn);
        if (ptpn[source_vertex].token == 0) {
          flag = false;
          break;
        }
      }
      if (flag) {
        //        if (ptpn[*vi].pnt.is_handle) {
        //          H_t.insert(index(*vi));
        //          enabled_t_time.insert(std::make_pair(index(*vi), 0));
        //        } else {
        //          h_t.insert(index(*vi));
        //          enabled_t_time.insert(std::make_pair(index(*vi), 0));
        //        }

        all_t.insert({*vi, 0});
      }
    }
  }
  return StateClass{initial_markings, all_t};
}

void PriorityTimePetriNet::generate_state_class() {
  BOOST_LOG_TRIVIAL(info) << "Generating state class";
  auto pt_a = chrono::steady_clock::now();
  queue<StateClass> q;
  q.push(initial_state_class);
  scg.insert(initial_state_class);

  std::string vertex_label = initial_state_class.to_scg_vertex();
  ScgVertexD v_id =
      boost::add_vertex(SCGVertex{vertex_label}, state_class_graph.scg);
  state_class_graph.scg_vertex_map.insert(
      std::make_pair(initial_state_class, v_id));
  int i = 0;
  while (!q.empty()) {
    StateClass s = q.front();
    q.pop();
    ScgVertexD prev_vertex = state_class_graph.add_scg_vertex(s);

    std::vector<SchedT> sched_t = get_sched_t(s);

    for (auto &t : sched_t) {
      StateClass new_state = fire_transition(s, t);
      std::cout << i << std::endl;
      ++i;
      // BOOST_LOG_TRIVIAL(info) << "new state: ";
      // new_state.print_current_mark();
      if (scg.find(new_state) != scg.end()) {

      } else {
        scg.insert(new_state);
        q.push(new_state);
        new_state.print_current_state();
      }

      auto succ_vertex = state_class_graph.add_scg_vertex(new_state);
      SCGEdge scg_e = {std::to_string(t.time.first)
                           .append(":")
                           .append(std::to_string(t.time.second))};
      boost::add_edge(prev_vertex, succ_vertex, scg_e, state_class_graph.scg);
    }
    //    if (sec.count() >= timeout) {
    //        break;
    //    }
  }
  chrono::duration<double> sec = chrono::steady_clock::now() - pt_a;
  BOOST_LOG_TRIVIAL(info) << "Generate SCG Time(s): " << sec.count();
  BOOST_LOG_TRIVIAL(info) << "SCG NUM: " << scg.size();
  BOOST_LOG_TRIVIAL(info) << "SYSTEM NO DEADLOCK!";
  state_class_graph.write_to_dot("out.dot");
}

std::vector<SchedT> PriorityTimePetriNet::get_sched_t(StateClass &state) {
  BOOST_LOG_TRIVIAL(debug) << "get sched t";
  set_state_class(state);
  // 获得使能的变迁
  std::vector<vertex_ptpn> enabled_t_s;
  std::vector<SchedT> sched_T;
  boost::graph_traits<PTPN>::vertex_iterator vi, vi_end;
  boost::graph_traits<PTPN>::in_edge_iterator in_i, in_end;
  for (boost::tie(vi, vi_end) = vertices(ptpn); vi != vi_end; ++vi) {
    bool enable_t = true;
    if (ptpn[*vi].shape == "circle") {
      continue;
    } else {
      for (boost::tie(in_i, in_end) = in_edges(*vi, ptpn); in_i != in_end;
           ++in_i) {
        auto source_vertex = source(*in_i, ptpn);
        if (ptpn[source_vertex].token == 0) {
          enable_t = false;
          break;
        }
      }
      if (enable_t)
        enabled_t_s.push_back(vertex(*vi, ptpn));
    }
  }

  if (enabled_t_s.empty()) {
    return {};
  }

  // 遍历这些变迁,找到符合发生区间的变迁集合
  std::pair<int, int> fire_time = {INT_MAX, INT_MAX};
  for (auto t : enabled_t_s) {
    // 如果等待时间已超过最短发生时间
    int f_min = ptpn[t].pnt.const_time.first - ptpn[t].pnt.runtime;
    if (f_min < 0) {
      f_min = 0;
    }
    int f_max = ptpn[t].pnt.const_time.second - ptpn[t].pnt.runtime;
    fire_time.first = (f_min < fire_time.first) ? f_min : fire_time.first;
    fire_time.second = (f_max < fire_time.second) ? f_max : fire_time.second;
  }
  if ((fire_time.first < 0) && (fire_time.second < 0)) {
    // BOOST_LOG_TRIVIAL(error) << "fire time not leq zero";
  }
  // find transition which satifies the time domain
  std::pair<int, int> sched_time = {0, 0};
  int s_h, s_l;
  for (auto t : enabled_t_s) {
    int t_min = ptpn[t].pnt.const_time.first - ptpn[t].pnt.runtime;
    if (t_min < 0) {
      t_min = 0;
    }
    int t_max = ptpn[t].pnt.const_time.second - ptpn[t].pnt.runtime;
    if (t_min > fire_time.second) {

      sched_time = fire_time;
    } else if (t_max > fire_time.second) {
      // transition's max go beyond time_d
      if (t_min >= fire_time.first) {
        sched_time = {t_min, fire_time.second};
      } else {
        BOOST_LOG_TRIVIAL(error) << "the time_d min value is error";
      }
    } else {
      if (t_min >= fire_time.first) {
        sched_time = {t_min, t_max};
      } else {
        BOOST_LOG_TRIVIAL(error) << "the time_d min value is error";
      }
    }

    sched_T.push_back(SchedT{t, sched_time});
  }

  // 删除优先级
  auto s_it = sched_T.begin();
  while (s_it != sched_T.end()) {
    auto next = s_it + 1;
    while (next != sched_T.end()) {
      if ((ptpn[(*s_it).t].pnt.priority > ptpn[(*next).t].pnt.priority) &&
          (ptpn[(*s_it).t].pnt.c == ptpn[(*next).t].pnt.c)) {
        sched_T.erase(next);
        next = s_it + 1;
      } else {
        ++next;
      }
    }
    ++s_it;
  }
  //  std::vector<int> erase_index;
  //  for (int i = 0; i < sched_T.size() - 1; i++) {
  //    for (int j = 1; j < sched_T.size(); j++) {
  //      if ((ptpn[sched_T[i].t].pnt.priority <
  //      ptpn[sched_T[j].t].pnt.priority) &&
  //            (ptpn[sched_T[i].t].pnt.c == ptpn[sched_T[j].t].pnt.c)) {
  //        erase_index.push_back(i);
  //        }
  //    }
  //  }
  //  // 遍历要删除的下标集合
  //  for (auto it = erase_index.rbegin(); it != erase_index.rend(); ++it) {
  //    auto index_i = *it;
  //    auto pos = sched_T.begin() + index_i; // 获取对应元素的迭代器
  //    // 如果该变迁为可挂起变迁,记录已等待时间
  //    //    if (ptpn[sched_T[*pos].t].pnt.is_handle) {
  //    //    }
  //    sched_T.erase(pos); // 移除该元素
  //  }
  for (auto d : sched_T) {
    BOOST_LOG_TRIVIAL(debug) << ptpn[d.t].label;
  }

  return sched_T;
}

StateClass PriorityTimePetriNet::fire_transition(const StateClass &sc,
                                                 SchedT transition) {
  BOOST_LOG_TRIVIAL(debug) << "fire_transition: " << transition.t;
  // reset!
  set_state_class(sc);
  // fire transition
  // 1. 首先获取所有可调度变迁
  std::vector<std::size_t> enabled_t, old_enabled_t;
  boost::graph_traits<PTPN>::vertex_iterator vi, vi_end;
  boost::graph_traits<PTPN>::in_edge_iterator in_i, in_end;
  for (boost::tie(vi, vi_end) = vertices(ptpn); vi != vi_end; ++vi) {
    bool enable_t = true;
    if (ptpn[*vi].shape == "circle") {
      continue;
    } else {
      for (boost::tie(in_i, in_end) = in_edges(*vi, ptpn); in_i != in_end;
           ++in_i) {
        auto source_vertex = source(*in_i, ptpn);
        if (ptpn[source_vertex].token == 0) {
          enable_t = false;
          break;
        }
      }
      if (enable_t)
        enabled_t.push_back(vertex(*vi, ptpn));
      old_enabled_t.push_back(vertex(*vi, ptpn));
    }
  }

  // 删除优先级
  auto s_it = enabled_t.begin();
  while (s_it != enabled_t.end()) {
    auto next = s_it + 1;
    while (next != enabled_t.end()) {
      if ((ptpn[(*s_it)].pnt.priority > ptpn[(*next)].pnt.priority) &&
          (ptpn[(*s_it)].pnt.c == ptpn[(*next)].pnt.c)) {
        enabled_t.erase(next);
        next = s_it + 1;
      } else {
        ++next;
      }
    }
    ++s_it;
  }
  //  std::vector<int> erase_index;
  //  for (int i = 0; i < enabled_t.size() - 1; i++) {
  //    for (int j = 1; j < enabled_t.size(); j++) {
  //        if ((ptpn[enabled_t[i]].pnt.priority <
  //        ptpn[enabled_t[j]].pnt.priority) &&
  //            (ptpn[enabled_t[i]].pnt.c == ptpn[enabled_t[j]].pnt.c)) {
  //        erase_index.push_back(i);
  //        }
  //    }
  //  }
  //  // 遍历要删除的下标集合
  //  for (auto it = erase_index.rbegin(); it != erase_index.rend(); ++it) {
  //    auto index_i = *it;
  //    auto pos = enabled_t.begin() + index_i;  // 获取对应元素的迭代器
  //    // 如果该变迁为可挂起变迁,记录已等待时间
  //    //    if (ptpn[sched_T[*pos].t].pnt.is_handle) {
  //    //    }
  //    enabled_t.erase(pos);  // 移除该元素
  //  }
  // 2. 将除发生变迁外的使能时间+状态转移时间
  for (auto t : enabled_t) {
    if (transition.time.second == transition.time.first) {
      ptpn[t].pnt.runtime += transition.time.first;
    } else {
      ptpn[t].pnt.runtime += (transition.time.second - transition.time.first);
    }
  }
  ptpn[transition.t].pnt.runtime = 0;
  // 3. 发生该变迁
  Marking new_mark;
  //  std::set<std::size_t> h_t, H_t;
  //  std::unordered_map<std::size_t, int> time;
  std::set<T_wait> all_t;
  // 发生变迁的前置集为 0
  for (boost::tie(in_i, in_end) = in_edges(transition.t, ptpn); in_i != in_end;
       ++in_i) {
    vertex_ptpn place = source(*in_i, ptpn);
    if (ptpn[place].token < 0) {
      BOOST_LOG_TRIVIAL(error)
          << "place token <= 0, the transition not enabled";
    } else {
      ptpn[place].token = 0;
    }
  }
  // 发生变迁的后继集为 1
  typename boost::graph_traits<PTPN>::out_edge_iterator out_i, out_end;
  for (boost::tie(out_i, out_end) = out_edges(transition.t, ptpn);
       out_i != out_end; ++out_i) {
    vertex_ptpn place = target(*out_i, ptpn);
    if (ptpn[place].token > 1) {
      BOOST_LOG_TRIVIAL(error)
          << "safe petri net, the place not increment >= 1";
    } else {
      ptpn[place].token = 1;
    }
  }
  // 4.获取新的标识
  std::vector<std::size_t> new_enabled_t;
  for (boost::tie(vi, vi_end) = vertices(ptpn); vi != vi_end; ++vi) {
    bool enable_t = true;
    if (ptpn[*vi].shape == "circle") {
      if (ptpn[*vi].token == 1) {
        new_mark.indexes.insert(vertex(*vi, ptpn));
        new_mark.labels.insert(ptpn[*vi].label);
      }
    } else {
      for (boost::tie(in_i, in_end) = in_edges(*vi, ptpn); in_i != in_end;
           ++in_i) {
        auto source_vertex = source(*in_i, ptpn);
        if (ptpn[source_vertex].token == 0) {
          enable_t = false;
          break;
        }
      }
      if (enable_t)
        new_enabled_t.push_back(vertex(*vi, ptpn));
    }
  }

  // 5. 比较前后使能部分,前一种状态使能,而新状态不使能,则为可挂起变迁
  // 将所有变迁等待时间置为0

  // 5.1 公共部分
  std::set<std::size_t> common;
  std::set_intersection(old_enabled_t.begin(), old_enabled_t.end(),
                        new_enabled_t.begin(), new_enabled_t.end(),
                        std::inserter(common, common.begin()));
  for (unsigned long it : common) {
    all_t.insert({it, ptpn[it].pnt.runtime});
    //    h_t.insert(it);
    //    time.insert(std::make_pair(it, ptpn[it].pnt.runtime));
  }
  // 5.2 前状态使能而现状态不使能
  std::set<std::size_t> old_minus_new;
  std::set_difference(old_enabled_t.begin(), old_enabled_t.end(),
                      new_enabled_t.begin(), new_enabled_t.end(),
                      std::inserter(old_minus_new, old_minus_new.begin()));
  for (unsigned long it : old_minus_new) {
    // 若为可挂起变迁,则保存已运行时间
    if (ptpn[it].pnt.is_handle) {
      all_t.insert({it, ptpn[it].pnt.runtime});
      //      H_t.insert(it);
      //      time.insert(std::make_pair(it, ptpn[it].pnt.runtime));
    } else {
      ptpn[it].pnt.runtime = 0;
    }
  }
  // 5.3 现状态使能而前状态不使能
  std::set<std::size_t> new_minus_old;
  std::set_difference(new_enabled_t.begin(), new_enabled_t.end(),
                      old_enabled_t.begin(), old_enabled_t.end(),
                      std::inserter(new_minus_old, new_minus_old.begin()));
  for (unsigned long it : new_minus_old) {
    if (ptpn[it].pnt.is_handle) {
      all_t.insert({it, ptpn[it].pnt.runtime});
      //      h_t.insert(it);
      //      time.insert(std::make_pair(it, ptpn[it].pnt.runtime));
    } else {
      //      h_t.insert(it);
      //      time.insert(std::make_pair(it, 0));
      all_t.insert({it, 0});
    }
  }
  //  for (auto it = all_t.begin(); it != all_t.end(); ++it) {
  //    for (auto it2 = std::next(it); it2 != all_t.end(); ) {
  //      if ((ptpn[(*it).t].pnt.priority < ptpn[(*it2).t].pnt.priority) &&
  //          (ptpn[(*it).t].pnt.c == ptpn[(*it2).t].pnt.c)) {
  //        if (ptpn[(*it2).t].pnt.is_handle) {
  //          ++it2;
  //        } else {
  //          it2 = all_t.erase(it);
  //        }
  //      } else {
  //        // 继续迭代
  //        ++it2;
  //      }
  //    }
  //  }
  //  StateClass new_sc;
  //  new_sc.mark = new_mark;
  //  new_sc.t_sched = h_t;
  //  new_sc.handle_t_sched = H_t;
  //  new_sc.t_time = time;
  return {new_mark, all_t};
}

void PriorityTimePetriNet::set_state_class(const StateClass &state_class) {
  // 首先重置原有状态
  boost::graph_traits<PTPN>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = vertices(ptpn); vi != vi_end; ++vi) {
    ptpn[*vi].token = 0;
    ptpn[*vi].enabled = false;
    ptpn[*vi].pnt.runtime = 0;
  }
  // 2. 设置标识
  for (auto m : state_class.mark.indexes) {
    ptpn[m].token = 1;
  }
  // 3. 设置各个变迁的已等待时间
  //  for (auto t : state_class.t_sched) {
  //    ptpn[t].pnt.runtime = state_class.t_time.find(t)->second;
  //  }
  // 4. 可挂起变迁的已等待时间
  //  for (auto t : state_class.handle_t_sched) {
  //    ptpn[t].pnt.runtime = state_class.t_time.find(t)->second;
  //  }
  // 5.设置所有变迁的已等待时间
  for (auto t : state_class.all_t) {
    ptpn[t.t].pnt.runtime = t.time;
  }
}