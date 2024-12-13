#include "state_class_graph.h"
#include <future>
#include <iostream>

bool StateClassGraph::is_transition_enabled(const PTPN& ptpn, vertex_ptpn v) {
  // 检查是否是变迁
  if (ptpn[v].shape != "box") {
    return false;
  }

  bool has_non_place = false;
  bool all_places_enabled = true;

  // 遍历所有入边
  for (auto [ei, ei_end] = boost::in_edges(v, ptpn); ei != ei_end; ++ei) {
    vertex_ptpn source = boost::source(*ei, ptpn);
    
    // 检查前置节点是否为库所
    if (ptpn[source].shape != "circle") {
      has_non_place = true;
      BOOST_LOG_TRIVIAL(error) << "Transition " << ptpn[v].name 
                              << " has non-place predecessor: " 
                              << ptpn[source].name;
      continue;
    }

    // 检查库所token数量
    if (ptpn[source].token < 1) {
      all_places_enabled = false;
      break;
    }
  }

  if (has_non_place) {
    return false;
  }

  return all_places_enabled;
}

StateClass StateClassGraph::get_initial_state_class(const PTPN& source_ptpn) {
    Marking mark;
    std::set<T_wait> all_t;
    for (auto [vi, vi_end] = vertices(source_ptpn); vi != vi_end; ++vi) {
      if (source_ptpn[*vi].shape == "circle" && source_ptpn[*vi].token > 0) {
        mark.indexes.insert(*vi);
        mark.labels.insert(source_ptpn[*vi].name);
      }
      // 如果是变迁且使能
      if (source_ptpn[*vi].shape == "box" && is_transition_enabled(source_ptpn, *vi)) {
        all_t.insert(T_wait(*vi, 0));
      }
    } 
    return StateClass(mark, all_t);
}

std::string StateClass::to_scg_vertex() {
  std::string labels;
  std::string times;
  for (const auto &l : mark.labels) {
    labels.append(l);
  }
  for (const auto &t : all_t) {
    times.append(std::to_string(t.t))
        .append(":")
        .append(std::to_string(t.time).append(";"));
  }
  return labels + times;
}


ScgVertexD StateClassGraph::add_scg_vertex(StateClass sc) {
  ScgVertexD svd;
  if (scg_vertex_map.find(sc) != scg_vertex_map.end()) {
    svd = scg_vertex_map.find(sc)->second;
  } else {
    svd = add_vertex(SCGVertex{sc.to_scg_vertex(), sc.to_scg_vertex()},
                            scg);
    scg_vertex_map.insert(std::make_pair(sc, svd));
  }
  return svd;
}

void StateClassGraph::set_state_class(const StateClass &state_class) {
  // 首先重置原有状态
  boost::graph_traits<PTPN>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = vertices(*init_ptpn); vi != vi_end; ++vi) {
    (*init_ptpn)[*vi].token = 0;
    (*init_ptpn)[*vi].enabled = false;
    (*init_ptpn)[*vi].pnt.runtime = 0;
  }
  // 2. 设置标识
  for (auto m : state_class.mark.indexes) {
    (*init_ptpn)[m].token = 1;
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
    (*init_ptpn)[t.t].pnt.runtime = t.time;
  }
}

std::vector<SchedT> StateClassGraph::get_sched_transitions(const StateClass &state_class) {
  BOOST_LOG_TRIVIAL(debug) << "Getting schedulable transitions";
  set_state_class(state_class);

  // 获取使能的变迁
  auto enabled_transitions = get_enabled_transitions();
  if (enabled_transitions.empty()) {
    return {};
  }
  // 计算发生时间区间
  auto fire_time = calculate_fire_time_domain(enabled_transitions);
  
  // 获取满足时间约束的变迁
  auto sched_transitions = get_time_satisfied_transitions(enabled_transitions, fire_time);

  // 应用优先级规则
  apply_priority_rules(sched_transitions);

  // 日志输出
  for (const auto& sched : sched_transitions) {
    BOOST_LOG_TRIVIAL(debug) << (*init_ptpn)[sched.t].label;
  }

  return sched_transitions;
}

std::vector<vertex_ptpn> StateClassGraph::get_enabled_transitions() {
  std::vector<vertex_ptpn> enabled_t_s;
  
  for (auto [vi, vi_end] = boost::vertices(*init_ptpn); vi != vi_end; ++vi) {
    if ((*init_ptpn)[*vi].shape != "box") continue;
    
    if (is_transition_enabled(*init_ptpn, *vi)) {
      enabled_t_s.push_back(*vi);
    }
  }
  
  return enabled_t_s;
}

std::pair<int, int> StateClassGraph::calculate_fire_time_domain(const std::vector<vertex_ptpn>& enabled_t_s) {
  std::pair<int, int> fire_time = {0, INT_MAX};
  
  for (auto t : enabled_t_s) {
    const auto& trans = (*init_ptpn)[t];
    int f_min = std::max(0, trans.pnt.const_time.first - trans.pnt.runtime);
    int f_max = trans.pnt.const_time.second - trans.pnt.runtime;
    
    if (f_max < 0) {
      BOOST_LOG_TRIVIAL(debug) << "变迁 " << trans.name << " 已超时，设置为立即发生";
      return {0, 0};
    }

    fire_time.first = std::min(fire_time.first, f_min);
    if (f_max < fire_time.second) {
      fire_time.second = f_max;
    }

    if (f_min > f_max) {
      BOOST_LOG_TRIVIAL(error) << "calculate_fire_time_domain error: min(" 
                              << f_min << ") > max(" 
                              << f_max << ")";
    }
  }
  if (fire_time.first > fire_time.second) {
    BOOST_LOG_TRIVIAL(debug) << "调整无效的时间区间为立即发生";
    return {0, 0};
  }
  return fire_time;
}

std::vector<SchedT> StateClassGraph::get_time_satisfied_transitions(
    const std::vector<vertex_ptpn>& enabled_t_s,
    const std::pair<int, int>& fire_time) {
  std::vector<SchedT> sched_T;
  
  for (auto t : enabled_t_s) {
    const auto& trans = (*init_ptpn)[t];
    int t_min = std::max(0, trans.pnt.const_time.first - trans.pnt.runtime);
    int t_max = trans.pnt.const_time.second - trans.pnt.runtime;
    
    // 添加日志输出以便调试
    BOOST_LOG_TRIVIAL(debug) << "Transition " << trans.name 
                            << ": t_min=" << t_min 
                            << ", t_max=" << t_max
                            << ", fire_time=(" << fire_time.first 
                            << "," << fire_time.second << ")";
    if (t_max < 0) {
      BOOST_LOG_TRIVIAL(debug) << "变迁 " << trans.name 
                                << " 已超过最大等待时间，应该已经发生";
      // 这种情况下应该立即发生
      sched_T.push_back(SchedT{t, {0, 0}});
      continue;
    }
    std::pair<int, int> sched_time;
    // 只检查最小值是否超出区间
    if (t_min > fire_time.second) {
      BOOST_LOG_TRIVIAL(debug) << "跳过变迁 " << trans.name 
                              << ": 最小发生时间超出区间";
      sched_time = fire_time;
    } else if (t_max > fire_time.second) {
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
    

    // 验证时间窗口的有效性
    if (sched_time.first > sched_time.second) {
      BOOST_LOG_TRIVIAL(error) << "Invalid time domain: min(" 
                              << sched_time.first << ") > max(" 
                              << sched_time.second << ")";
      continue;
    }
    
    sched_T.push_back(SchedT{t, sched_time});
  }
  
  return sched_T;
}

void StateClassGraph::apply_priority_rules(std::vector<SchedT>& sched_T) {
  for (auto it = sched_T.begin(); it != sched_T.end(); ++it) {
    auto next = it + 1;
    while (next != sched_T.end()) {
      const auto& t1 = (*init_ptpn)[it->t];
      const auto& t2 = (*init_ptpn)[next->t];
      
      if (t1.pnt.priority > t2.pnt.priority && t1.pnt.c == t2.pnt.c) {
        next = sched_T.erase(next);
      } else {
        ++next;
      }
    }
  }
}

void StateClassGraph::apply_priority_rules(std::vector<std::size_t>& enabled_t) {
  for (auto it = enabled_t.begin(); it != enabled_t.end(); ++it) {
    auto next = it + 1;
    while (next != enabled_t.end()) {
      const auto& t1 = (*init_ptpn)[*it];
      const auto& t2 = (*init_ptpn)[*next];
      
      if (t1.pnt.priority > t2.pnt.priority && t1.pnt.c == t2.pnt.c) {
        next = enabled_t.erase(next);
      } else {
        ++next;
      }
    }
  }
}

StateClass StateClassGraph::fire_transition(const StateClass &sc, SchedT transition) {
  BOOST_LOG_TRIVIAL(debug) << "fire_transition: " << transition.t;
  set_state_class(sc);

  // 1. 获取当前使能的变迁
  auto [enabled_t, old_enabled_t] = get_enabled_transitions_with_history();
  
  // 2. 应用优先级规则
  apply_priority_rules(enabled_t);

  // 3. 更新变迁等待时间
  update_transition_times(enabled_t, transition);
  
  // 4. 执行变迁
  execute_transition(transition);
  
  // 5. 获取新标识和新使能变迁
  auto [new_mark, new_enabled_t] = get_new_marking_and_enabled();
  
  // 6. 计算新的等待时间集合
  auto all_t = calculate_new_wait_times(old_enabled_t, new_enabled_t);

  return {new_mark, all_t};
}


std::pair<std::vector<std::size_t>, std::vector<std::size_t>> 
StateClassGraph::get_enabled_transitions_with_history() {
  std::vector<std::size_t> enabled_t, old_enabled_t;
  
  for (auto [vi, vi_end] = boost::vertices(*init_ptpn); vi != vi_end; ++vi) {
    if ((*init_ptpn)[*vi].shape == "circle") continue;
    
    bool enable_t = is_transition_enabled(*init_ptpn, *vi);
    if (enable_t) enabled_t.push_back(*vi);
    old_enabled_t.push_back(*vi);
  }
  
  return {enabled_t, old_enabled_t};
}

void StateClassGraph::update_transition_times(const std::vector<std::size_t>& enabled_t, 
                           const SchedT& transition) {
  int time_increment = (transition.time.second == transition.time.first) 
                      ? transition.time.first 
                      : (transition.time.second - transition.time.first);
                      
  for (auto t : enabled_t) {
    (*init_ptpn)[t].pnt.runtime += time_increment;
  }
  (*init_ptpn)[transition.t].pnt.runtime = 0;
}

void StateClassGraph::execute_transition(const SchedT& transition) {
  // 清空前置库所
  for (auto [in_i, in_end] = boost::in_edges(transition.t, *init_ptpn); 
       in_i != in_end; ++in_i) {
    vertex_ptpn place = boost::source(*in_i, *init_ptpn);
    if ((*init_ptpn)[place].token < 0) {
      BOOST_LOG_TRIVIAL(error) << "Place token <= 0, transition not enabled";
    }
    (*init_ptpn)[place].token = 0;
  }
  
  // 设置后继库所
  for (auto [out_i, out_end] = boost::out_edges(transition.t, *init_ptpn);
       out_i != out_end; ++out_i) {
    vertex_ptpn place = boost::target(*out_i, *init_ptpn);
    if ((*init_ptpn)[place].token > 1) {
      BOOST_LOG_TRIVIAL(error) << "Unsafe Petri net, place token >= 1";
    }
    (*init_ptpn)[place].token = 1;
  }
}

std::pair<Marking, std::vector<std::size_t>> StateClassGraph::get_new_marking_and_enabled() {
  Marking new_mark;
  std::vector<std::size_t> new_enabled_t;
  
  for (auto [vi, vi_end] = boost::vertices(*init_ptpn); vi != vi_end; ++vi) {
    if ((*init_ptpn)[*vi].shape == "circle") {
      if ((*init_ptpn)[*vi].token == 1) {
        new_mark.indexes.insert(*vi);
        new_mark.labels.insert((*init_ptpn)[*vi].label);
      }
    } else if (is_transition_enabled(*init_ptpn, *vi)) {
      new_enabled_t.push_back(*vi);
    }
  }
  
  return {new_mark, new_enabled_t};
}

std::set<T_wait> StateClassGraph::calculate_new_wait_times(
    const std::vector<std::size_t>& old_enabled_t,
    const std::vector<std::size_t>& new_enabled_t) {
  std::set<T_wait> all_t;
  
  // 处理共同使能的变迁
  std::set<std::size_t> common;
  std::set_intersection(old_enabled_t.begin(), old_enabled_t.end(),
                       new_enabled_t.begin(), new_enabled_t.end(),
                       std::inserter(common, common.begin()));
  for (auto t : common) {
    all_t.insert({t, (*init_ptpn)[t].pnt.runtime});
  }
  
  // 处理失去使能的变迁
  std::set<std::size_t> disabled;
  std::set_difference(old_enabled_t.begin(), old_enabled_t.end(),
                     new_enabled_t.begin(), new_enabled_t.end(),
                     std::inserter(disabled, disabled.begin()));
  for (auto t : disabled) {
    if ((*init_ptpn)[t].pnt.is_handle) {
      all_t.insert({t, (*init_ptpn)[t].pnt.runtime});
    } else {
      (*init_ptpn)[t].pnt.runtime = 0;
    }
  }
  
  // 处理新获得使能的变迁
  std::set<std::size_t> newly_enabled;
  std::set_difference(new_enabled_t.begin(), new_enabled_t.end(),
                     old_enabled_t.begin(), old_enabled_t.end(),
                     std::inserter(newly_enabled, newly_enabled.begin()));
  for (auto t : newly_enabled) {
    all_t.insert({t, (*init_ptpn)[t].pnt.is_handle ? 
                     (*init_ptpn)[t].pnt.runtime : 0});
  }
  
  return all_t;
}

void StateClassGraph::generate_state_class() {
  BOOST_LOG_TRIVIAL(info) << "Generating state class";
  auto start_time = chrono::steady_clock::now();
  
  // 初始化队列和状态集
  std::queue<StateClass> state_queue;
  std::set<StateClass> visited_states;  // 记录已访问的状态
  
  // 处理初始状态
  state_queue.push(init_state_class);
  visited_states.insert(init_state_class);
  
  // 添加初始顶点
  std::string vertex_label = init_state_class.to_scg_vertex();
  ScgVertexD init_vertex = add_vertex(SCGVertex{vertex_label}, scg);
  scg_vertex_map.insert(std::make_pair(init_state_class, init_vertex));

  int state_count = 0;
  while (!state_queue.empty()) {
    StateClass current_state = state_queue.front();
    state_queue.pop();
    ScgVertexD current_vertex = add_scg_vertex(current_state);

    // 获取当前状态的所有可调度变迁
    std::vector<SchedT> sched_transitions = get_sched_transitions(current_state);
    // 检查sched_transitions如果只有周期变迁，那么为死锁
    if (!sched_transitions.empty()) {
    // 获取当前状态对应的顶点
    ScgVertexD current_vertex = add_scg_vertex(current_state);
    
    // 如果不是初始状态(顶点索引不为0)，则检查是否所有可调度变迁都是周期变迁
    if (current_vertex != 0) {
       bool all_periodic = true;
       for (const auto& trans : sched_transitions) {
          // 检查变迁是否在周期变迁列表中
          if (std::find(period_transitions_id.begin(), 
                      period_transitions_id.end(), 
                      trans.t) == period_transitions_id.end()) {
               all_periodic = false;
            break;
          }
        }
          
        // 如果所有可调度变迁都是周期变迁，则标记为死锁状态
        if (all_periodic) {
            BOOST_LOG_TRIVIAL(warning) << "Deadlock state detected: only periodic transitions enabled";
            deadlock_sc_sets.insert(current_state);
            continue;  // 跳过这个状态的后续处理
        }
      }
    } else {
        // 如果没有可调度变迁，也是死锁状态
        deadlock_sc_sets.insert(current_state);
    }

    // 处理每个可调度变迁
    for (const auto& transition : sched_transitions) {
      // 计算新状态
      StateClass new_state = fire_transition(current_state, transition);
      
      // 检查是否是新状态
      if (visited_states.find(new_state) == visited_states.end()) {
        state_queue.push(new_state);
        visited_states.insert(new_state);
        state_count++;
        
        // 添加新顶点和边
        auto new_vertex = add_scg_vertex(new_state);
        SCGEdge edge = {
          std::to_string(transition.time.first) + ":" + 
          std::to_string(transition.time.second)
        };
        add_edge(current_vertex, new_vertex, edge, scg);
        
        BOOST_LOG_TRIVIAL(debug) << "Added new state: " << state_count;
      } else {
        // 状态已存在，只添加边
        auto existing_vertex = scg_vertex_map.find(new_state)->second;
        SCGEdge edge = {
          std::to_string(transition.time.first) + ":" + 
          std::to_string(transition.time.second)
        };
        add_edge(current_vertex, existing_vertex, edge, scg);
      }
    }
  }

  // 输出统计信息
  auto duration = chrono::duration<double>(chrono::steady_clock::now() - start_time);
  BOOST_LOG_TRIVIAL(info) << "State class generation completed:";
  BOOST_LOG_TRIVIAL(info) << "- Time taken: " << duration.count() << "s";
  BOOST_LOG_TRIVIAL(info) << "- Total states: " << num_vertices(scg);
  BOOST_LOG_TRIVIAL(info) << "- Unique states: " << visited_states.size();
}

void StateClassGraph::generate_state_class_with_thread(int num_threads) {
    BOOST_LOG_TRIVIAL(info) << "Starting state class generation with " << num_threads << " threads";
    
    // 1. 检查初始化
    if (!init_ptpn) {
        BOOST_LOG_TRIVIAL(error) << "init_ptpn is null";
        throw std::runtime_error("init_ptpn not initialized");
    }

    try {
        // 2. 获取初始状态类
        init_state_class = get_initial_state_class(*init_ptpn);
        BOOST_LOG_TRIVIAL(debug) << "Initial state class created";
        
        // 3. 初始化队列
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            while (!pending_states.empty()) {
                pending_states.pop(); // 清空队列
            }
            pending_states.push(init_state_class);
        }
        
        // 4. 初始化SCG图
        {
            std::lock_guard<std::mutex> lock(scg_mutex);
            // 添加初始顶点
            ScgVertexD init_vertex = add_vertex(SCGVertex{init_state_class.to_scg_vertex()}, scg);
            scg_vertex_map[init_state_class] = init_vertex;
        }
        
        // 5. 重置状态
        processing_complete = false;
        active_threads = 0;
        
        // 6. 创建PTPN副本
        thread_ptpns.clear();
        for (int i = 0; i < num_threads; ++i) {
    BOOST_LOG_TRIVIAL(debug) << "Creating deep copy of PTPN for thread " << i;
    
    // 创建新的PTPN实例
    auto thread_ptpn = std::make_unique<PTPN>();
    
    // 复制顶点
    std::map<vertex_ptpn, vertex_ptpn> vertex_map;
    for (auto [vi, vi_end] = vertices(*init_ptpn); vi != vi_end; ++vi) {
        // 深拷贝顶点属性
        auto new_vertex = add_vertex(PTPNVertex((*init_ptpn)[*vi]), *thread_ptpn);
        vertex_map[*vi] = new_vertex;
    }
            
    // 复制边
    for (auto [ei, ei_end] = edges(*init_ptpn); ei != ei_end; ++ei) {
        vertex_ptpn src = source(*ei, *init_ptpn);
        vertex_ptpn tgt = target(*ei, *init_ptpn);
        
        // 使用vertex_map找到对应的新顶点
        add_edge(vertex_map[src], vertex_map[tgt], 
                PTPNEdge((*init_ptpn)[*ei]), *thread_ptpn);
    }
    
    // 使用 push_back 时使用 std::move
    thread_ptpns.push_back(std::move(thread_ptpn));
    BOOST_LOG_TRIVIAL(debug) << "PTPN deep copy completed for thread " << i;
        }
        
        // 7. 创建并启动工作线程
        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            BOOST_LOG_TRIVIAL(debug) << "Starting worker thread " << i;
            threads.emplace_back(&StateClassGraph::worker_thread, this, i);
        }
        
        // 8. 等待所有线程完成
        BOOST_LOG_TRIVIAL(debug) << "Waiting for threads to complete";
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        BOOST_LOG_TRIVIAL(info) << "State class generation completed";
        BOOST_LOG_TRIVIAL(info) << "Total states: " << num_vertices(scg);
        BOOST_LOG_TRIVIAL(info) << "Total transitions: " << num_edges(scg);
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Error in state class generation: " << e.what();
        throw;
    }
}

void StateClassGraph::worker_thread(int thread_id) {
    BOOST_LOG_TRIVIAL(debug) << "Worker thread " << thread_id << " started";
    
    try {
        // 检查线程的PTPN副本是否存在
        if (thread_id >= thread_ptpns.size() || !thread_ptpns[thread_id]) {
            throw std::runtime_error("Invalid thread PTPN");
        }
        
        auto& local_ptpn = thread_ptpns[thread_id];
        StateClass current_state;
        
        while (true) {
            // 获取下一个待处理的状态类
            if (!get_next_state(current_state)) {
                BOOST_LOG_TRIVIAL(debug) << "Thread " << thread_id << " completed";
                break;
            }
            
            BOOST_LOG_TRIVIAL(debug) << "Thread " << thread_id << " processing new state";
            process_state_class(current_state, *local_ptpn);
        }
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Error in worker thread " << thread_id << ": " << e.what();
        throw;
    }
}

bool StateClassGraph::get_next_state(StateClass& states) {
    std::unique_lock<std::mutex> lock(queue_mutex);
    
    cv.wait(lock, [this] {
        return !pending_states.empty() || processing_complete;
    });
    
    if (pending_states.empty()) {
        if (!processing_complete) {
            processing_complete = true;
            cv.notify_all();
        }
        return false;
    }
    
    // 批量获取状态
    if (!pending_states.empty()) {
        states = std::move(pending_states.front());
        pending_states.pop();
    }
    return true;
}

void StateClassGraph::process_state_class(const StateClass& state_class, PTPN& local_ptpn) {
    try {
        // 获取可调度变迁
        auto sched_transitions = get_sched_transitions(state_class, local_ptpn);
        
        // 对每个可调度变迁
        for (const auto& transition : sched_transitions) {
            // 发生变迁，得到新状态类
            StateClass new_state = fire_transition(state_class, transition, local_ptpn);
            
            bool is_new_state = false;
            {
                std::lock_guard<std::mutex> lock(scg_mutex);
                
                // 检查是否是新状态
                if (scg_vertex_map.find(new_state) == scg_vertex_map.end()) {
                    is_new_state = true;
                    // 添加新顶点
                    ScgVertexD new_vertex = add_vertex(SCGVertex{new_state.to_scg_vertex()}, scg);
                    scg_vertex_map[new_state] = new_vertex;
                }
                
                // 添加边
                ScgVertexD source_vertex = scg_vertex_map[state_class];
                ScgVertexD target_vertex = scg_vertex_map[new_state];
                
                SCGEdge edge = {
                    std::to_string(transition.time.first) + ":" + 
                    std::to_string(transition.time.second)
                };
                add_edge(source_vertex, target_vertex, edge, scg);
            }
            
            if (is_new_state) {
                std::lock_guard<std::mutex> lock(queue_mutex);
                pending_states.push(new_state);
                cv.notify_one();
            }
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Error processing state class: " << e.what();
        throw;
    }
}

void StateClassGraph::add_new_state(const StateClass& new_state) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    
    // 检查状态是否已存在
    if (sc_sets.find(new_state) == sc_sets.end()) {
        sc_sets.insert(new_state);
        pending_states.push(new_state);
        
        {
            std::lock_guard<std::mutex> scg_lock(scg_mutex);
            add_scg_vertex(new_state);
        }
        
        cv.notify_one();  // 通知等待的线程有新状态可处理
    }
}

std::vector<SchedT> StateClassGraph::get_sched_transitions(const StateClass &state_class, PTPN& local_ptpn) {
    BOOST_LOG_TRIVIAL(debug) << "Getting schedulable transitions";
    set_state_class(state_class, local_ptpn);  // 使用本地PTPN副本

    // 获取使能的变迁
    auto enabled_transitions = get_enabled_transitions(local_ptpn);
    if (enabled_transitions.empty()) {
        return {};
    }

    // 计算发生时间区间
    auto fire_time = calculate_fire_time_domain(enabled_transitions, local_ptpn);
    
    // 获取满足时间约束的变迁
    auto sched_transitions = get_time_satisfied_transitions(enabled_transitions, fire_time, local_ptpn);

    // 应用优先级规则
    apply_priority_rules(sched_transitions, local_ptpn);

    return sched_transitions;
}

StateClass StateClassGraph::fire_transition(const StateClass &sc, SchedT transition, PTPN& local_ptpn) {
    BOOST_LOG_TRIVIAL(debug) << "fire_transition: " << transition.t;
    set_state_class(sc, local_ptpn);

    // 1. 获取当前使能的变迁
    auto [enabled_t, old_enabled_t] = get_enabled_transitions_with_history(local_ptpn);
    
    // 2. 应用优先级规则
    apply_priority_rules(enabled_t, local_ptpn);

    // 3. 更新变迁等待时间
    update_transition_times(enabled_t, transition, local_ptpn);
    
    // 4. 执行变迁
      execute_transition(transition, local_ptpn);
    
    // 5. 获取新标识和新使能变迁
    auto [new_mark, new_enabled_t] = get_new_marking_and_enabled(local_ptpn);
    
    // 6. 计算新的等待时间集合
    auto all_t = calculate_new_wait_times(old_enabled_t, new_enabled_t, local_ptpn);

    return {new_mark, all_t};
}

void StateClassGraph::set_state_class(const StateClass &state_class, PTPN& local_ptpn) {
  // 首先重置原有状态
  boost::graph_traits<PTPN>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = vertices(local_ptpn); vi != vi_end; ++vi) {
    (local_ptpn)[*vi].token = 0;
    (local_ptpn)[*vi].enabled = false;
    (local_ptpn)[*vi].pnt.runtime = 0;
  }
  // 2. 设置标识
  for (auto m : state_class.mark.indexes) {
    (local_ptpn)[m].token = 1;
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
    (local_ptpn)[t.t].pnt.runtime = t.time;
  }
}

std::vector<vertex_ptpn> StateClassGraph::get_enabled_transitions(PTPN& local_ptpn) {
  std::vector<vertex_ptpn> enabled_t_s;
  
  for (auto [vi, vi_end] = boost::vertices(local_ptpn); vi != vi_end; ++vi) {
    if ((local_ptpn)[*vi].shape != "box") continue;
    
    if (is_transition_enabled(local_ptpn, *vi)) {
      enabled_t_s.push_back(*vi);
    }
  }
  
  return enabled_t_s;
}

std::pair<std::vector<std::size_t>, std::vector<std::size_t>> 
StateClassGraph::get_enabled_transitions_with_history(PTPN& local_ptpn) {
  std::vector<std::size_t> enabled_t, old_enabled_t;
  
  for (auto [vi, vi_end] = boost::vertices(local_ptpn); vi != vi_end; ++vi) {
    if (local_ptpn[*vi].shape == "circle") continue;
    
    bool enable_t = is_transition_enabled(local_ptpn, *vi);
    if (enable_t) enabled_t.push_back(*vi);
    old_enabled_t.push_back(*vi);
  }
  
  return {enabled_t, old_enabled_t};
}

std::pair<int, int> StateClassGraph::calculate_fire_time_domain(
    const std::vector<vertex_ptpn>& enabled_t_s, 
    PTPN& local_ptpn) {
  std::pair<int, int> fire_time = {0, INT_MAX};
  
  for (auto t : enabled_t_s) {
    const auto& trans = (local_ptpn)[t];
    int f_min = std::max(0, trans.pnt.const_time.first - trans.pnt.runtime);
    int f_max = trans.pnt.const_time.second - trans.pnt.runtime;
    
    if (f_max < 0) {
      BOOST_LOG_TRIVIAL(debug) << "变迁 " << trans.name << " 已超时，设置为立即发生";
      return {0, 0};
    }

    fire_time.first = std::min(fire_time.first, f_min);
    if (f_max < fire_time.second) {
      fire_time.second = f_max;
    }
  }
  return fire_time;
}

void StateClassGraph::execute_transition(const SchedT& transition, PTPN& local_ptpn) {
  // 清空前置库所
    for (auto [in_i, in_end] = boost::in_edges(transition.t, local_ptpn); 
       in_i != in_end; ++in_i) {
    vertex_ptpn place = boost::source(*in_i, local_ptpn);
    (local_ptpn)[place].token = 0;
  }
  
  // 设置后继库所
  for (auto [out_i, out_end] = boost::out_edges(transition.t, local_ptpn);
       out_i != out_end; ++out_i) {
    vertex_ptpn place = boost::target(*out_i, local_ptpn);
    (local_ptpn)[place].token = 1;
  }
}

std::pair<Marking, std::vector<std::size_t>> 
StateClassGraph::get_new_marking_and_enabled(PTPN& local_ptpn) {
  Marking new_mark;
  std::vector<std::size_t> new_enabled_t;
  
  for (auto [vi, vi_end] = boost::vertices(local_ptpn); vi != vi_end; ++vi) {
    if ((local_ptpn)[*vi].shape == "circle") {
      if ((local_ptpn)[*vi].token == 1) {
        new_mark.indexes.insert(*vi);
        new_mark.labels.insert((local_ptpn)[*vi].label);
      }
    } else if (is_transition_enabled(local_ptpn, *vi)) {
      new_enabled_t.push_back(*vi);
    }
  }
  
  return {new_mark, new_enabled_t};
}

std::vector<SchedT> StateClassGraph::get_time_satisfied_transitions(
    const std::vector<vertex_ptpn>& enabled_t_s,
    const std::pair<int, int>& fire_time, PTPN& local_ptpn) {
  std::vector<SchedT> sched_T;
  
  for (auto t : enabled_t_s) {
      const auto& trans = (local_ptpn)[t];
    int t_min = std::max(0, trans.pnt.const_time.first - trans.pnt.runtime);
    int t_max = trans.pnt.const_time.second - trans.pnt.runtime;
    
    // 添加日志输出以便调试
    BOOST_LOG_TRIVIAL(debug) << "Transition " << trans.name 
                            << ": t_min=" << t_min 
                            << ", t_max=" << t_max
                            << ", fire_time=(" << fire_time.first 
                            << "," << fire_time.second << ")";
    if (t_max < 0) {
      BOOST_LOG_TRIVIAL(debug) << "变迁 " << trans.name 
                                << " 已超过最大等待时间，应该已经发生";
      // 这种情况下应该立即发生
      sched_T.push_back(SchedT{t, {0, 0}});
      continue;
    }
    std::pair<int, int> sched_time;
    // 只检查最小值是否超出区间
    if (t_min > fire_time.second) {
      BOOST_LOG_TRIVIAL(debug) << "跳过变迁 " << trans.name 
                              << ": 最小发生时间超出区间";
      sched_time = fire_time;
    } else if (t_max > fire_time.second) {
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
    

    // 验证时间窗口的有效性
    if (sched_time.first > sched_time.second) {
      BOOST_LOG_TRIVIAL(error) << "Invalid time domain: min(" 
                              << sched_time.first << ") > max(" 
                              << sched_time.second << ")";
      continue;
    }
    
    sched_T.push_back(SchedT{t, sched_time});
  }
  
  return sched_T;
}

std::set<T_wait> StateClassGraph::calculate_new_wait_times(
    const std::vector<std::size_t>& old_enabled_t,
    const std::vector<std::size_t>& new_enabled_t,
    PTPN& local_ptpn) {
  std::set<T_wait> all_t;
  
  // 处理共同使能的变迁
  std::set<std::size_t> common;
  std::set_intersection(old_enabled_t.begin(), old_enabled_t.end(),
                       new_enabled_t.begin(), new_enabled_t.end(),
                       std::inserter(common, common.begin()));
  for (auto t : common) {
    all_t.insert({t, (local_ptpn)[t].pnt.runtime});
  }
  
  // 处理失去使能的变迁
  std::set<std::size_t> disabled;
  std::set_difference(old_enabled_t.begin(), old_enabled_t.end(),
                     new_enabled_t.begin(), new_enabled_t.end(),
                     std::inserter(disabled, disabled.begin()));
  for (auto t : disabled) {
    if ((local_ptpn)[t].pnt.is_handle) {
      all_t.insert({t, (local_ptpn)[t].pnt.runtime});
    } else {
      local_ptpn[t].pnt.runtime = 0;
    }
  }
  
  // 处理新获得使能的变迁
  std::set<std::size_t> newly_enabled;
  std::set_difference(new_enabled_t.begin(), new_enabled_t.end(),
                     old_enabled_t.begin(), old_enabled_t.end(),
                     std::inserter(newly_enabled, newly_enabled.begin()));
  for (auto t : newly_enabled) {
    all_t.insert({t, (local_ptpn)[t].pnt.is_handle ? 
                     (local_ptpn)[t].pnt.runtime : 0});
  }
  
  return all_t;
}

void StateClassGraph::apply_priority_rules(std::vector<SchedT>& sched_T, PTPN& local_ptpn) {
  for (auto it = sched_T.begin(); it != sched_T.end(); ++it) {
    auto next = it + 1;
    while (next != sched_T.end()) {
      const auto& t1 = (local_ptpn)[it->t];
      const auto& t2 = (local_ptpn)[next->t];
      
      if (t1.pnt.priority > t2.pnt.priority && t1.pnt.c == t2.pnt.c) {
        next = sched_T.erase(next);
      } else {
        ++next;
      }
    }
  }
}

  void StateClassGraph::apply_priority_rules(std::vector<std::size_t>& enabled_t, PTPN& local_ptpn) {
  for (auto it = enabled_t.begin(); it != enabled_t.end(); ++it) {
    auto next = it + 1;
    while (next != enabled_t.end()) {
      const auto& t1 = (local_ptpn)[*it];
      const auto& t2 = (local_ptpn)[*next];
      
      if (t1.pnt.priority > t2.pnt.priority && t1.pnt.c == t2.pnt.c) {
        next = enabled_t.erase(next);
      } else {
        ++next;
      }
    }
  }
}

void StateClassGraph::update_transition_times(const std::vector<std::size_t>& enabled_t, 
                           const SchedT& transition, PTPN& local_ptpn) {
  int time_increment = (transition.time.second == transition.time.first) 
                      ? transition.time.first 
                      : (transition.time.second - transition.time.first);
                      
  for (auto t : enabled_t) {
    (local_ptpn)[t].pnt.runtime += time_increment;
  }
  (local_ptpn)[transition.t].pnt.runtime = 0;
}





