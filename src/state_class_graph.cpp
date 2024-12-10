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
  std::pair<int, int> fire_time = {INT_MAX, INT_MAX};
  
  for (auto t : enabled_t_s) {
    const auto& trans = (*init_ptpn)[t];
    int f_min = std::max(0, trans.pnt.const_time.first - trans.pnt.runtime);
    int f_max = trans.pnt.const_time.second - trans.pnt.runtime;
    
    fire_time.first = std::min(fire_time.first, f_min);
    fire_time.second = std::min(fire_time.second, f_max);
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
    
    std::pair<int, int> sched_time;
    if (t_min > fire_time.second) {
      sched_time = fire_time;
    } else if (t_max > fire_time.second && t_min >= fire_time.first) {
      sched_time = {t_min, fire_time.second};
    } else if (t_min >= fire_time.first) {
      sched_time = {t_min, t_max};
    } else {
      BOOST_LOG_TRIVIAL(error) << "Invalid time domain minimum value";
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
  queue<StateClass> state_queue;
  set<StateClass> visited_states;  // 记录已访问的状态
  
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














