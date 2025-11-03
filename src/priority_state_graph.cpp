#include "priority_state_graph.h"
#include <boost/graph/graphviz.hpp>
#include <boost/log/trivial.hpp>
#include <unordered_set>

namespace priority_scg {

PriorityStateClassGraph::PriorityStateClassGraph(
    const PriorityTPNGraph &petri_net)
    : petri_net(petri_net) {
  graph[graph_bundle].name = "优先级时间Petri网状态类图";
}

// 获取初始标记下的状态类
std::shared_ptr<PriorityStateClass>
PriorityStateClassGraph::get_initial_state_class() {
  BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 正在获取初始状态类...";
  Marking initial_marking;

  graph_traits<PriorityTPNGraph>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = vertices(petri_net); vi != vi_end; ++vi) {
    if (const auto &vertex = petri_net[*vi]; vertex.is_place()) {
      if (const auto &[token, capacity] = vertex.as_place(); token > 0) {
        initial_marking[*vi] = token;
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 初始标记: 库所 " << vertex.name << " 的 token 数量为 " << token;
      }
    }
  }

  PriorityStateClass initial_state(initial_marking,
                                   std::map<ptpn_v_desc, TimeInterval>(),
                                   std::map<ptpn_v_desc, TimeInterval>());

  std::map<ptpn_v_desc, TimeInterval> enabled_runtimes;
  std::vector<ptpn_v_desc> enabled_transitions;

  for (boost::tie(vi, vi_end) = vertices(petri_net); vi != vi_end; ++vi) {
    const auto &vertex = petri_net[*vi];

    if (vertex.is_transition()) {
      bool all_places_have_tokens = true;
      graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;
      for (boost::tie(ei, ei_end) = in_edges(*vi, petri_net); ei != ei_end;
           ++ei) {
        const auto &[label, weight] = petri_net[*ei];
        const auto &source = boost::source(*ei, petri_net);

        if (const auto &source_vertex = petri_net[source];
            source_vertex.is_place()) {
          if (const auto &[token, capacity] = source_vertex.as_place();
              token < weight) {
            all_places_have_tokens = false;
            break;
          }
        }
      }
      if (all_places_have_tokens) {
        enabled_transitions.push_back(*vi);

        TimeInterval interval{0, 0};
        enabled_runtimes[*vi] = interval;

        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 初始使能变迁: " << vertex.name << " 时间区间 " << interval.to_string();
      }
    }
  }

  const auto filtered_transitions = filter_by_priority(enabled_transitions);
  std::map<ptpn_v_desc, TimeInterval> filtered_runtimes;
  for (const auto &t : filtered_transitions) {
    filtered_runtimes[t] = enabled_runtimes[t];
    const auto &vertex = petri_net[t];
    BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 优先级过滤后的使能变迁: " << vertex.name;
  }

  return std::make_shared<PriorityStateClass>(
      initial_marking, filtered_runtimes,
      std::map<ptpn_v_desc, TimeInterval>());
}

// 重置 Petri 网到指定的状态类
void PriorityStateClassGraph::reset_petri_net(const PriorityStateClass &state) {
  // 获取标记和运行时间
  const Marking &marking = state.get_marking();
  const auto &enabled_runtimes = state.get_enabled_runtimes();
  const auto &suspended_runtimes = state.get_suspended_runtimes();

  // 首先,将所有库所的token清零,并重置所有变迁的状态
  graph_traits<PriorityTPNGraph>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = vertices(petri_net); vi != vi_end; ++vi) {
    if (auto &vertex = petri_net[*vi]; vertex.is_place()) {
      auto &[token, capacity] = vertex.as_place();
      token = 0;
    } else if (vertex.is_transition()) {
      auto &transition = vertex.as_transition();
      // 注意: runtime 和 handle 是初始化后不应修改的参数
      transition.runtimes = {0, 0}; // 重置运行时间区间
      transition.enable = false;    // 重置使能状态
    }
  }

  // 设置库所的token
  for (const auto &[place, tokens] : marking) {
    if (tokens > 0) {
      if (auto &vertex = petri_net[place]; vertex.is_place()) {
        auto &[token, capacity] = vertex.as_place();
        token = tokens;
      }
    }
  }

  // 不需要计算是否使能,直接重置时间
  for (const auto &[transition, interval] : enabled_runtimes) {
    auto &vertex = petri_net[transition]; 
    vertex.as_transition().runtimes.first = interval.lower;
    vertex.as_transition().runtimes.second = interval.upper;
  }

  for (const auto &[transition, interval] : suspended_runtimes) {
    auto &vertex = petri_net[transition];
    vertex.as_transition().runtimes.first = interval.lower;
    vertex.as_transition().runtimes.second = interval.upper;
  }
}

// 根据优先级过滤使能变迁
std::vector<ptpn_v_desc> PriorityStateClassGraph::filter_by_priority(
    const std::vector<ptpn_v_desc> &enabled_transitions) {
  if (enabled_transitions.empty()) {
    return {};
  }

  std::map<int, std::vector<std::pair<ptpn_v_desc, int>>> transitions_by_core;

  for (const auto &t : enabled_transitions) {
    if (const auto &vertex = petri_net[t]; vertex.is_transition()) {
      const auto &transition = vertex.as_transition();
      transitions_by_core[transition.core].emplace_back(t, transition.priority);
    }
  }

  std::vector<ptpn_v_desc> filtered_transitions;

  for (auto &[core, transitions] : transitions_by_core) {
    std::sort(transitions.begin(), transitions.end(),
              [](const auto &a, const auto &b) {
                return a.second > b.second;
              });

    int highest_priority = transitions.front().second;

    for (const auto &[t, priority] : transitions) {
      if (priority == highest_priority) {
        filtered_transitions.push_back(t);
      } else {
        break;
      }
    }
  }

  return filtered_transitions;
}

// 计算变迁的时间区间
TimeInterval
PriorityStateClassGraph::compute_time_interval(const PriorityStateClass &state,
                                               const ptpn_v_desc transition) {  
  const auto &vertex = petri_net[transition];
  const auto &transition_node = vertex.as_transition();
  
  if (!state.is_transition_enabled(transition)) {
    BOOST_LOG_TRIVIAL(warning) << "[STATE CLASS] 变迁 " << vertex.name << " 不在状态的enabled_runtimes中,返回无效区间 [1, 0]";
    BOOST_LOG_TRIVIAL(warning) << "[STATE CLASS] 当前状态的enabled_runtimes中有 " << state.get_enabled_runtimes().size() << " 个变迁";
    for (const auto &[t, rt] : state.get_enabled_runtimes()) {
      BOOST_LOG_TRIVIAL(warning) << "[STATE CLASS] - 变迁 " << petri_net[t].name << ": " << rt.to_string();
    }
    return TimeIntervalT<int>{1, 0};
  }

  const auto &[fst, snd] = transition_node.const_time;
  const auto &runtime = state.get_enabled_runtime(transition);

  BOOST_LOG_TRIVIAL(info) << "[STATE CLASS] 变迁 " << vertex.name << " 静态时间约束: [" << fst << "," << snd << "], 当前运行时间: " << runtime.to_string();

  int lower = std::max(0, fst - runtime.upper);
  int upper = snd - runtime.lower;
  
  BOOST_LOG_TRIVIAL(info) << "[STATE CLASS] 计算过程: lower = max(0, " << fst << " - " << runtime.upper << ") = " << lower << ", upper = " << snd << " - " << runtime.lower << " = " << upper;

  if (upper < lower) {
    BOOST_LOG_TRIVIAL(warning) << "[STATE CLASS] 变迁 " << vertex.name << " 的可发生时间区间无效: [" << lower << "," << upper << "]";
    return TimeIntervalT<int>{1, 0};
  }

  const TimeInterval interval(lower, upper);
  BOOST_LOG_TRIVIAL(info) << "[STATE CLASS] 变迁 " << vertex.name << " 的可发生时间区间: " << interval.to_string() << " (有效: " << interval.is_valid() << ")";
  return interval;
}

// 添加状态类到图中
SCGVertex PriorityStateClassGraph::add_state(const PriorityStateClass &state) {
  // 直接使用状态对象作为key，避免哈希碰撞问题
  if (const auto it = state_vertex_map.find(state);
      it != state_vertex_map.end()) {
    BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 发现重复状态: " << graph[it->second].id;
    return it->second;
  }

  const SCGVertex new_vertex = boost::add_vertex(graph);

  graph[new_vertex].id = "S" + std::to_string(new_vertex);
  graph[new_vertex].state = std::make_shared<PriorityStateClass>(state);
  graph[new_vertex].label = graph[new_vertex].id + "\n" + state.to_string();

  state_vertex_map[state] = new_vertex;

  BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 添加新状态: " << graph[new_vertex].id << " (哈希: " << state.hash() << ")";

  return new_vertex;
}

SCGEdge PriorityStateClassGraph::add_edge(const SCGVertex from,
                                          const SCGVertex to,
                                          const ptpn_v_desc transition,
                                          const TimeInterval &interval) {
  graph_traits<StateClassGraph>::out_edge_iterator ei, ei_end;
  for (boost::tie(ei, ei_end) = out_edges(from, graph); ei != ei_end; ++ei) {
    if (boost::target(*ei, graph) == to &&
        graph[*ei].transition == transition) {
      graph[*ei].time_interval = graph[*ei].time_interval.intersect(interval);
      return *ei;
    }
  }

  bool success;
  SCGEdge new_edge;
  boost::tie(new_edge, success) = boost::add_edge(from, to, graph);

  if (success) {
    const auto &transition_vertex = petri_net[transition];
    std::string transition_name = transition_vertex.name;

    graph[new_edge].transition = transition;
    graph[new_edge].xlabel = transition_name;
    graph[new_edge].time_interval = interval;

    BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 添加新边: " << graph[from].id
                             << " --[" << transition_name << ", "
                             << interval.to_string() << "]--> "
                             << graph[to].id;
  }

  return new_edge;
}

std::vector<ptpn_v_desc> PriorityStateClassGraph::compute_enabled_transitions(
    const PriorityStateClass &state) {
  reset_petri_net(state);

  std::vector<ptpn_v_desc> enabled_transitions;
  graph_traits<PriorityTPNGraph>::vertex_iterator vi, vi_end;

  for (boost::tie(vi, vi_end) = vertices(petri_net); vi != vi_end; ++vi) {
    if (auto &vertex = petri_net[*vi]; vertex.is_transition()) {
      bool is_enabled = true;
      graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;

      for (boost::tie(ei, ei_end) = in_edges(*vi, petri_net); ei != ei_end;
           ++ei) {
        const auto source = boost::source(*ei, petri_net);
        const auto &[label, weight] = petri_net[*ei];

        if (const auto &source_vertex = petri_net[source];
            source_vertex.is_place()) {
          if (const auto &[token, capacity] = source_vertex.as_place();
              token < weight) {
            is_enabled = false;
            break;
          }
        }
      }

      if (is_enabled) {
        vertex.as_transition().enable = true;
        enabled_transitions.push_back(*vi);
      }
    }
  }

  return enabled_transitions;
}

// 变迁触发后得到的后继状态
PriorityStateClass
PriorityStateClassGraph::fire_transition(const PriorityStateClass &state,
                                         ptpn_v_desc transition,
                                         const TimeInterval &common_interval) {
  reset_petri_net(state);

  Marking new_marking = state.get_marking();

  graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;
  for (boost::tie(ei, ei_end) = in_edges(transition, petri_net); ei != ei_end;
       ++ei) {
    auto source = boost::source(*ei, petri_net);
    const auto &[label, weight] = petri_net[*ei];

    if (auto &source_vertex = petri_net[source]; source_vertex.is_place()) {
      auto &[token, capacity] = source_vertex.as_place();
      token -= weight;
      if (token > 0) {
        new_marking[source] = token;
      } else {
        new_marking.erase(source);
      }
    }
  }

  graph_traits<PriorityTPNGraph>::out_edge_iterator eo, eo_end;
  for (boost::tie(eo, eo_end) = out_edges(transition, petri_net); eo != eo_end;
       ++eo) {
    auto target = boost::target(*eo, petri_net);
    const auto &[label, weight] = petri_net[*eo];

    if (auto &target_vertex = petri_net[target]; target_vertex.is_place()) {
      auto &[token, capacity] = target_vertex.as_place();
      token += weight;
      if (token > capacity) {
        token = capacity;
      }
      new_marking[target] = token;
      BOOST_ASSERT(token <= capacity);
    }
  }

  PriorityStateClass temp_state(new_marking,
                                std::map<ptpn_v_desc, TimeInterval>(),
                                std::map<ptpn_v_desc, TimeInterval>());

  reset_petri_net(temp_state);

  auto enabled_transitions = compute_enabled_transitions(temp_state);
  BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 新状态下有 " << enabled_transitions.size() << " 个使能变迁";
  for (const auto &t : enabled_transitions) {
    BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 新状态下的使能变迁: " << petri_net[t].name << ", 优先级: " << petri_net[t].as_transition().priority << ", cpu: " << petri_net[t].as_transition().core;
                    
  }

  auto filtered_transitions = filter_by_priority(enabled_transitions);
  BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 优先级过滤后有 " << filtered_transitions.size() << " 个使能变迁";
  for (const auto &t : filtered_transitions) {
    BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 优先级过滤后的使能变迁: " << petri_net[t].name << ", 优先级: " << petri_net[t].as_transition().priority << ", cpu: " << petri_net[t].as_transition().core;
  }

  std::map<ptpn_v_desc, TimeInterval> new_enabled_runtimes;
  std::map<ptpn_v_desc, TimeInterval> new_suspended_runtimes;

  for (const auto &t : filtered_transitions) {
    const auto &vertex = petri_net[t];
    const auto &transition_node = vertex.as_transition();

    if (state.is_transition_enabled(t) && t != transition) {
      TimeInterval original_interval = state.get_enabled_runtime(t);

      int new_lower = original_interval.lower + common_interval.lower;
      int new_upper = original_interval.upper + common_interval.upper;

      if (new_lower >= transition_node.const_time.second) {
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << vertex.name << " 已达到最小执行时间,应该被触发";
        continue;
      }

      if (new_upper > transition_node.const_time.second) {
        new_upper = transition_node.const_time.second;
      }

      if (new_upper < new_lower) {
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << vertex.name << " 的时间区间变为无效,跳过";
        continue;
      }

      TimeInterval updated_interval(new_lower, new_upper);
      new_enabled_runtimes[t] = updated_interval;

      BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << vertex.name << " 在经过时间区间 " << common_interval.to_string() << " 后更新为时间区间: " << updated_interval.to_string();

    }
    else if (state.is_transition_suspended(t)) {
      new_enabled_runtimes[t] = state.get_suspended_runtime(t);
      BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 挂起的变迁 " << vertex.name << " 恢复使能,使用原挂起时间区间: " << state.get_suspended_runtime(t).to_string();
    }
    else {
      TimeInterval interval(0, 0);
      new_enabled_runtimes[t] = interval;
      BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 新使能的变迁 " << vertex.name << " 重置运行时间为0";
    }
  }

  for (const auto &[t, _] : state.get_enabled_runtimes()) {
    if (t != transition &&
        std::find(filtered_transitions.begin(), filtered_transitions.end(),
                  t) == filtered_transitions.end()) {
      const auto &vertex = petri_net[t];
      const auto &transition_node = vertex.as_transition();

      if (transition_node.handle && transition_node.runtimes.first != 0 &&
          transition_node.runtimes.second != 0) {
        new_suspended_runtimes[t] = state.get_enabled_runtime(t);
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << vertex.name << " 被挂起,保持原有时间区间: " << state.get_enabled_runtime(t).to_string();                      
      } else {
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << vertex.name << " 不是可挂起变迁,不加入挂起集合";
      }
    }
  }

  for (const auto &[t, interval] : state.get_suspended_runtimes()) {
    if (std::find(filtered_transitions.begin(), filtered_transitions.end(),
                  t) == filtered_transitions.end()) {
      new_suspended_runtimes[t] = interval;
      BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 挂起的变迁 " << petri_net[t].name << " 保持挂起状态,时间区间: " << interval.to_string();
    }
  }

  return {new_marking, new_enabled_runtimes, new_suspended_runtimes};
}

SCGVertex PriorityStateClassGraph::find_state_vertex(
    const PriorityStateClass &state) const {
  // 直接使用状态对象作为key查找，避免哈希碰撞问题
  if (const auto it = state_vertex_map.find(state);
      it != state_vertex_map.end()) {
    return it->second;
  }

  return graph_traits<StateClassGraph>::null_vertex();
}

TimeInterval PriorityStateClassGraph::compute_common_firing_interval(
    const std::map<ptpn_v_desc, TimeInterval> &transition_intervals) {
  if (transition_intervals.empty()) {
    BOOST_LOG_TRIVIAL(warning) << "[STATE CLASS] 没有变迁区间可计算,返回默认公共调度区间 [0, 0]";
    return TimeIntervalT<int>{0, 0};
  }

  BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 计算公共调度区间,共 " << transition_intervals.size() << " 个变迁区间";
  std::vector<std::pair<int, int>> valid_intervals;

  for (const auto &[t, interval] : transition_intervals) {
    BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 的时间区间: " << interval.to_string();
                    
    if (interval.is_valid()) {
      valid_intervals.emplace_back(interval.lower, interval.upper);
    } else {
      BOOST_LOG_TRIVIAL(warning) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 的区间 " << interval.to_string() << " 无效,跳过";

    }
  }

  if (valid_intervals.empty()) {
    BOOST_LOG_TRIVIAL(warning) << "[STATE CLASS] 没有有效的变迁区间,返回默认公共调度区间 [0, 0]";
    return TimeIntervalT<int>{0, 0};
  }

  std::sort(valid_intervals.begin(), valid_intervals.end());

  const int min_lower = valid_intervals[0].first;
  int min_upper = valid_intervals[0].second;

  for (const auto &[lower, upper] : valid_intervals) {
    if (lower > min_upper) {
      break;
    }

    min_upper = std::min(min_upper, upper);
  }

  const TimeInterval common_interval(min_lower, min_upper);
  BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 计算得到的公共调度区间: " << common_interval.to_string();

  return common_interval;
}


void PriorityStateClassGraph::generate_state_class_graph() {
  BOOST_LOG_TRIVIAL(info) << "[STATE CLASS] 开始生成状态类图...";

  graph.clear();
  state_vertex_map.clear();

  auto initial_state = get_initial_state_class();
  if (!initial_state) {
    BOOST_LOG_TRIVIAL(error) << "[STATE CLASS] 无法获取初始状态类";
    return;
  }

  BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 初始状态类: " << initial_state->to_string();

  SCGVertex initial_vertex = add_state(*initial_state);

  std::queue<SCGVertex> vertex_queue;
  vertex_queue.push(initial_vertex);

  int processed_states = 0;
  std::unordered_set<SCGVertex> processed_vertices;

  while (!vertex_queue.empty()) {
    SCGVertex current_vertex = vertex_queue.front();
    vertex_queue.pop();

    if (processed_vertices.find(current_vertex) != processed_vertices.end()) {
      continue;
    }

    processed_vertices.insert(current_vertex);
    const PriorityStateClass &current_state = *graph[current_vertex].state;
    BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 当前状态类: " << current_state.to_string();

    auto enabled_transitions = compute_enabled_transitions(current_state);
    auto filtered_transitions = filter_by_priority(enabled_transitions);

    if (filtered_transitions.empty()) {
      BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 状态 " << graph[current_vertex].id << " 没有使能变迁";
      continue;
    } 

    std::vector<ptpn_v_desc> state_enabled_transitions;
    for (const auto &t : filtered_transitions) {
      if (current_state.is_transition_enabled(t)) {
        state_enabled_transitions.push_back(t);
      } else {
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 在Petri网中使能但不在状态类中,跳过";
      }
    }

    if (state_enabled_transitions.empty()) {
      BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 状态 " << graph[current_vertex].id << " 没有在状态类中使能的变迁";
      continue;
    }

    std::map<ptpn_v_desc, TimeInterval> transition_intervals;
    for (const auto &t : state_enabled_transitions) {
      TimeInterval interval = compute_time_interval(current_state, t);
      BOOST_LOG_TRIVIAL(info) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 计算得到的时间区间: " << interval.to_string() << " (有效: " << interval.is_valid() << ")";
      if (interval.is_valid()) {
        transition_intervals[t] = interval;
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 的时间区间: " << interval.to_string();
                       
      } else {
        BOOST_LOG_TRIVIAL(warning) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 的时间区间是:" << interval.to_string() << ",无效,跳过";
                 
      }
    }

    bool has_ready_transition = false;
    for (const auto &[t, runtime] : current_state.get_enabled_runtimes()) {
      const auto &vertex = petri_net[t];
      const auto &transition_node = vertex.as_transition();
      if (runtime.lower >= transition_node.const_time.first) {
        has_ready_transition = true;
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << vertex.name << " 已达到触发条件,运行时间: " << runtime.to_string() << ", 约束: [" << transition_node.const_time.first << "," << transition_node.const_time.second << "]";
        break;
      }
    }

    TimeInterval common_interval =
        compute_common_firing_interval(transition_intervals);
    BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 公共调度区间: " << common_interval.to_string();

    std::vector<ptpn_v_desc> schedulable_transitions;
    for (const auto &[t, interval] : transition_intervals) {
      if (interval.lower <= common_interval.upper) {
        schedulable_transitions.push_back(t);
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 可调度变迁: " << petri_net[t].name;
      }
    }

    for (const auto &t : schedulable_transitions) {
      TimeInterval interval = transition_intervals[t];
      TimeInterval firing_interval = interval.intersect(common_interval);
      BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 的实际发生区间: " << firing_interval.to_string();

      PriorityStateClass successor_state =
          fire_transition(current_state, t, common_interval);

      if (!successor_state.is_valid()) {
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 触发后的状态无效,跳过";
        continue;
      }

      SCGVertex successor_vertex = add_state(successor_state);

      BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 后继状态类: " << successor_state.to_string();
      add_edge(current_vertex, successor_vertex, t, firing_interval);

      if (processed_vertices.find(successor_vertex) ==
          processed_vertices.end()) {
        vertex_queue.push(successor_vertex);
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 将后继状态 S" << successor_vertex << " 加入队列";
      } else {
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 后继状态 S" << successor_vertex << " 已处理,不再加入队列";
                 
      }
    }
  }

  BOOST_LOG_TRIVIAL(info) << "[STATE CLASS] 状态类图生成完成,共 " << num_vertices(graph) << " 个状态," << num_edges(graph) << " 条边";
                    
}

// 生成状态类图(带状态数限制)
void PriorityStateClassGraph::generate_state_class_graph_with_limit(
    size_t max_states) {
  BOOST_LOG_TRIVIAL(info) << "[STATE CLASS] 开始生成状态类图(最大状态数限制: " << max_states << "...)";

  graph.clear();
  state_vertex_map.clear();

  auto initial_state = get_initial_state_class();
  if (!initial_state) {
    BOOST_LOG_TRIVIAL(error) << "[STATE CLASS] 无法获取初始状态类";
    return;
  }

  BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 初始状态类: " << initial_state->to_string();

  SCGVertex initial_vertex = add_state(*initial_state);

  std::queue<SCGVertex> vertex_queue;
  vertex_queue.push(initial_vertex);

  int processed_states = 0;
  std::unordered_set<SCGVertex> processed_vertices;

  while (!vertex_queue.empty() && processed_states < max_states) {
    SCGVertex current_vertex = vertex_queue.front();
    vertex_queue.pop();

    if (processed_vertices.find(current_vertex) != processed_vertices.end()) {
      continue;
    }

    processed_vertices.insert(current_vertex);
    const PriorityStateClass &current_state = *graph[current_vertex].state;
    BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 当前状态类: " << current_state.to_string();
    processed_states++;
    if (processed_states % 50 == 0) {
      BOOST_LOG_TRIVIAL(info) << "[STATE CLASS] 已处理 " << processed_states << " 个状态,当前队列大小: " << vertex_queue.size();
                    
    }

    if (processed_states >= max_states) {
      BOOST_LOG_TRIVIAL(warning) << "[STATE CLASS] 已达到最大状态数限制 " << max_states << " 停止生成";
      break;
    }

    auto enabled_transitions = compute_enabled_transitions(current_state);
    auto filtered_transitions = filter_by_priority(enabled_transitions);

    if (filtered_transitions.empty()) {
      BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 状态 " << graph[current_vertex].id << " 没有使能变迁";
      continue;
    }

    std::vector<ptpn_v_desc> state_enabled_transitions;
    for (const auto &t : filtered_transitions) {
      if (current_state.is_transition_enabled(t)) {
        state_enabled_transitions.push_back(t);
      } else {
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 在Petri网中使能但不在状态类中,跳过";
      }
    }

    if (state_enabled_transitions.empty()) {
      BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 状态 " << graph[current_vertex].id << " 没有在状态类中使能的变迁";
      continue;
    }

    std::map<ptpn_v_desc, TimeInterval> transition_intervals;
    for (const auto &t : state_enabled_transitions) {
      TimeInterval interval = compute_time_interval(current_state, t);
      BOOST_LOG_TRIVIAL(info) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 计算得到的时间区间: " << interval.to_string() << " (有效: " << interval.is_valid() << ")";
      if (interval.is_valid()) {
        transition_intervals[t] = interval;
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 的时间区间: " << interval.to_string();
                       
      } else {
        BOOST_LOG_TRIVIAL(warning) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 的时间区间是:" << interval.to_string() << ",无效,跳过";
                          
      }
    }

    TimeInterval common_interval =
        compute_common_firing_interval(transition_intervals);
    BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 公共调度区间: " << common_interval.to_string();

    std::vector<ptpn_v_desc> schedulable_transitions;
    for (const auto &[t, interval] : transition_intervals) {
      if (interval.lower <= common_interval.upper) {
        schedulable_transitions.push_back(t);
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 可调度变迁: " << petri_net[t].name;
      }
    }

    for (const auto &t : schedulable_transitions) {
      if (processed_states >= max_states) {
        BOOST_LOG_TRIVIAL(warning) << "[STATE CLASS] 已达到最大状态数限制 " << max_states << " 停止生成";
        break;
      }

      TimeInterval interval = transition_intervals[t];

      TimeInterval firing_interval = interval.intersect(common_interval);
      BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 的实际发生区间: " << firing_interval.to_string();
                     

      PriorityStateClass successor_state =
          fire_transition(current_state, t, common_interval);

      if (!successor_state.is_valid()) {
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 变迁 " << petri_net[t].name << " 触发后的状态无效,跳过";
        continue;
      }

      SCGVertex successor_vertex = add_state(successor_state);

      BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 后继状态类: " << successor_state.to_string();      
      add_edge(current_vertex, successor_vertex, t, firing_interval);

      if (processed_vertices.find(successor_vertex) ==
          processed_vertices.end()) {
        vertex_queue.push(successor_vertex);
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 将后继状态 S" << successor_vertex << " 加入队列";
      } else {
        BOOST_LOG_TRIVIAL(debug) << "[STATE CLASS] 后继状态 S" << successor_vertex << " 已处理,不再加入队列";
                         
      }
    }
  }

  if (processed_states >= max_states) {
    BOOST_LOG_TRIVIAL(warning) << "[STATE CLASS] 状态类图生成因达到最大状态数限制 " << max_states << " 而停止";
              
  }

  BOOST_LOG_TRIVIAL(info) << "[STATE CLASS] 状态类图生成完成,共 " << num_vertices(graph) << " 个状态," << num_edges(graph) << " 条边";
               
}

std::size_t PriorityStateClassGraph::get_vertex_count() const {
  return num_vertices(graph);
}

std::size_t PriorityStateClassGraph::get_edge_count() const {
  return num_edges(graph);
}

bool PriorityStateClassGraph::save_to_dot(const std::string &filename) const {
  try {
    std::ofstream dot_file(filename);
    if (!dot_file) {
      BOOST_LOG_TRIVIAL(error) << "[STATE CLASS] 无法打开文件: " << filename;
      return false;
    }

    dot_file << "digraph StateClassGraph {\n";
    dot_file << "  rankdir=LR;\n";
    dot_file << "  fontname=\"SimSun\";\n";
    dot_file << "  node [fontname=\"SimSun\", shape=box];\n";
    dot_file << "  edge [fontname=\"SimSun\"];\n\n";

    graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
    for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi) {
      const SCGVertex &v = *vi;
      const PriorityStateClass &state = *graph[v].state;

      std::string node_label = graph[v].id + "\\n";

      node_label += "markings: {";
      bool first = true;
      for (const auto &[place, tokens] : state.get_marking()) {
        if (tokens <= 0)
          continue;
        if (!first)
          node_label += ", ";
        std::string place_name = "P" + std::to_string(place);
        if (place < num_vertices(petri_net)) {
          const auto &vertex = petri_net[place];
          if (vertex.is_place()) {
            place_name = vertex.name;
          }
        }
        node_label += place_name + "->" + std::to_string(tokens);
        first = false;
      }
      node_label += "}\\n";

      const auto &enabled_runtimes = state.get_enabled_runtimes();
      if (!enabled_runtimes.empty()) {
        node_label += "使能变迁: ";
        first = true;
        for (const auto &[trans, interval] : enabled_runtimes) {
          if (!first)
            node_label += ", ";
          std::string trans_name = "T" + std::to_string(trans);
          if (trans < num_vertices(petri_net)) {
            const auto &vertex = petri_net[trans];
            if (vertex.is_transition()) {
              trans_name = vertex.name;
            }
          }
          node_label += trans_name + "[" + interval.to_string() + "]";
          first = false;
        }
        node_label += "\\n";
      }

      const auto &suspended_runtimes = state.get_suspended_runtimes();
      if (!suspended_runtimes.empty()) {
        node_label += "挂起变迁: ";
        first = true;
        for (const auto &[trans, interval] : suspended_runtimes) {
          if (!first)
            node_label += ", ";
          std::string trans_name = "T" + std::to_string(trans);
          if (trans < num_vertices(petri_net)) {
            const auto &vertex = petri_net[trans];
            if (vertex.is_transition()) {
              trans_name = vertex.name;
            }
          }
          node_label += trans_name + "[" + interval.to_string() + "]";
          first = false;
        }
        node_label += "\\n";
      }

      dot_file << "  " << graph[v].id << " [label=\"" << node_label << "\"];\n";
    }

    graph_traits<StateClassGraph>::edge_iterator ei, ei_end;
    for (boost::tie(ei, ei_end) = edges(graph); ei != ei_end; ++ei) {
      const SCGEdge &e = *ei;
      SCGVertex source = boost::source(*ei, graph);
      SCGVertex target = boost::target(*ei, graph);

      std::string edge_label =
          graph[e].xlabel + "\\n" + graph[e].time_interval.to_string();

      dot_file << "  " << graph[source].id << " -> " << graph[target].id
               << " [label=\"" << edge_label << "\"];\n";
    }

    dot_file << "}\n";
    dot_file.close();
    BOOST_LOG_TRIVIAL(info) << "[STATE CLASS] 状态类图已保存到: " << filename;
    return true;
  } catch (const std::exception &e) {
    BOOST_LOG_TRIVIAL(error) << "[STATE CLASS] 保存DOT文件时发生错误: " << e.what();
    return false;
  }
}

void PriorityStateClassGraph::print_graph_info() const {
  BOOST_LOG_TRIVIAL(info) << "[STATE CLASS] 状态类图信息:";
  BOOST_LOG_TRIVIAL(info) << "[STATE CLASS]  节点数量: " << get_vertex_count();
  BOOST_LOG_TRIVIAL(info) << "[STATE CLASS]  边数量: " << get_edge_count();

  int terminal_states = 0;
  graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi) {
    if (out_degree(*vi, graph) == 0) {
      terminal_states++;
    }
  }

  BOOST_LOG_TRIVIAL(info) << "[STATE CLASS]  终止状态数量: " << terminal_states;

  const bool has_deadlock_state = has_deadlock();
  BOOST_LOG_TRIVIAL(info) << "[STATE CLASS]  是否存在死锁状态: " << has_deadlock_state;
              

  int max_depth = get_max_depth();
  BOOST_LOG_TRIVIAL(info) << "[STATE CLASS]  可达性树最大深度: " << max_depth;
}

bool PriorityStateClassGraph::save_to_json(const std::string &filename) const {
  try {
    std::ofstream json_file(filename);
    if (!json_file) {
      BOOST_LOG_TRIVIAL(error) << "[STATE CLASS] 无法打开文件: " << filename;
      return false;
    }

    json_file << "{\n";
    json_file << "  \"graph\": {\n";
    json_file << "    \"name\": \"StateClassGraph\",\n";
    json_file << "    \"nodes\": [\n";

    graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
    bool first_node = true;
    for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi) {
      const SCGVertex &v = *vi;
      const PriorityStateClass &state = *graph[v].state;

      if (!first_node)
        json_file << ",\n";
      first_node = false;

      json_file << "      {\n";
      json_file << R"(        "id": ")" << graph[v].id << "\",\n";

      json_file << "        \"markings\": {\n";
      bool first_marking = true;
      for (const auto &[place, tokens] : state.get_marking()) {
        if (tokens <= 0)
          continue;
        if (!first_marking)
          json_file << ",\n";
        first_marking = false;

        std::string place_name = "P" + std::to_string(place);
        if (place < num_vertices(petri_net)) {
          const auto &vertex = petri_net[place];
          if (vertex.is_place()) {
            place_name = vertex.name;
          }
        }
        json_file << "          \"" << place_name << "\": " << tokens;
      }
      json_file << "\n        },\n";

      const auto &enabled_runtimes = state.get_enabled_runtimes();
      json_file << "        \"enabled_transitions\": {\n";
      bool first_enabled = true;
      for (const auto &[trans, interval] : enabled_runtimes) {
        if (!first_enabled)
          json_file << ",\n";
        first_enabled = false;

        std::string trans_name = "T" + std::to_string(trans);
        if (trans < num_vertices(petri_net)) {
          const auto &vertex = petri_net[trans];
          if (vertex.is_transition()) {
            trans_name = vertex.name;
          }
        }
        json_file << "          \"" << trans_name << "\": \""
                  << interval.to_string() << "\"";
      }
      json_file << "\n        },\n";

      const auto &suspended_runtimes = state.get_suspended_runtimes();
      json_file << "        \"suspended_transitions\": {\n";
      bool first_suspended = true;
      for (const auto &[trans, interval] : suspended_runtimes) {
        if (!first_suspended)
          json_file << ",\n";
        first_suspended = false;

        std::string trans_name = "T" + std::to_string(trans);
        if (trans < num_vertices(petri_net)) {
          const auto &vertex = petri_net[trans];
          if (vertex.is_transition()) {
            trans_name = vertex.name;
          }
        }
        json_file << "          \"" << trans_name << "\": \""
                  << interval.to_string() << "\"";
      }
      json_file << "\n        }\n";
      json_file << "      }";
    }

    json_file << "\n    ],\n";
    json_file << "    \"edges\": [\n";

    graph_traits<StateClassGraph>::edge_iterator ei, ei_end;
    bool first_edge = true;
    for (boost::tie(ei, ei_end) = edges(graph); ei != ei_end; ++ei) {
      const SCGEdge &e = *ei;
      SCGVertex source = boost::source(*ei, graph);
      SCGVertex target = boost::target(*ei, graph);

      if (!first_edge)
        json_file << ",\n";
      first_edge = false;

      json_file << "      {\n";
      json_file << R"(        "source": ")" << graph[source].id << "\",\n";
      json_file << R"(        "target": ")" << graph[target].id << "\",\n";
      json_file << R"(        "transition": ")" << graph[e].xlabel << "\",\n";
      json_file << R"(        "time_interval": ")"
                << graph[e].time_interval.to_string() << "\"\n";
      json_file << "      }";
    }

    json_file << "\n    ]\n";
    json_file << "  }\n";
    json_file << "}\n";

    json_file.close();
    BOOST_LOG_TRIVIAL(info) << "[STATE CLASS] 状态类图JSON已保存到: " << filename;
    return true;
  } catch (const std::exception &e) {
    BOOST_LOG_TRIVIAL(error) << "[STATE CLASS] 保存JSON文件时发生错误: " << e.what();
    return false;
  }
}

bool PriorityStateClassGraph::has_deadlock() const {
  graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi) {
    if (out_degree(*vi, graph) == 0) {
      const PriorityStateClass &state = *graph[*vi].state;
      if (!state.get_enabled_runtimes().empty()) {
          BOOST_LOG_TRIVIAL(warning) << "[STATE CLASS] 发现死锁状态: " << graph[*vi].id;
        return true;
      }
    }
  }

  return false;
}

int PriorityStateClassGraph::get_max_depth() const {
  constexpr SCGVertex initial_vertex = 0;

  std::unordered_map<SCGVertex, int> depths;
  depths[initial_vertex] = 0;

  std::queue<SCGVertex> vertex_queue;
  vertex_queue.push(initial_vertex);

  int max_depth = 0;

  while (!vertex_queue.empty()) {
    SCGVertex current = vertex_queue.front();
    vertex_queue.pop();

    int current_depth = depths[current];
    max_depth = std::max(max_depth, current_depth);

    graph_traits<StateClassGraph>::out_edge_iterator ei, ei_end;
    for (boost::tie(ei, ei_end) = out_edges(current, graph); ei != ei_end;
         ++ei) {
      SCGVertex target = boost::target(*ei, graph);

      if (depths.find(target) == depths.end() ||
          depths[target] < current_depth + 1) {
        depths[target] = current_depth + 1;
        vertex_queue.push(target);
      }
    }
  }

  return max_depth;
}

}