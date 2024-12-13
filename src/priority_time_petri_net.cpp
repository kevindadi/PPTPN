//
// Created by 张凯文 on 2024/3/22.
//
#include "priority_time_petri_net.h"

// 任务节点名称生成器
struct TaskNodeNames {
  string entry, get_core, ready, get_lock, deal, drop_lock, unlock, exec, exit;
  
  explicit TaskNodeNames(const string& task_name) {
    entry = task_name + "entry";
    get_core = task_name + "get_core";
    ready = task_name + "ready";
    get_lock = task_name + "get_lock";
    deal = task_name + "deal";
    drop_lock = task_name + "drop_lock";
    unlock = task_name + "unlocked";
    exec = task_name + "exec";
    exit = task_name + "exit";
  }
};

// 创建基本任务结构的辅助函数
struct BasicTaskStructure {
  vertex_ptpn entry, get_core, ready, exec, exit;
  vector<vertex_ptpn> task_pt_chain;
  
  BasicTaskStructure(PriorityTimePetriNet* ptpn, const TaskNodeNames& names,
                    int priority, int core, const pair<int,int>& exec_time) {
    entry = ptpn->add_place(ptpn->ptpn, names.entry, 0);
    get_core =  ptpn->add_transition(ptpn->ptpn, names.get_core,
                                  PTPNTransition{priority, {0, 0}, core});
    ready = ptpn ->add_place(ptpn->ptpn, names.ready, 0);
    exec = ptpn->add_transition(ptpn->ptpn, names.exec,
                              PTPNTransition{priority, exec_time, core});
          exit = ptpn->add_place(ptpn->ptpn, names.exit, 0);
    
    ptpn->add_edge(entry, get_core, ptpn->ptpn);
    ptpn->add_edge(get_core, ready, ptpn->ptpn);
    ptpn->add_edge(exec, exit, ptpn->ptpn);
    
    task_pt_chain = {entry, get_core, ready};
  }
};

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
  try {
    // 1. 转换顶点
    transform_vertices(tdg);
    
    // 2. 转换边
    transform_edges(tdg);

    // 3. 创建优先级抢占关系
    add_preempt_task_ptpn(tdg.classify_priority(), tdg.tasks_config, tdg.nodes_type);
    
    // 4. 添加资源和绑定
    add_resources_and_bindings(tdg);
    
    // 5. 输出网络信息
    log_network_info();
  } catch (const std::exception& e) {
    BOOST_LOG_TRIVIAL(error) << "Failed to transform TDG to PTPN: " << e.what();
    throw;
  }

}


void PriorityTimePetriNet::transform_vertices(TDG &tdg) {
  BOOST_FOREACH (TDG_RAP::vertex_descriptor v, vertices(tdg.tdg)) {
    const string& vertex_name = tdg.tdg[v].name;
    BOOST_LOG_TRIVIAL(debug) << "Processing vertex: " << vertex_name;
    
    try {
      auto node_type_it = tdg.nodes_type.find(vertex_name);
      if (node_type_it == tdg.nodes_type.end()) {
        throw std::runtime_error("Node type not found for: " + vertex_name);
      }
      
      add_node_ptpn(node_type_it->second);
    } catch (const std::exception& e) {
      BOOST_LOG_TRIVIAL(error) << "Failed to transform vertex " << vertex_name 
                              << ": " << e.what();
      throw;
    }
  }
  BOOST_LOG_TRIVIAL(info) << "Vertex transformation completed";
}

void PriorityTimePetriNet::transform_edges(TDG &tdg) {
  BOOST_FOREACH (TDG_RAP::edge_descriptor e, edges(tdg.tdg)) {
    try {
      const string& source_name = tdg.tdg[source(e, tdg.tdg)].name;
      const string& target_name = tdg.tdg[target(e, tdg.tdg)].name;
      
      if (is_self_loop_edge(source_name, target_name)) {
        handle_self_loop_edge(tdg, e, source_name);
        continue;
      }
      
      if (is_dashed_edge(tdg.tdg[e].style)) {
        handle_dashed_edge(source_name, target_name);
        continue;
      }
      
      handle_normal_edge(source_name, target_name);
      
    } catch (const std::exception& e) {
      BOOST_LOG_TRIVIAL(error) << "Failed to transform edge: " << e.what();
      throw;
    }
  }
}

bool PriorityTimePetriNet::is_self_loop_edge(const string& source, const string& target) {
  return source == target;
}

bool PriorityTimePetriNet::is_dashed_edge(const string& edge) {
  return edge.find("dashed") != string::npos;
}

void PriorityTimePetriNet::handle_self_loop_edge(TDG &tdg, 
                                                TDG_RAP::edge_descriptor e, 
                                                const string& source_name) {
  int task_period_time = std::stoi(tdg.tdg[e].label);
  auto task_start_end = node_start_end_map.find(source_name);
  if (task_start_end == node_start_end_map.end()) {
    throw std::runtime_error("Start/end nodes not found for: " + source_name);
  }
  
  add_monitor_ptpn(source_name, task_period_time, 
                   task_start_end->second.first,
                   task_start_end->second.second);
}

void PriorityTimePetriNet::handle_dashed_edge(const string& source_name, 
                                             const string& target_name) {
  // 虚线链接的尾节点为开始节点,头节点为结束节点
  // TODO: 实现虚线边的处理逻辑
}

void PriorityTimePetriNet::handle_normal_edge(const string& source_name, 
                                             const string& target_name) {
  auto source_it = node_start_end_map.find(source_name);
  auto target_it = node_start_end_map.find(target_name);
  
  if (source_it == node_start_end_map.end() || target_it == node_start_end_map.end()) {
    throw std::runtime_error("Node mapping not found for edge: " + 
                           source_name + " -> " + target_name);
  }
  
  vertex_ptpn source_node = source_it->second.second;
  vertex_ptpn target_node = target_it->second.first;

  BOOST_LOG_TRIVIAL(debug) << "source_name: " << source_name << " target_name: " << target_name;

  // 如果链接的某个节点属于Dist，Sync则直接链接
  if (source_name.substr(0, 4) ==  "Dist" || source_name.substr(0, 4) == "Wait") {
    add_edge(source_node, target_node, ptpn);
    return;
  } else if (target_name.substr(0, 4) == "Dist" || target_name.substr(0, 4) == "Wait") {
    add_edge(source_node, target_node, ptpn);
    return;
  } 
  
  // 添加中间变迁
  string trans_name = source_name + "_to_" + target_name;
  vertex_ptpn middle_trans = add_transition(ptpn, trans_name, PTPNTransition{0, {0,0}, INT_MAX});
                
  add_edge(source_node, middle_trans, ptpn);
  add_edge(middle_trans, target_node, ptpn);
  BOOST_LOG_TRIVIAL(debug) << "Added edge: " << source_node << " -> " << target_node;
}

void PriorityTimePetriNet::add_resources_and_bindings(TDG &tdg) {
  // 添加CPU资源
  add_cpu_resource(6);
  
  // 添加锁资源
  add_lock_resource(tdg.lock_set);
  
  // 绑定任务到CPU
  task_bind_cpu_resource(tdg.all_task);
  
  // 绑定任务到锁
  task_bind_lock_resource(tdg.all_task, tdg.task_locks_map);
}

void PriorityTimePetriNet::log_network_info() {
  BOOST_LOG_TRIVIAL(info) << "Petri net statistics:";
  BOOST_LOG_TRIVIAL(info) << "- Places + Transitions: " << boost::num_vertices(ptpn);
  BOOST_LOG_TRIVIAL(info) << "- Flows: " << boost::num_edges(ptpn);
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
    vector<NodeType>& all_task,
    std::map<string, vector<string>>& task_locks) {

  // 处理单个任务的锁资源绑定
  auto bind_task_locks = [&](const string& task_name,
                            const vector<string>& lock_types,
                            const vector<vector<vertex_ptpn>>& task_pt_chains) {
    constexpr size_t MIN_CHAIN_LENGTH = 5; // 不含锁的 P-T 链最小长度
    
    for (const auto& task_pt_chain : task_pt_chains) {
      // 检查链长度是否满足最小要求
      if (task_pt_chain.size() < MIN_CHAIN_LENGTH) {
        BOOST_LOG_TRIVIAL(error) << "Skip chain for " << task_name 
                                << ": too short for locks";
        continue;
      }
      
      // 获取任务的锁数量
      auto task_locks_it = task_locks.find(task_name);
      if (task_locks_it == task_locks.end()) {
        BOOST_LOG_TRIVIAL(warning) << "No locks found for task: " << task_name;
        continue;
      }
      size_t lock_nums = task_locks_it->second.size();
      BOOST_LOG_TRIVIAL(debug) << "chains size for task : " << task_name << " is: " << task_pt_chain.size();
      // 为每个锁添加获取和释放边
      for (size_t i = 0; i < lock_nums; i++) {
        try {
          // 获取锁类型
          const string& lock_type = lock_types[i];
          
          // 计算获取和释放锁的节点索引
          vertex_ptpn get_lock = task_pt_chain[3 + 2 * i];
          vertex_ptpn drop_lock = task_pt_chain[task_pt_chain.size() - 2 - 2 * (i + 1)];
          
          // 查找对应的锁库所
          auto lock_it = locks_place.find(lock_type);
          if (lock_it == locks_place.end()) {
            throw std::runtime_error("Lock place not found: " + lock_type);
          }
          vertex_ptpn lock = lock_it->second;
          
          // 添加边
          add_edge(lock, get_lock, ptpn);
          add_edge(drop_lock, lock, ptpn);
          
          BOOST_LOG_TRIVIAL(debug) << "Bound lock " << lock_type 
                                  << " to task " << task_name;
                                  
        } catch (const std::exception& e) {
          BOOST_LOG_TRIVIAL(error) << "Failed to bind lock " << i 
                                  << " for task " << task_name 
                                  << ": " << e.what();
          throw;
        }
      }
    }
  };

  // 处理所有任务
  for (const auto& task : all_task) {
    try {
      if (holds_alternative<APeriodicTask>(task)) {
        const auto& ap_task = get<APeriodicTask>(task);
        auto chains_it = task_pn_map.find(ap_task.name);
        if (chains_it != task_pn_map.end()) {
          bind_task_locks(ap_task.name, ap_task.lock, chains_it->second);
        }
        
      } else if (holds_alternative<PeriodicTask>(task)) {
        const auto& p_task = get<PeriodicTask>(task);
        auto chains_it = task_pn_map.find(p_task.name);
        if (chains_it != task_pn_map.end()) {
          bind_task_locks(p_task.name, p_task.lock, chains_it->second);
        }
      }
      // 其他类型的任务忽略
      
    } catch (const std::exception& e) {
      BOOST_LOG_TRIVIAL(error) << "Failed to process task: " << e.what();
      throw;
    }
  }
  
  BOOST_LOG_TRIVIAL(info) << "Completed lock resource binding for all tasks";
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



// 处理锁资源的辅助函数
vertex_ptpn handle_locks(PriorityTimePetriNet* ptpn, const TaskNodeNames& names,
                 vector<vertex_ptpn>& chain, const vector<string>& locks,
                 const vector<pair<int,int>>& times, int priority, int core) {
  vertex_ptpn last_lock_place;
  vertex_ptpn first_unlock_trans;

  // 获取ready库所（在chain中的倒数第三个位置）
  vertex_ptpn ready = chain[chain.size()-3];

  for (size_t i = 0; i < locks.size(); i++) {
    // 获取锁
    string gl = names.get_lock + locks[i];
    vertex_ptpn get_lock = ptpn->add_transition(
        ptpn->ptpn, gl, PTPNTransition{priority, {0, 0}, core});
    vertex_ptpn deal = ptpn->add_place(ptpn->ptpn, names.deal + locks[i], 0);
    
    // 如果是第一个锁，将ready库所与获取锁的变迁连接
    if (i == 0) {
      ptpn->add_edge(ready, get_lock, ptpn->ptpn);
    } else {
      ptpn->add_edge(chain.back(), get_lock, ptpn->ptpn);
    }
    chain.push_back(get_lock);
    chain.push_back(deal);
    ptpn->add_edge(get_lock, deal, ptpn->ptpn);

    // 记录最后一个锁的库所
    last_lock_place = deal;
  }
  
  vertex_ptpn last_unlocked_place;
  // 释放锁
  for (int j = locks.size() - 1; j >= 0; j--) {
    string dl = names.drop_lock + locks[j];
    vertex_ptpn drop_lock = ptpn->add_transition(
        ptpn->ptpn, dl, PTPNTransition{priority, times[j], core});
    vertex_ptpn unlocked = ptpn->add_place(
        ptpn->ptpn, names.unlock + locks[j], 0);
  
    ptpn->add_edge(chain.back(), drop_lock, ptpn->ptpn);
    ptpn->add_edge(drop_lock, unlocked, ptpn->ptpn);
    
    chain.push_back(drop_lock);
    chain.push_back(unlocked);

    // 记录第一个解锁的变迁
    if (j == locks.size() - 1) {
      first_unlock_trans = drop_lock;
    }
    if (j == 0) {
      last_unlocked_place = unlocked;
    }
  } 

  // 将最后一个锁的库所与第一个解锁的变迁连接起来
  if (last_lock_place && first_unlock_trans) {
    ptpn->add_edge(last_lock_place, first_unlock_trans, ptpn->ptpn);
  }

  return last_unlocked_place;
}

pair<vertex_ptpn, vertex_ptpn>
PriorityTimePetriNet::add_p_node_ptpn(PeriodicTask &p_task) {
  TaskNodeNames names(p_task.name);
  
  // 创建周期任务特有的随机触发结构
  string task_random_period = p_task.name + "random";
  vertex_ptpn random = add_place(ptpn, task_random_period, 1);
  vertex_ptpn fire = add_transition(ptpn, p_task.name + "fire",
      PTPNTransition{p_task.priority, p_task.period_time, p_task.core});
  period_transitions_id.push_back(fire);
  // 创建基本任务结构
  BasicTaskStructure basic(this, names, p_task.priority, p_task.core, 
                          p_task.time.back());
  
  // 添加周期任务特有的边
  add_edge(random, fire, ptpn);
  add_edge(fire, basic.entry, ptpn);
  
  // 处理锁资源
  if (!p_task.lock.empty()) {
    vertex_ptpn last_place = handle_locks(this, names, basic.task_pt_chain, 
                                          p_task.lock, p_task.time,
                                          p_task.priority, p_task.core);
    add_edge(last_place, basic.exec, ptpn);
  } else {
    add_edge(basic.ready, basic.exec, ptpn);
  }
  
  // 完成任务链
  basic.task_pt_chain.push_back(basic.exec);
  basic.task_pt_chain.push_back(basic.exit);
  
  // 记录任务映射
  vector<vector<vertex_ptpn>> task_pt_chains{basic.task_pt_chain};
  node_pn_map.insert({p_task.name, basic.task_pt_chain});
  task_pn_map.insert({p_task.name, task_pt_chains});
  
  return {basic.entry, basic.exit};
}

pair<vertex_ptpn, vertex_ptpn>
PriorityTimePetriNet::add_ap_node_ptpn(APeriodicTask &ap_task) {
  TaskNodeNames names(ap_task.name);
  
  // 创建基本任务结构
  BasicTaskStructure basic(this, names, ap_task.priority, ap_task.core,
                          ap_task.time.back());
  
  // 处理锁资源
  if (!ap_task.lock.empty()) {
    vertex_ptpn last_place = handle_locks(this, names, basic.task_pt_chain, 
                                          ap_task.lock, ap_task.time,
                                          ap_task.priority, ap_task.core);
    add_edge(last_place, basic.exec, ptpn);
  } else {
    add_edge(basic.ready, basic.exec, ptpn);
  }
  
  // 完成任务链
  basic.task_pt_chain.push_back(basic.exec);
  basic.task_pt_chain.push_back(basic.exit);
  
  // 记录任务映射
  vector<vector<vertex_ptpn>> task_pt_chains{basic.task_pt_chain};
  node_pn_map.insert({ap_task.name, basic.task_pt_chain});
  task_pn_map.insert({ap_task.name, task_pt_chains});
  
  return {basic.entry, basic.exit};
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
    
  // 处理单个任务的抢占
  auto handle_task_preemption = [&](const string& l_t_name, const string& h_t_name,
                                   const TaskConfig& l_tc, const TaskConfig& h_tc,
                                   const vector<vertex_ptpn>& l_t_pn,
                                   const vector<vertex_ptpn>& h_t_pn,
                                   bool is_interrupt) {
    int task_pn_size = l_t_pn.size();
    ptpn[l_t_pn[task_pn_size - 2]].pnt.is_handle = true;
    
    if (is_interrupt) {
      create_task_priority(h_t_name, l_t_pn[task_pn_size - 3], 
                          l_t_pn[task_pn_size - 2], l_t_pn[0], 
                          l_t_pn.back(), nodes_type.at(h_t_name));
    } else {
      create_hlf_task_priority(h_t_name, l_t_pn[task_pn_size - 3],
                              l_t_pn[task_pn_size - 2], h_t_pn[0], 
                              l_t_pn[0], h_t_pn[2], h_tc.priority, h_tc.core);
    }
    
    // 处理锁
    if (!l_tc.locks.empty()) {
      constexpr size_t MIN_CHAIN_LENGTH = 9;
      // 检查链长度是否满足最小要求
      if (l_t_pn.size() < MIN_CHAIN_LENGTH) {
        BOOST_LOG_TRIVIAL(debug) << "Skip chain for " << l_t_name 
                                << ": too short for locks";
        return;
      }
      for (size_t i = 0; i < l_tc.locks.size(); i++) {
        if (l_tc.locks[i].find("spin") != string::npos) break;
        
        size_t idx = task_pn_size - 2 - 2 * (i + 1);
        ptpn[l_t_pn[idx]].pnt.is_handle = true;
        
        if (is_interrupt) {
          create_task_priority(h_t_name, l_t_pn[idx - 1], l_t_pn[idx],
                              l_t_pn[0], l_t_pn.back(), nodes_type.at(h_t_name));
        } else {
          create_hlf_task_priority(h_t_name, l_t_pn[idx - 1], l_t_pn[idx],
                                  h_t_pn[0], l_t_pn[0], h_t_pn[2],
                                  h_tc.priority, h_tc.core);
        }
      }
    }
  };

  // 主循环
  for (const auto& [core_id, tasks] : core_task) {
    BOOST_LOG_TRIVIAL(debug) << "Processing core: " << core_id;
    
    for (auto l_t = tasks.begin(); l_t != tasks.end() - 1; l_t++) {
      const string& l_t_name = *l_t;
      const TaskConfig& l_tc = tc.at(l_t_name);
      
      for (auto h_t = l_t + 1; h_t != tasks.end(); h_t++) {
        const string& h_t_name = *h_t;
        const TaskConfig& h_tc = tc.at(h_t_name);
        
        if (l_tc.priority == h_tc.priority) continue;
        
        auto l_t_pns = task_pn_map.find(l_t_name);
        auto h_t_pns = task_pn_map.find(h_t_name);
        if (l_t_pns == task_pn_map.end() || h_t_pns == task_pn_map.end()) continue;
        
        const auto& h_t_pn = h_t_pns->second[0];
        bool is_interrupt = false;
        
        if (auto node_type = nodes_type.find(h_t_name); 
            node_type != nodes_type.end() && 
            holds_alternative<PeriodicTask>(node_type->second)) {
          auto p_task = get<PeriodicTask>(node_type->second);
          is_interrupt = (p_task.task_type == TaskType::INTERRUPT);
        }
        
        for (const auto& l_t_pn : l_t_pns->second) {
          handle_task_preemption(l_t_name, h_t_name, l_tc, h_tc, 
                               l_t_pn, h_t_pn, is_interrupt);
        }
        
        BOOST_LOG_TRIVIAL(debug) << "Priority " << l_t_name << ": " 
                                << l_tc.priority << " " << h_t_name 
                                << ": " << h_tc.priority;
      }
    }
  }
}

/// \brief 根据软件学报论文创建抢占变迁
/// \param name 高优先级任务的名字
/// \param preempt_vertex 发生抢占的库所
/// \param handle_t 可挂起的变迁
/// \param start 高优先级的开始库所
/// \param end 高优先��的结束库所
/// \param task_type 高优先级任务类型
void PriorityTimePetriNet::create_task_priority(
    const std::string &name, vertex_ptpn preempt_vertex, size_t handle_t,
    vertex_ptpn start, vertex_ptpn end, NodeType task_type) {
  
  // 设置处理变迁标志
  ptpn[handle_t].pnt.is_handle = true;

  // 名字被占用，加index区分
 struct TaskNodeNames {
    string get_core, ready, get_lock, deal, drop_lock, unlock, exec;
    
    explicit TaskNodeNames(const string& base_name, int index) {
      get_core = base_name + "get_core" + to_string(index);
      ready = base_name + "ready" + to_string(index);
      get_lock = base_name + "get_lock" + to_string(index);
      deal = base_name + "deal" + to_string(index);
      drop_lock = base_name + "drop_lock" + to_string(index);
      unlock = base_name + "unlocked" + to_string(index);
      exec = base_name + "exec" + to_string(index);
    }
  };

  TaskNodeNames node_names(name, node_index);
  std::vector<vertex_ptpn> node{start};

  // 创建基本任务结构的辅助函数
  auto create_basic_structure = [&](int priority, int core, const std::pair<int,int>& exec_time) {
    // 创建获取CPU和就绪节点
    vertex_ptpn get_core = add_transition(
        ptpn, node_names.get_core,
        PTPNTransition{priority, {0, 0}, core});
    vertex_ptpn ready = add_place(ptpn, node_names.ready, 0);
    vertex_ptpn exec = add_transition(
        ptpn, node_names.exec,
        PTPNTransition{priority, exec_time, core});

    // 添加基本边
    add_edge(start, get_core, ptpn);
    add_edge(preempt_vertex, get_core, ptpn);
    add_edge(get_core, ready, ptpn);
    add_edge(exec, end, ptpn);
    add_edge(exec, preempt_vertex, ptpn);

    // 更新节点序列
    node.push_back(start);
    node.push_back(get_core);
    node.push_back(ready);

    return std::make_tuple(ready, exec);
  };

  // 处理锁相关结构的辅助函数
  auto handle_locks = [&](const vector<string>& locks, const vector<pair<int,int>>& times, int core) {
    if (locks.empty()) {
      return;
    }
    // 添加获取锁的结构
    for (size_t j = 0; j < locks.size(); j++) {
      if (locks[j].find("spin") != string::npos) break;
      std::string gl = node_names.get_lock + locks[j] + to_string(node_index);
      vertex_ptpn get_lock = add_transition(ptpn, gl, PTPNTransition{256, {0, 0}, core});
      vertex_ptpn deal = add_place(ptpn, node_names.deal + locks[j], 0);
      
      add_edge(node.back(), get_lock, ptpn);
      node.push_back(get_lock);
      node.push_back(deal);
      add_edge(get_lock, deal, ptpn);


    }

    // 添加释放锁的结构
    for (int k = locks.size() - 1; k >= 0; k--) {
      std::string dl = node_names.drop_lock + locks[k] + to_string(node_index);
      vertex_ptpn drop_lock = add_transition(ptpn, dl, PTPNTransition{256, times[k], core});
      vertex_ptpn unlocked = add_place(ptpn, node_names.unlock + locks[k], 0);
      
      add_edge(node.back(), drop_lock, ptpn);
      add_edge(drop_lock, unlocked, ptpn);
      
      if (locks.size() == 1 || k == 0) {
        add_edge(unlocked, node.back(), ptpn);
      }
      
      node.push_back(drop_lock);
      node.push_back(unlocked);
    }
  };

  // 处理不同类型的任务
  if (holds_alternative<APeriodicTask>(task_type)) {
    auto task = get<APeriodicTask>(task_type);
    auto [ready, exec] = create_basic_structure(task.priority, task.core, task.time.back());
    
    if (task.lock.empty()) {
      add_edge(ready, exec, ptpn);
    } else {
      handle_locks(task.lock, task.time, task.core);
      add_edge(node.back(), exec, ptpn);
    }
    
    node.push_back(exec);
    node.push_back(end);
    task_pn_map.find(task.name)->second.push_back(node);
    
  } else if (holds_alternative<PeriodicTask>(task_type)) {
    auto task = get<PeriodicTask>(task_type);
    auto [ready, exec] = create_basic_structure(task.priority, task.core, task.time.back());
    
    if (task.lock.empty()) {
      add_edge(ready, exec, ptpn);
    } else {
      handle_locks(task.lock, task.time, task.core);
      add_edge(node.back(), exec, ptpn);
    }
    
    node.push_back(exec);
    node.push_back(end);
    task_pn_map.find(task.name)->second.push_back(node);
    
  } else {
    BOOST_LOG_TRIVIAL(error) << "unreachable!";
    return;
  }

  node_index++;
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

bool PriorityTimePetriNet::verify_petri_net_structure() {
  bool is_valid = true;
    
    // 遍历所有顶点
    for (auto [vi, vi_end] = vertices(ptpn); vi != vi_end; ++vi) {
        const auto& vertex = ptpn[*vi];
        
        if (vertex.shape == "box") {  // 变迁
            // 检查1：变迁的前后节点必须是库所且不能为空
            bool has_input = false;
            bool has_output = false;
            bool all_inputs_are_places = true;
            bool all_outputs_are_places = true;
            
            // 检查入边
            for (auto [ei, ei_end] = in_edges(*vi, ptpn); ei != ei_end; ++ei) {
                has_input = true;
                vertex_ptpn pre = source(*ei, ptpn);
                if (ptpn[pre].shape != "circle") {
                    all_inputs_are_places = false;
                    BOOST_LOG_TRIVIAL(error) << "变迁 " << vertex.name 
                        << " 的前置节点 " << ptpn[pre].name 
                        << " 不是库所";
                }
            }
            
            // 检查出边
            for (auto [ei, ei_end] = out_edges(*vi, ptpn); ei != ei_end; ++ei) {
                has_output = true;
                vertex_ptpn suc = target(*ei, ptpn);
                if (ptpn[suc].shape != "circle") {
                    all_outputs_are_places = false;
                    BOOST_LOG_TRIVIAL(error) << "变迁 " << vertex.name 
                        << " 的后继节点 " << ptpn[suc].name 
                        << " 不是库所";
                }
            }
            
            if (!has_input || !has_output) {
                is_valid = false;
                BOOST_LOG_TRIVIAL(error) << "变迁 " << vertex.name 
                    << " 的前置或后继节点为空";
            }
            
            if (!all_inputs_are_places || !all_outputs_are_places) {
                is_valid = false;
            }
            
            // 检查4：变迁时间约束的有效性
            if (vertex.pnt.const_time.first < 0 || 
                vertex.pnt.const_time.second < vertex.pnt.const_time.first) {
                is_valid = false;
                BOOST_LOG_TRIVIAL(error) << "变迁 " << vertex.name 
                    << " 的时间约束无效: [" 
                    << vertex.pnt.const_time.first << "," 
                    << vertex.pnt.const_time.second << "]";
            }
            
        } else if (vertex.shape == "circle") {  // 库所
            // 检查2：库所的前后节点必须是变迁（但可以为空）
            bool all_inputs_are_transitions = true;
            bool all_outputs_are_transitions = true;
            
            // 检查入边
            for (auto [ei, ei_end] = in_edges(*vi, ptpn); ei != ei_end; ++ei) {
                vertex_ptpn pre = source(*ei, ptpn);
                if (ptpn[pre].shape != "box") {
                    all_inputs_are_transitions = false;
                    BOOST_LOG_TRIVIAL(error) << "库所 " << vertex.name 
                        << " 的前置节点 " << ptpn[pre].name 
                        << " 不是变迁";
                }
            }
            
            // 检查出边
            for (auto [ei, ei_end] = out_edges(*vi, ptpn); ei != ei_end; ++ei) {
                vertex_ptpn suc = target(*ei, ptpn);
                if (ptpn[suc].shape != "box") {
                    all_outputs_are_transitions = false;
                    BOOST_LOG_TRIVIAL(error) << "库所 " << vertex.name 
                            << " 的后继节点 " << ptpn[suc].name 
                        << " 不是变迁";
                }
            }
            
            if (!all_inputs_are_transitions || !all_outputs_are_transitions) {
                is_valid = false;
            }
            
            // 检查3：token数量必须是0或1
            if (vertex.token < 0 || vertex.token > 1) {
                is_valid = false;
                BOOST_LOG_TRIVIAL(error) << "库所 " << vertex.name 
                    << " 的token数量无效: " << vertex.token;
            }
        }
    }
    
    if (is_valid) {
        BOOST_LOG_TRIVIAL(info) << "Petri网结构验证通过";
    } else {
        BOOST_LOG_TRIVIAL(error) << "Petri网结构验证失败";
    }
    
    return is_valid;
}
