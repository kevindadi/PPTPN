#include "state_class_graph.h"
#include "priority_time_petri_net.h"
#include <unordered_map>

namespace scg {
void StateClassGraph::generate_state_class_graph() {
  // 计算初始状态
  auto initial_state = compute_initial_state();
  auto initial_vertex = add_state(initial_state);

  // 使用工作列表算法计算状态类图
  std::vector<Vertex> work_list{initial_vertex};
  std::unordered_map<State, Vertex> state_to_vertex;
  state_to_vertex[*initial_state] = initial_vertex;

  while (!work_list.empty()) {
    auto current_vertex = work_list.back();
    work_list.pop_back();

    auto current_state = get_vertex_state(current_vertex);
    auto enabled_trans = get_enabled_transitions(*current_state);
    auto fireable_trans =
        get_fireable_transitions(*current_state, enabled_trans);
    auto filtered_trans = filter_by_priority(fireable_trans);

    for (const auto &[t, firing_interval] : filtered_trans) {
      auto next_state =
          compute_successor_state(*current_state, t, firing_interval);

      // 检查是否是新状态
      auto it = state_to_vertex.find(*next_state);
      if (it == state_to_vertex.end()) {
        // 新状态
        auto next_vertex = add_state(next_state);
        add_edge(current_vertex, next_vertex, t);
        work_list.push_back(next_vertex);
        state_to_vertex[*next_state] = next_vertex;
      } else {
        // 已存在的状态
        add_edge(current_vertex, it->second, t);
      }
    }
  }
}

// 计算初始状态
std::shared_ptr<State> StateClassGraph::compute_initial_state() {
  std::unordered_map<int, int> initial_marking;
  std::vector<int> enabled_transitions;

  // 遍历 Petri 网的所有顶点
  boost::graph_traits<ptpn::PriorityTPNGraph>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = boost::vertices(pn_graph); vi != vi_end; ++vi) {
    const auto &vertex = pn_graph[*vi];
    if (vertex.is_place()) {
      const auto &place = vertex.as_place();
      if (place.token >= 1) {
        initial_marking[*vi] = place.token;
      }
    } else if (vertex.is_transition()) {
      if (is_transition_enabled(*vi, initial_marking)) {
        enabled_transitions.push_back(*vi);
      }
    }
  }

  std::vector<TimeConstraint> initial_constraints;
  for (int t : enabled_transitions) {
    const auto &transition = pn_graph[t].as_transition();
    TimeInterval runtimes(0, 0);
    initial_constraints.push_back(
        TimeConstraint(t, runtimes.min, runtimes.max));
  }

  return std::make_shared<State>(initial_marking, initial_constraints,
                                 enabled_transitions);
}

std::vector<TimeConstraint> StateClassGraph::update_time_constraints(
    const State &current_state, int fired_transition,
    const TimeInterval &firing_interval,
    const std::vector<int> &new_enabled_transitions) {
  std::vector<TimeConstraint> new_constraints;

  // 更新现有的时间约束
  for (const auto &tc : current_state.time_constraints) {
    if (tc.transition_id != fired_transition) {
      TimeInterval old_interval(tc.min_time, tc.max_time);
      TimeInterval new_interval = old_interval.intersect(
          TimeInterval(old_interval.min - firing_interval.max,
                       old_interval.max - firing_interval.min));

      if (new_interval.is_valid()) {
        new_constraints.emplace_back(tc.transition_id, new_interval.min,
                                     new_interval.max);
      }
    }
  }

  // 为新使能的变迁添加时间约束
  for (int t : new_enabled_transitions) {
    const auto &transition = pn_graph[t].as_transition();
    if (transition.const_time.first > 0 ||
        transition.const_time.second <
            std::numeric_limits<double>::infinity()) {
      new_constraints.emplace_back(t, transition.const_time.first,
                                   transition.const_time.second);
    }
  }

  return new_constraints;
}

bool StateClassGraph::is_transition_enabled(
    int transition_id, const std::unordered_map<int, int> &marking) {
  boost::graph_traits<ptpn::PriorityTPNGraph>::in_edge_iterator ei, ei_end;
  for (boost::tie(ei, ei_end) = boost::in_edges(transition_id, pn_graph);
       ei != ei_end; ++ei) {
    auto source = boost::source(*ei, pn_graph);
    if (pn_graph[source].is_place()) {
      int weight = pn_graph[*ei].weight;
      auto it = marking.find(source);
      if (it == marking.end() || it->second < weight) {
        return false;
      }
    }
  }
  return true;
}

// 获取使能变迁

std::vector<int> StateClassGraph::get_enabled_transitions(State &state) {
  std::vector<int> enabled;
  boost::graph_traits<ptpn::PriorityTPNGraph>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = boost::vertices(pn_graph); vi != vi_end; ++vi) {
    if (pn_graph[*vi].is_transition()) {
      bool is_enabled = true;
      // 检查前置库所是否有足够的 token
      boost::graph_traits<ptpn::PriorityTPNGraph>::in_edge_iterator ei, ei_end;
      for (boost::tie(ei, ei_end) = boost::in_edges(*vi, pn_graph);
           ei != ei_end; ++ei) {
        auto source = boost::source(*ei, pn_graph);
        if (pn_graph[source].is_place()) {
          int place_index = (int)(source);
          if (state.marking[place_index] < 1) {
            is_enabled = false;
            break;
          }
        }
      }
      if (is_enabled) {
        enabled.push_back(static_cast<int>(*vi));
      }
    }
  }
  return enabled;
}

std::vector<std::pair<int, TimeInterval>>
StateClassGraph::get_fireable_transitions(
    const State &state, const std::vector<int> &enabled_trans) {
  std::vector<std::pair<int, TimeInterval>> fireable_trans;

  for (int t : enabled_trans) {
    const auto &transition = pn_graph[t].as_transition();
    TimeInterval const_time(transition.const_time.first,
                            transition.const_time.second);
    TimeInterval runtime(transition.runtimes.first, transition.runtimes.second);

    // 计算可发生的时间区间
    TimeInterval fireable_interval(std::max(0.0, const_time.min - runtime.max),
                                   const_time.max - runtime.min);

    if (fireable_interval.is_valid()) {
      fireable_trans.emplace_back(t, fireable_interval);
      BOOST_LOG_TRIVIAL(error)
          << "无效Transition " << pn_graph[t].name << " is fireable in ["
          << fireable_interval.min << ", " << fireable_interval.max << "]";
    }
  }

  return fireable_trans;
}

// 根据优先级过滤变迁
std::vector<std::pair<int, TimeInterval>> StateClassGraph::filter_by_priority(
    const std::vector<std::pair<int, TimeInterval>> &fireable_trans) {
  if (fireable_trans.empty()) {
    return {};
  }

  std::vector<std::pair<int, TimeInterval>> filtered;
  std::unordered_map<int, int>
      core_highest_priority; // core_id -> highest priority

  // 第一次遍历:找到每个 core 上的最高优先级
  for (auto t : fireable_trans) {
    const auto &transition = pn_graph[t.first].as_transition();

    int core_id = transition.core;
    int priority = transition.priority;

    auto it = core_highest_priority.find(core_id);
    if (it == core_highest_priority.end()) {
      core_highest_priority[core_id] = priority;
    } else {
      core_highest_priority[core_id] = std::max(it->second, priority);
    }
  }

  // 第二次遍历:保留在任一 core 上具有最高优先级的变迁
  for (auto t : fireable_trans) {
    const auto &transition = pn_graph[t.first].as_transition();
    bool has_highest = false;

    // 检查该变迁是否在任一 core 上具有最高优先级
    int core_id = transition.core;
    int priority = transition.priority;

    if (priority == core_highest_priority[core_id]) {
      has_highest = true;
      break;
    }

    if (has_highest) {
      filtered.push_back(t);
    }
  }

  return filtered;
}

// 计算后继状态
std::shared_ptr<State>
StateClassGraph::compute_successor_state(const State &current_state,
                                         int transition,
                                         const TimeInterval &firing_interval) {
  std::unordered_map<int, int> new_marking = current_state.marking;
  std::vector<int> new_enabled_transitions;

  // 更新标识
  update_marking(new_marking, transition);

  // 更新使能变迁
  new_enabled_transitions = update_enabled_transitions(new_marking);

  // 更新时间约束
  std::vector<TimeConstraint> new_constraints = update_time_constraints(
      current_state, transition, firing_interval, new_enabled_transitions);

  return std::make_shared<State>(new_marking, new_constraints,
                                 new_enabled_transitions);
}

void StateClassGraph::update_marking(std::unordered_map<int, int> &marking,
                                     int transition) {
  // 减少输入库所的 token
  boost::graph_traits<ptpn::PriorityTPNGraph>::in_edge_iterator ei, ei_end;
  for (boost::tie(ei, ei_end) = boost::in_edges(transition, pn_graph);
       ei != ei_end; ++ei) {
    auto source = boost::source(*ei, pn_graph);
    if (pn_graph[source].is_place()) {
      int weight = pn_graph[*ei].weight;
      marking[source] -= weight;
      if (marking[source] == 0) {
        marking.erase(source);
      }
    }
  }

  // 增加输出库所的 token
  boost::graph_traits<ptpn::PriorityTPNGraph>::out_edge_iterator eo, eo_end;
  for (boost::tie(eo, eo_end) = boost::out_edges(transition, pn_graph);
       eo != eo_end; ++eo) {
    auto target = boost::target(*eo, pn_graph);
    if (pn_graph[target].is_place()) {
      int weight = pn_graph[*eo].weight;
      marking[target] += weight;
    }
  }
}

std::vector<int> StateClassGraph::update_enabled_transitions(
    const std::unordered_map<int, int> &marking) {
  std::vector<int> enabled;
  boost::graph_traits<ptpn::PriorityTPNGraph>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = boost::vertices(pn_graph); vi != vi_end; ++vi) {
    if (pn_graph[*vi].is_transition() && is_transition_enabled(*vi, marking)) {
      enabled.push_back(*vi);
    }
  }
  return enabled;
}

// 获取顶点对应的状态
std::shared_ptr<State> StateClassGraph::get_vertex_state(Vertex v) {
  return (*graph)[v].state;
}
} // namespace scg

bool StateClassGraph::is_transition_enabled(const ptpn::PriorityTPNGraph &ptpn,
                                            ptpn::ptpn_v_desc v) {
  // 检查是否是变迁
  if (ptpn[v].shape != "box") {
    return false;
  }

  bool has_non_place = false;
  bool all_places_enabled = true;

  // 遍历所有入边
  for (auto [ei, ei_end] = boost::in_edges(v, ptpn); ei != ei_end; ++ei) {
    ptpn::ptpn_v_desc source = boost::source(*ei, ptpn);

    // 检查前置节点是否为库所
    if (ptpn[source].shape != "circle") {
      has_non_place = true;
      BOOST_LOG_TRIVIAL(error)
          << "Transition " << ptpn[v].name
          << " has non-place predecessor: " << ptpn[source].name;
      continue;
    }

    // 检查库所token数量
    if (ptpn[source].as_place().token < 1) {
      all_places_enabled = false;
      break;
    }
  }

  if (has_non_place) {
    return false;
  }

  return all_places_enabled;
}

StateClass StateClassGraph::get_initial_state_class(
    const ptpn::PriorityTPNGraph &source_ptpn) {
  Marking mark;
  std::set<T_wait> all_t;
  for (auto [vi, vi_end] = vertices(source_ptpn); vi != vi_end; ++vi) {
    if (source_ptpn[*vi].shape == "circle" &&
        source_ptpn[*vi].as_place().token > 0) {
      mark.indexes.insert(*vi);
      mark.labels.insert(source_ptpn[*vi].name);
    }
    // 如果是变迁且使能
    if (source_ptpn[*vi].shape == "box" &&
        is_transition_enabled(source_ptpn, *vi)) {
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
    svd = add_vertex(SCGVertex{sc.to_scg_vertex(), sc.to_scg_vertex()}, scg);
    scg_vertex_map.insert(std::make_pair(sc, svd));
  }
  return svd;
}

void StateClassGraph::set_state_class(const StateClass &state_class) {
  // 首先重置原有状态
  boost::graph_traits<ptpn::PriorityTPNGraph>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = vertices(init_ptpn); vi != vi_end; ++vi) {
    if (init_ptpn[*vi].is_place()) {
      init_ptpn[*vi].as_place().token = 0;
    }
    if (init_ptpn[*vi].is_transition()) {
      init_ptpn[*vi].as_transition().enable = false;
      init_ptpn[*vi].as_transition().runtime = 0;
    }
  }
  // 2. 设置标识
  for (auto m : state_class.mark.indexes) {
    (init_ptpn)[m].as_place().token = 1;
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
    init_ptpn[t.t].as_transition().runtime = t.time;
  }
}

std::vector<SchedT>
StateClassGraph::get_sched_transitions(const StateClass &state_class) {
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
  auto sched_transitions =
      get_time_satisfied_transitions(enabled_transitions, fire_time);

  // 应用优先级规则
  apply_priority_rules(sched_transitions);

  // 日志输出
  for (const auto &sched : sched_transitions) {
    BOOST_LOG_TRIVIAL(debug) << init_ptpn[sched.t].label;
  }

  return sched_transitions;
}

std::vector<ptpn::ptpn_v_desc> StateClassGraph::get_enabled_transitions() {
  std::vector<ptpn::ptpn_v_desc> enabled_t_s;

  for (auto [vi, vi_end] = boost::vertices(init_ptpn); vi != vi_end; ++vi) {
    if (init_ptpn[*vi].shape != "box")
      continue;

    if (is_transition_enabled(init_ptpn, *vi)) {
      enabled_t_s.push_back(*vi);
    }
  }

  return enabled_t_s;
}

std::pair<int, int> StateClassGraph::calculate_fire_time_domain(
    const std::vector<ptpn::ptpn_v_desc> &enabled_t_s) {
  std::pair<int, int> fire_time = {0, INT_MAX};

  for (auto t : enabled_t_s) {
    const auto &trans = init_ptpn[t].as_transition();
    int f_min = std::max(0, trans.const_time.first - trans.runtime);
    int f_max = trans.const_time.second - trans.runtime;

    if (f_max < 0) {
      BOOST_LOG_TRIVIAL(debug)
          << "变迁 " << init_ptpn[t].name << " 已超时，设置为立即发生";
      return {0, 0};
    }

    fire_time.first = std::min(fire_time.first, f_min);
    if (f_max < fire_time.second) {
      fire_time.second = f_max;
    }

    if (f_min > f_max) {
      BOOST_LOG_TRIVIAL(error) << "calculate_fire_time_domain error: min("
                               << f_min << ") > max(" << f_max << ")";
    }
  }
  if (fire_time.first > fire_time.second) {
    BOOST_LOG_TRIVIAL(debug) << "调整无效的时间区间为立即发生";
    return {0, 0};
  }
  return fire_time;
}

std::vector<SchedT> StateClassGraph::get_time_satisfied_transitions(
    const std::vector<ptpn::ptpn_v_desc> &enabled_t_s,
    const std::pair<int, int> &fire_time) {
  std::vector<SchedT> sched_T;

  for (auto t : enabled_t_s) {
    const auto &trans = init_ptpn[t];
    int t_min = std::max(0, trans.as_transition().const_time.first -
                                trans.as_transition().runtime);
    int t_max =
        trans.as_transition().const_time.second - trans.as_transition().runtime;

    // 添加日志输出以便调试
    BOOST_LOG_TRIVIAL(debug)
        << "Transition " << trans.name << ": t_min=" << t_min
        << ", t_max=" << t_max << ", fire_time=(" << fire_time.first << ","
        << fire_time.second << ")";
    if (t_max < 0) {
      BOOST_LOG_TRIVIAL(debug)
          << "变迁 " << trans.name << " 已超过最大等待时间，应该已经发生";
      // 这种情况下应该立即发生
      sched_T.push_back(SchedT{t, {0, 0}});
      continue;
    }
    std::pair<int, int> sched_time;
    // 只检查最小值是否超出区间
    if (t_min > fire_time.second) {
      BOOST_LOG_TRIVIAL(debug)
          << "跳过变迁 " << trans.name << ": 最小发生时间超出区间";
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
      BOOST_LOG_TRIVIAL(error)
          << "Invalid time domain: min(" << sched_time.first << ") > max("
          << sched_time.second << ")";
      continue;
    }

    sched_T.push_back(SchedT{t, sched_time});
  }

  return sched_T;
}

void StateClassGraph::apply_priority_rules(std::vector<SchedT> &sched_T) {
  for (auto it = sched_T.begin(); it != sched_T.end(); ++it) {
    auto next = it + 1;
    while (next != sched_T.end()) {
      const auto &t1 = init_ptpn[it->t].as_transition();
      const auto &t2 = init_ptpn[next->t].as_transition();

      if (t1.priority > t2.priority && t1.core == t2.core) {
        next = sched_T.erase(next);
      } else {
        ++next;
      }
    }
  }
}

void StateClassGraph::apply_priority_rules(
    std::vector<std::size_t> &enabled_t) {
  for (auto it = enabled_t.begin(); it != enabled_t.end(); ++it) {
    auto next = it + 1;
    while (next != enabled_t.end()) {
      const auto &t1 = init_ptpn[*it].as_transition();
      const auto &t2 = init_ptpn[*next].as_transition();

      if (t1.priority > t2.priority && t1.core == t2.core) {
        next = enabled_t.erase(next);
      } else {
        ++next;
      }
    }
  }
}

StateClass StateClassGraph::fire_transition(const StateClass &sc,
                                            SchedT transition) {
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

  for (auto [vi, vi_end] = boost::vertices(init_ptpn); vi != vi_end; ++vi) {
    if (init_ptpn[*vi].shape == "circle")
      continue;

    bool enable_t = is_transition_enabled(init_ptpn, *vi);
    if (enable_t)
      enabled_t.push_back(*vi);
    old_enabled_t.push_back(*vi);
  }

  return {enabled_t, old_enabled_t};
}

void StateClassGraph::update_transition_times(
    const std::vector<std::size_t> &enabled_t, const SchedT &transition) {
  int time_increment = (transition.time.second == transition.time.first)
                           ? transition.time.first
                           : (transition.time.second - transition.time.first);

  for (auto t : enabled_t) {
    init_ptpn[t].as_transition().runtime += time_increment;
  }
  init_ptpn[transition.t].as_transition().runtime = 0;
}

void StateClassGraph::execute_transition(const SchedT &transition) {
  // 清空前置库所
  for (auto [in_i, in_end] = boost::in_edges(transition.t, init_ptpn);
       in_i != in_end; ++in_i) {
    ptpn::ptpn_v_desc place = boost::source(*in_i, init_ptpn);
    if (init_ptpn[place].as_place().token < 0) {
      BOOST_LOG_TRIVIAL(error) << "Place token <= 0, transition not enabled";
    }
    init_ptpn[place].as_place().token = 0;
  }

  // 设置后继库所
  for (auto [out_i, out_end] = boost::out_edges(transition.t, init_ptpn);
       out_i != out_end; ++out_i) {
    ptpn::ptpn_v_desc place = boost::target(*out_i, init_ptpn);
    if (init_ptpn[place].as_place().token > 1) {
      BOOST_LOG_TRIVIAL(error) << "Unsafe Petri net, place token >= 1";
    }
    init_ptpn[place].as_place().token = 1;
  }
}

std::pair<Marking, std::vector<std::size_t>>
StateClassGraph::get_new_marking_and_enabled() {
  Marking new_mark;
  std::vector<std::size_t> new_enabled_t;

  for (auto [vi, vi_end] = boost::vertices(init_ptpn); vi != vi_end; ++vi) {
    if (init_ptpn[*vi].shape == "circle") {
      if (init_ptpn[*vi].as_place().token == 1) {
        new_mark.indexes.insert(*vi);
        new_mark.labels.insert(init_ptpn[*vi].label);
      }
    } else if (is_transition_enabled(init_ptpn, *vi)) {
      new_enabled_t.push_back(*vi);
    }
  }

  return {new_mark, new_enabled_t};
}

std::set<T_wait> StateClassGraph::calculate_new_wait_times(
    const std::vector<std::size_t> &old_enabled_t,
    const std::vector<std::size_t> &new_enabled_t) {
  std::set<T_wait> all_t;

  // 处理共同使能的变迁
  std::set<std::size_t> common;
  std::set_intersection(old_enabled_t.begin(), old_enabled_t.end(),
                        new_enabled_t.begin(), new_enabled_t.end(),
                        std::inserter(common, common.begin()));
  for (auto t : common) {
    all_t.insert({t, init_ptpn[t].as_transition().runtime});
  }

  // 处理失去使能的变迁
  std::set<std::size_t> disabled;
  std::set_difference(old_enabled_t.begin(), old_enabled_t.end(),
                      new_enabled_t.begin(), new_enabled_t.end(),
                      std::inserter(disabled, disabled.begin()));
  for (auto t : disabled) {
    if (init_ptpn[t].as_transition().handle) {
      all_t.insert({t, init_ptpn[t].as_transition().runtime});
    } else {
      init_ptpn[t].as_transition().runtime = 0;
    }
  }

  // 处理新获得使能的变迁
  std::set<std::size_t> newly_enabled;
  std::set_difference(new_enabled_t.begin(), new_enabled_t.end(),
                      old_enabled_t.begin(), old_enabled_t.end(),
                      std::inserter(newly_enabled, newly_enabled.begin()));
  for (auto t : newly_enabled) {
    all_t.insert({t, init_ptpn[t].as_transition().handle
                         ? init_ptpn[t].as_transition().runtime
                         : 0});
  }

  return all_t;
}

void StateClassGraph::generate_state_class() {
  BOOST_LOG_TRIVIAL(info) << "Generating state class";
  auto start_time = chrono::steady_clock::now();

  // 初始化队列和状态集
  std::queue<StateClass> state_queue;
  std::set<StateClass> visited_states; // 记录已访问的状态

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
    std::vector<SchedT> sched_transitions =
        get_sched_transitions(current_state);

    // 处理每个可调度变迁
    for (const auto &transition : sched_transitions) {
      // 计算新状态
      StateClass new_state = fire_transition(current_state, transition);

      // 检查是否是新状态
      if (visited_states.find(new_state) == visited_states.end()) {
        state_queue.push(new_state);
        visited_states.insert(new_state);
        state_count++;

        // 添加新顶点和边
        auto new_vertex = add_scg_vertex(new_state);
        SCGEdge edge = {std::to_string(transition.time.first) + ":" +
                        std::to_string(transition.time.second)};
        add_edge(current_vertex, new_vertex, edge, scg);

        BOOST_LOG_TRIVIAL(debug) << "Added new state: " << state_count;
      } else {
        // 状态已存在，只添加边
        auto existing_vertex = scg_vertex_map.find(new_state)->second;
        SCGEdge edge = {std::to_string(transition.time.first) + ":" +
                        std::to_string(transition.time.second)};
        add_edge(current_vertex, existing_vertex, edge, scg);
      }
    }
  }

  // 输出统计信息
  auto duration =
      chrono::duration<double>(chrono::steady_clock::now() - start_time);
  BOOST_LOG_TRIVIAL(info) << "State class generation completed:";
  BOOST_LOG_TRIVIAL(info) << "- Time taken: " << duration.count() << "s";
  BOOST_LOG_TRIVIAL(info) << "- Total states: " << num_vertices(scg);
  BOOST_LOG_TRIVIAL(info) << "- Unique states: " << visited_states.size();
}
