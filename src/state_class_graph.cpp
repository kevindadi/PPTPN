#include "state_class_graph.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <spdlog/spdlog.h>

namespace state_class {

static void debug(const std::string& msg) {
  spdlog::debug("[STATE_CLASS] {}", msg);
}

static void info(const std::string& msg) {
  spdlog::info("[STATE_CLASS] {}", msg);
}

std::string StateClassReachabilityGraph::format_marking(
    const std::vector<int>& marking) {
  std::string result = "[";
  for (size_t i = 0; i < marking.size(); ++i) {
    result += std::to_string(marking[i]);
    if (i < marking.size() - 1) {
      result += ", ";
    }
  }
  result += "]";
  return result;
}

std::string StateClassReachabilityGraph::format_transitions(
    const std::set<size_t>& trans_indices, bool detailed) const {
  if (trans_indices.empty()) {
    return "(无)";
  }

  std::string result;
  bool first = true;
  for (size_t t : trans_indices) {
    if (!first) {
      result += ", ";
    }
    first = false;

    if (detailed && t < ptpn_.num_transitions()) {
      const auto& trans = ptpn_.get_transition(t);
      result += "T" + std::to_string(t) + "(" + trans.name;
      result += ", 优先级=" + std::to_string(trans.priority);
      result += ", 核心=" + std::to_string(trans.core);
      result += trans.suspendable ? ", 可挂起" : "";
      result += ")";
    } else {
      result += "T" + std::to_string(t);
    }
  }
  return result;
}

std::string StateClassReachabilityGraph::format_places(
    const std::vector<int>& marking) const {
  std::string result = "[";
  bool first = true;
  for (size_t i = 0; i < marking.size(); ++i) {
    if (!first) {
      result += ", ";
    }
    first = false;

    if (i < ptpn_.num_places()) {
      const auto& place = ptpn_.get_place(i);
      result += "P" + std::to_string(i) + "(" + place.name +
                ")=" + std::to_string(marking[i]);
    } else {
      result += "P" + std::to_string(i) + "=" + std::to_string(marking[i]);
    }
  }
  result += "]";
  return result;
}

void StateClassReachabilityGraph::log_state_class_details(
    const StateClass& state, const std::string& prefix) const {
  spdlog::debug("{}========== 状态类详情: ID={} ==========", prefix, state.state_id);
  spdlog::debug("{}库所信息: {}", prefix, format_places(state.marking));
  spdlog::debug("{}使能变迁: {}", prefix, format_transitions(state.enabled));
  if (!state.suspended.empty()) {
    spdlog::debug("{}挂起变迁: {}", prefix, format_transitions(state.suspended));
  }
  spdlog::debug("{}累计时间: {}", prefix, state.cumulative_time);

  spdlog::debug("{}Z1 (不可挂起变迁):", prefix);
  std::string z1_str = state.Z1.to_string();
  std::istringstream z1_stream(z1_str);
  std::string z1_line;
  while (std::getline(z1_stream, z1_line)) {
    spdlog::debug("{}  {}", prefix, z1_line);
  }

  spdlog::debug("{}Z2 (可挂起变迁):", prefix);
  std::string z2_str = state.Z2.to_string();
  std::istringstream z2_stream(z2_str);
  std::string z2_line;
  while (std::getline(z2_stream, z2_line)) {
    spdlog::debug("{}  {}", prefix, z2_line);
  }

  spdlog::debug("{}==========================================", prefix);
}

StateClassReachabilityGraph::StateClassReachabilityGraph(
    const matrix_ptpn::MatrixPTPN& ptpn)
    : ptpn_(ptpn), next_state_id_(0), pruning_enabled_(false) {}

size_t StateClassReachabilityGraph::build(size_t max_states) {
  stats_ = Statistics();
  state_to_vertex_.clear();

  StateClass s0 = create_initial_state_class();
  StateClass canonical_s0 = canonicalize(s0);

  StateClassVertex s0_vertex = find_or_add_vertex(s0);
  initial_vertex_ = s0_vertex;
  stats_.total_states++;

  std::map<std::tuple<std::vector<int>, DBM, DBM>, StateClassVertex> uniq;
  uniq[{s0.marking, s0.Z1, s0.Z2}] = s0_vertex;

  std::queue<StateClass> Q;
  Q.push(canonical_s0);

  size_t iteration = 0;
  while (!Q.empty() && stats_.total_states < max_states) {
    iteration++;
    StateClass cur = Q.front();
    Q.pop();

    StateClassVertex u = find_or_add_vertex(cur);

    log_state_class_details(cur,
                            "[状态 " + std::to_string(cur.state_id) + "] ");

    std::vector<size_t> chosen = select_per_core(cur.enabled);
    stats_.enabled_transitions_count += chosen.size();

    StateClass scheduled = cur.copy();
    apply_preemption(chosen, scheduled);

    double dt = 0;
    if (maximal_time_elapse(scheduled, dt)) {
      spdlog::debug("  执行最大化时间推进: dt = {}", dt);
    }

    if (pruning_enabled_ && scheduled.Z1.is_empty()) {
      debug("  [剪枝] Z1为空,跳过此状态");
      stats_.pruned_states_count++;
      continue;
    } else if (!pruning_enabled_ && scheduled.Z1.is_empty()) {
      debug("  [警告] Z1为空,但剪枝已禁用,继续处理");
    }

    size_t fired_count = 0;
    for (size_t t : chosen) {
      auto [ok, nxt, tau] = fire_with_dbm(t, scheduled);

      if (!ok) {
        if (pruning_enabled_) {
          spdlog::debug("  {}: 触发失败", format_transitions({t}, false));
          stats_.pruned_states_count++;
          continue;
        } else {
          spdlog::debug("  {}: 触发失败 [警告] 剪枝已禁用,继续处理此变迁", format_transitions({t}, false));
          continue;
        }
      }

      spdlog::debug("  {} -> 后继状态: ID={}", format_transitions({t}, false), nxt.state_id);

      auto key = std::make_tuple(nxt.marking, nxt.Z1, nxt.Z2);
      StateClassVertex v;

      if (uniq.find(key) != uniq.end()) {
        v = uniq[key];
        debug("  [已存在] 使用已有状态");
      } else {
        StateClass canonical_nxt = canonicalize(nxt);
        v = find_or_add_vertex(nxt);
        Q.push(canonical_nxt);
        uniq[key] = v;
        stats_.total_states++;
        debug("  [新状态] 添加到图和队列");
        log_state_class_details(
            nxt, "[新状态 " + std::to_string(nxt.state_id) + "] ");
      }

      TransitionEdge edge(static_cast<int>(t), tau);
      boost::add_edge(u, v, edge, graph_);
      stats_.total_transitions++;
      fired_count++;
    }

    spdlog::info("[STATE_CLASS] 状态 {}: 选择了 {} 个候选变迁, 成功触发 {} 个, 队列大小: {}, 总状态数: {}",
                 cur.state_id, chosen.size(), fired_count, Q.size(), stats_.total_states);
  }

  spdlog::info("[STATE_CLASS] 构建完成: 总迭代次数={}, 总状态数={}", iteration, stats_.total_states);

  return stats_.total_states;
}

StateClass StateClassReachabilityGraph::create_initial_state_class() {
  StateClass initial;
  initial.marking = ptpn_.get_marking();
  initial.state_id = next_state_id_++;
  initial.cumulative_time = 0.0;

  size_t num_transitions = ptpn_.num_transitions();

  initial.Z1.resize(num_transitions + 1);
  initial.Z2.resize(num_transitions + 1);

  compute_enabled_and_clocks(initial);

  log_state_class_details(initial, "[初始状态] ");

  debug("[STATE_CLASS] 创建初始状态:");
  spdlog::debug("  标识: {}", format_marking(initial.marking));
  spdlog::debug("  使能变迁: {}", format_transitions(initial.enabled));
  if (!initial.suspended.empty()) {
    spdlog::debug("  挂起变迁: {}", format_transitions(initial.suspended));
  }

  return initial;
}

void StateClassReachabilityGraph::explore_successors(
    const StateClass& current_state, std::set<StateClass>& visited) {}

bool StateClassReachabilityGraph::is_transition_enabled(
    const StateClass& state, size_t trans_idx) const {
  return matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, trans_idx);
}

std::pair<int, int> StateClassReachabilityGraph::get_transition_time_bounds(
    const StateClass& state, size_t trans_idx) const {
  const auto& transition = ptpn_.get_transition(trans_idx);
  int earliest = transition.time_interval.earliest;
  int latest = transition.time_interval.latest;

  return {earliest, latest};
}

std::pair<DBM, DBM> StateClassReachabilityGraph::time_advance(
    const StateClass& state) const {
  DBM z1_up = state.Z1;
  DBM z2_up = state.Z2;

  DBM invariants = get_invariants_for(state.marking);

  size_t num_clocks = z1_up.size();
  size_t num_transitions = ptpn_.num_transitions();

  size_t relaxed_count = 0;

  for (size_t i = 1; i < num_clocks && i <= num_transitions; ++i) {
    size_t trans_idx = i - 1;

    bool is_enabled = false;
    if (trans_idx < num_transitions) {
      is_enabled =
          matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, trans_idx);
    }

    if (!is_enabled) {
      continue;
    }

    if (z1_up.is_frozen(i)) {
      spdlog::debug("    时钟{}({}): 被冻结,不参与时间推进", i, format_transitions({trans_idx}, false));
      continue;
    }

    const auto& transition = ptpn_.get_transition(trans_idx);
    bool is_exact_time =
        (transition.time_interval.earliest == transition.time_interval.latest &&
         transition.time_interval.latest != matrix_ptpn::INF);

    if (is_exact_time && !transition.suspendable) {
      spdlog::debug("    时钟{}({}): 精确时间约束,不放宽上界", i, format_transitions({trans_idx}, false));
      continue;
    }

    int current_upper = z1_up.get_constraint(i, 0);
    if (current_upper != INF_TIME) {
      z1_up.set_constraint(i, 0, INF_TIME);
      relaxed_count++;
      spdlog::debug("    时钟{}({}): 放宽上界 {} -> INF", i, format_transitions({trans_idx}, false), current_upper);
    }
  }

  spdlog::debug("  时间推进: 放宽了 {} 个时钟的上界", relaxed_count);

  if (invariants.size() > 0 && z1_up.size() == invariants.size()) {
    z1_up = z1_up.intersection(invariants);
  }

  z1_up.minimize();
  z2_up.minimize();

  return {z1_up, z2_up};
}

bool StateClassReachabilityGraph::is_suspended(
    size_t trans_idx, const std::vector<size_t>& enabled) const {
  const auto& transition = ptpn_.get_transition(trans_idx);

  if (!transition.suspendable) {
    return false;
  }

  int trans_core = transition.core;
  int trans_priority = transition.priority;

  for (size_t other_t : enabled) {
    if (other_t == trans_idx) continue;

    const auto& other_trans = ptpn_.get_transition(other_t);
    if (other_trans.core == trans_core && !other_trans.suspendable &&
        other_trans.priority > trans_priority) {
      return true;
    }
  }

  return false;
}

bool StateClassReachabilityGraph::check_dbm_time_intersection(
    const DBM& z1, size_t trans_idx) const {
  const auto& transition = ptpn_.get_transition(trans_idx);

  size_t clock_idx = trans_idx + 1;

  if (clock_idx >= z1.size()) {
    return false;
  }

  int alpha = transition.time_interval.earliest;
  int beta = transition.time_interval.latest == matrix_ptpn::INF
                 ? INF_TIME
                 : transition.time_interval.latest;

  DBM restricted = restrict_for_firing(z1, trans_idx);
  return !restricted.is_empty();
}

DBM StateClassReachabilityGraph::restrict_for_firing(const DBM& z,
                                                     size_t trans_idx) const {
  const auto& transition = ptpn_.get_transition(trans_idx);
  int alpha = transition.time_interval.earliest;
  int beta = transition.time_interval.latest == matrix_ptpn::INF
                 ? INF_TIME
                 : transition.time_interval.latest;

  return z.restrict_for_firing(trans_idx, alpha, beta);
}

double StateClassReachabilityGraph::compute_firing_time(
    const DBM& z1_up, size_t trans_idx) const {
  const auto& transition = ptpn_.get_transition(trans_idx);

  size_t clock_idx = trans_idx + 1;
  if (clock_idx >= z1_up.size()) {
    return static_cast<double>(transition.time_interval.earliest);
  }

  int alpha = transition.time_interval.earliest;
  int beta = transition.time_interval.latest == matrix_ptpn::INF
                 ? INF_TIME
                 : transition.time_interval.latest;

  int dbm_lower =
      -z1_up.get_constraint(0, clock_idx);

  int firing_time_int = std::max(alpha, dbm_lower);

  if (beta != INF_TIME && firing_time_int > beta) {
    firing_time_int = beta;
  }

  return static_cast<double>(firing_time_int);
}

StateClass StateClassReachabilityGraph::canonicalize(
    const StateClass& state) const {
  StateClass canonical = state;

  canonical.Z1.minimize();
  canonical.Z2.minimize();

  return canonical;
}

void StateClassReachabilityGraph::recompute_suspension(
    StateClass& state) const {
  std::set<size_t> enabled;
  for (size_t t = 0; t < ptpn_.num_transitions(); ++t) {
    if (matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, t)) {
      enabled.insert(t);
    }
  }

  state.enabled = enabled;

  std::set<size_t> suspended;
  std::vector<size_t> enabled_vec(enabled.begin(), enabled.end());

  for (size_t t : enabled) {
    if (is_suspended(t, enabled_vec)) {
      suspended.insert(t);

      if (state.suspended.find(t) == state.suspended.end()) {
        spdlog::debug("    {}: 变为挂起状态,冻结时钟", format_transitions({t}, false));
        size_t clock_idx = t + 1;
        state.Z1.copy_clock_constraints(clock_idx, state.Z2);
        state.Z1.freeze_clock(clock_idx);
        state.Z2.freeze_clock(clock_idx);
      }
    } else {
      if (state.suspended.find(t) != state.suspended.end()) {
        spdlog::debug("    {}: 变为非挂起状态,解冻时钟", format_transitions({t}, false));
        size_t clock_idx = t + 1;
        state.Z2.copy_clock_constraints(clock_idx, state.Z1);
        state.Z1.unfreeze_clock(clock_idx);
        state.Z2.unfreeze_clock(clock_idx);
      }
    }
  }

  state.suspended = suspended;

  const_cast<StateClassReachabilityGraph*>(this)->update_dbm_constraints(state);
}

DBM StateClassReachabilityGraph::get_invariants_for(
    const std::vector<int>& marking) const {
  size_t num_transitions = ptpn_.num_transitions();
  DBM invariants(num_transitions + 1);

  return invariants;
}

StateClass StateClassReachabilityGraph::fire_transition(const StateClass& state,
                                                        size_t trans_idx,
                                                        double firing_time) {
  spdlog::debug("    触发变迁 {} @时间 {}", format_transitions({trans_idx}, false), firing_time);

  StateClass new_state = state;

  new_state.marking =
      matrix_ptpn::MatrixPTPN::fire(state.marking, ptpn_, trans_idx);

  spdlog::debug("      标识变化: {} -> {}", format_marking(state.marking), format_marking(new_state.marking));

  new_state.cumulative_time = state.cumulative_time + firing_time;

  const auto& transition = ptpn_.get_transition(trans_idx);
  size_t clock_idx = trans_idx + 1;

  if (transition.suspendable) {
    spdlog::debug("      重置Z2中的时钟 {}", clock_idx);
    new_state.Z2.reset_clock(clock_idx);
  } else {
    spdlog::debug("      重置Z1中的时钟 {}", clock_idx);
    new_state.Z1.reset_clock(clock_idx);
  }

  update_dbm_constraints(new_state);

  return new_state;
}

void StateClassReachabilityGraph::update_dbm_constraints(StateClass& state) {
  size_t num_transitions = ptpn_.num_transitions();

  if (state.Z1.size() < num_transitions + 1) {
    state.Z1.resize(num_transitions + 1);
  }
  if (state.Z2.size() < num_transitions + 1) {
    state.Z2.resize(num_transitions + 1);
  }

  std::vector<size_t> enabled;
  for (size_t t = 0; t < ptpn_.num_transitions(); ++t) {
    if (matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, t)) {
      enabled.push_back(t);
    }
  }

  size_t cleared_count = 0;
  size_t initialized_count = 0;

  for (size_t t = 0; t < num_transitions; ++t) {
    bool is_enabled = false;
    for (size_t e : enabled) {
      if (e == t) {
        is_enabled = true;
        break;
      }
    }

    if (!is_enabled) {
      size_t clock_idx = t + 1;

      if (clock_idx < state.Z1.size()) {
        state.Z1.reset_clock(clock_idx);
        cleared_count++;
      }
      if (clock_idx < state.Z2.size()) {
        state.Z2.reset_clock(clock_idx);
      }

      state.Z1.unfreeze_clock(clock_idx);
      state.Z2.unfreeze_clock(clock_idx);
    }
  }

  if (cleared_count > 0) {
    spdlog::debug("    清除了 {} 个不再使能变迁的约束", cleared_count);
  }

  for (size_t trans_idx : enabled) {
    const auto& transition = ptpn_.get_transition(trans_idx);
    size_t clock_idx = trans_idx + 1;

    bool clock_just_reset = false;
    bool clock_exists = false;

    if (transition.suspendable) {
      if (clock_idx < state.Z2.size()) {
        clock_exists = true;
        int upper = state.Z2.get_constraint(clock_idx, 0);
        int lower = -state.Z2.get_constraint(0, clock_idx);
        if (upper == INF_TIME && lower == 0) {
          clock_just_reset = true;
        }
      }
    } else {
      if (clock_idx < state.Z1.size()) {
        clock_exists = true;
        int upper = state.Z1.get_constraint(clock_idx, 0);
        int lower = -state.Z1.get_constraint(0, clock_idx);
        if (upper == INF_TIME && lower == 0) {
          clock_just_reset = true;
        }
      }
    }

    if (!clock_exists || clock_just_reset) {
      initialized_count++;
      std::string latest_str = transition.time_interval.latest == matrix_ptpn::INF
          ? "∞"
          : std::to_string(transition.time_interval.latest);
      spdlog::debug("    初始化{}的时钟约束: [{}, {}]",
                    format_transitions({trans_idx}, false),
                    transition.time_interval.earliest,
                    latest_str);

      if (transition.suspendable) {
        if (transition.time_interval.earliest > 0) {
          state.Z2.set_constraint(0, clock_idx,
                                  -transition.time_interval.earliest);
        } else {
          state.Z2.set_constraint(0, clock_idx, 0);
        }
        if (transition.time_interval.latest != matrix_ptpn::INF) {
          state.Z2.set_constraint(clock_idx, 0,
                                  transition.time_interval.latest);
        } else {
          state.Z2.set_constraint(clock_idx, 0, INF_TIME);
        }
      } else {
        if (transition.time_interval.earliest > 0) {
          state.Z1.set_constraint(0, clock_idx,
                                  -transition.time_interval.earliest);
        } else {
          state.Z1.set_constraint(0, clock_idx, 0);
        }
        if (transition.time_interval.latest != matrix_ptpn::INF) {
          state.Z1.set_constraint(clock_idx, 0,
                                  transition.time_interval.latest);
        } else {
          state.Z1.set_constraint(clock_idx, 0, INF_TIME);
        }
      }
    }
  }

  if (initialized_count > 0) {
    spdlog::debug("    初始化了 {} 个新使能变迁的时钟约束", initialized_count);
  }

  state.Z1.minimize();
  state.Z2.minimize();
}

bool StateClassReachabilityGraph::should_prune(
    const StateClass& state, const std::set<StateClass>& visited) const {
  if (state.Z1.is_empty() || state.Z2.is_empty()) {
    spdlog::info("    状态 {} 被剪枝,因为 Z1 或 Z2 为空", state.state_id);
    return true;
  }

  return visited.find(state) != visited.end();
}

StateClassVertex StateClassReachabilityGraph::find_or_add_vertex(
    const StateClass& state) {
  auto it = state_to_vertex_.find(state);
  if (it != state_to_vertex_.end()) {
    return it->second;
  }

  StateClass new_state = state;
  new_state.state_id = next_state_id_++;
  StateClassVertex v = boost::add_vertex(new_state, graph_);
  state_to_vertex_[new_state] = v;
  return v;
}

bool StateClassReachabilityGraph::save_to_dot(
    const std::string& file_path) const {
  try {
    std::ofstream out(file_path);
    if (!out.is_open()) {
      return false;
    }

    out << "digraph StateClassGraph {\n";
    out << "  rankdir=LR;\n";
    out << "  node [shape=box];\n\n";

    typedef boost::graph_traits<StateClassGraph>::vertex_iterator
        StateClassVertexIterator;
    StateClassVertexIterator vi, vi_end;
    for (std::tie(vi, vi_end) = boost::vertices(graph_); vi != vi_end; ++vi) {
      const StateClass& state = boost::get(boost::vertex_name, graph_, *vi);
      out << "  s" << state.state_id << " [label=\"";
      out << "State " << state.state_id << "\\n";
      out << "M: [";
      for (size_t i = 0; i < state.marking.size(); ++i) {
        if (i > 0) out << ", ";
        out << state.marking[i];
      }
      out << "]\\n";
      out << "Time: " << state.cumulative_time;
      out << "\"];\n";
    }

    out << "\n";

    typedef boost::graph_traits<StateClassGraph>::edge_iterator
        StateClassEdgeIterator;
    StateClassEdgeIterator ei, ei_end;
    for (std::tie(ei, ei_end) = boost::edges(graph_); ei != ei_end; ++ei) {
      StateClassVertex src = boost::source(*ei, graph_);
      StateClassVertex tgt = boost::target(*ei, graph_);
      const TransitionEdge& edge = boost::get(boost::edge_name, graph_, *ei);
      const StateClass& src_state = boost::get(boost::vertex_name, graph_, src);
      const StateClass& tgt_state = boost::get(boost::vertex_name, graph_, tgt);

      out << "  s" << src_state.state_id << " -> s" << tgt_state.state_id;
      out << " [label=\"T" << edge.transition_id << "\\n@" << edge.firing_time
          << "\"];\n";
    }

    out << "}\n";
    out.close();

    return true;
  } catch (...) {
    return false;
  }
}

bool StateClassReachabilityGraph::save_to_json(
    const std::string& file_path) const {
  try {
    std::ofstream out(file_path);
    if (!out.is_open()) {
      return false;
    }

    out << "{\n";
    out << "  \"states\": [\n";

    typedef boost::graph_traits<StateClassGraph>::vertex_iterator
        StateClassVertexIterator;
    StateClassVertexIterator vi, vi_end;
    bool first_state = true;
    for (std::tie(vi, vi_end) = boost::vertices(graph_); vi != vi_end; ++vi) {
      const StateClass& state = boost::get(boost::vertex_name, graph_, *vi);
      if (!first_state) out << ",\n";
      first_state = false;

      out << "    {\n";
      out << "      \"id\": " << state.state_id << ",\n";
      out << "      \"marking\": [";
      for (size_t i = 0; i < state.marking.size(); ++i) {
        if (i > 0) out << ", ";
        out << state.marking[i];
      }
      out << "],\n";
      out << "      \"cumulative_time\": " << std::fixed << std::setprecision(2)
          << state.cumulative_time << "\n";
      out << "    }";
    }

    out << "\n  ],\n";
    out << "  \"transitions\": [\n";

    typedef boost::graph_traits<StateClassGraph>::edge_iterator
        StateClassEdgeIterator;
    StateClassEdgeIterator ei, ei_end;
    bool first_trans = true;
    for (std::tie(ei, ei_end) = boost::edges(graph_); ei != ei_end; ++ei) {
      StateClassVertex src = boost::source(*ei, graph_);
      StateClassVertex tgt = boost::target(*ei, graph_);
      const TransitionEdge& edge = boost::get(boost::edge_name, graph_, *ei);
      const StateClass& src_state = boost::get(boost::vertex_name, graph_, src);
      const StateClass& tgt_state = boost::get(boost::vertex_name, graph_, tgt);

      if (!first_trans) out << ",\n";
      first_trans = false;

      out << "    {\n";
      out << "      \"source\": " << src_state.state_id << ",\n";
      out << "      \"target\": " << tgt_state.state_id << ",\n";
      out << "      \"transition_id\": " << edge.transition_id << ",\n";
      out << "      \"firing_time\": " << std::fixed << std::setprecision(2)
          << edge.firing_time << "\n";
      out << "    }";
    }

    out << "\n  ],\n";
    out << "  \"statistics\": {\n";
    out << "    \"total_states\": " << stats_.total_states << ",\n";
    out << "    \"total_transitions\": " << stats_.total_transitions << ",\n";
    out << "    \"enabled_transitions_count\": "
        << stats_.enabled_transitions_count << ",\n";
    out << "    \"pruned_states_count\": " << stats_.pruned_states_count
        << "\n";
    out << "  }\n";
    out << "}\n";

    out.close();
    return true;
  } catch (...) {
    return false;
  }
}

std::vector<size_t> StateClassReachabilityGraph::select_per_core(
    const std::set<size_t>& enabled) const {
  std::map<int, size_t> best_per_core;

  for (size_t t : enabled) {
    const auto& transition = ptpn_.get_transition(t);
    int core = transition.core;
    int priority = transition.priority;

    if (best_per_core.find(core) == best_per_core.end() ||
        priority > ptpn_.get_transition(best_per_core[core]).priority) {
      best_per_core[core] = t;
    }
  }

  std::vector<size_t> chosen;
  for (const auto& [core, trans_idx] : best_per_core) {
    chosen.push_back(trans_idx);
  }

  spdlog::debug("  每核心最高优先级候选({}): {}",
                chosen.size(),
                format_transitions(std::set<size_t>(chosen.begin(), chosen.end()), false));

  return chosen;
}

void StateClassReachabilityGraph::apply_preemption(
    const std::vector<size_t>& chosen, StateClass& state) const {
  state.suspended.clear();

  size_t num_transitions = ptpn_.num_transitions();
  for (size_t u = 0; u < num_transitions; ++u) {
    const auto& transition_u = ptpn_.get_transition(u);

    if (!transition_u.suspendable) {
      continue;
    }

    for (size_t t : chosen) {
      const auto& transition_t = ptpn_.get_transition(t);

      if (transition_t.core == transition_u.core &&
          transition_t.priority > transition_u.priority) {
        state.suspended.insert(u);

        size_t clock_idx = u + 1;
        if (clock_idx < state.Z1.size() && clock_idx < state.Z2.size()) {
          state.Z1.copy_clock_constraints(clock_idx, state.Z2);
          state.Z1.freeze_clock(clock_idx);
          state.Z2.freeze_clock(clock_idx);
        }

        spdlog::debug("    {}: 被{}抢占,冻结时钟",
                      format_transitions({u}, false),
                      format_transitions({t}, false));
        break;
      }
    }
  }
}

bool StateClassReachabilityGraph::maximal_time_elapse(StateClass& state,
                                                      double& dt) const {
  int ub_star = INF_TIME;

  size_t num_clocks = state.Z1.size();
  size_t num_transitions = ptpn_.num_transitions();

  for (size_t i = 1; i < num_clocks && i <= num_transitions; ++i) {
    if (state.Z1.is_frozen(i)) {
      continue;
    }

    int ub = state.Z1.get_constraint(i, 0);
    if (ub != INF_TIME && ub < ub_star) {
      ub_star = ub;
    }
  }

  num_clocks = state.Z2.size();
  for (size_t i = 1; i < num_clocks && i <= num_transitions; ++i) {
    if (state.Z2.is_frozen(i)) {
      continue;
    }

    int ub = state.Z2.get_constraint(i, 0);
    if (ub != INF_TIME && ub < ub_star) {
      ub_star = ub;
    }
  }

  if (ub_star == INF_TIME || ub_star <= 0) {
    dt = 0;
    return false;
  }

  state.Z1.elapse_time(ub_star);
  state.Z2.elapse_time(ub_star);
  state.cumulative_time += ub_star;
  dt = ub_star;

  spdlog::debug("  最大化时间推进: dt = {}", dt);

  return true;
}

std::tuple<bool, StateClass, double> StateClassReachabilityGraph::fire_with_dbm(
    size_t trans_idx, const StateClass& from_state) {
  StateClass to = from_state.copy();
  to.state_id = next_state_id_++;

  const auto& transition = ptpn_.get_transition(trans_idx);
  int alpha = transition.time_interval.earliest;
  int beta = transition.time_interval.latest == matrix_ptpn::INF
                 ? INF_TIME
                 : transition.time_interval.latest;

  const DBM& targetZ = transition.suspendable ? to.Z2 : to.Z1;

  DBM zcheck = restrict_for_firing(targetZ, trans_idx);

  if (pruning_enabled_) {
    if (zcheck.is_empty() || !zcheck.is_consistent()) {
      spdlog::debug("    {}: 触发窗口检查失败", format_transitions({trans_idx}, false));
      return {false, StateClass(), 0.0};
    }
  } else {
    if (zcheck.is_empty() || !zcheck.is_consistent()) {
      spdlog::debug("    {}: 触发窗口检查失败,但剪枝已禁用,继续处理", format_transitions({trans_idx}, false));
    }
  }

  int delta = 0;
  if (alpha == 0 && beta == 0) {
    delta = 0;
  } else if (alpha == beta) {
    delta = alpha;
  } else {
    delta = alpha;
  }

  if (delta > 0) {
    to.Z1.elapse_time(delta);
    to.Z2.elapse_time(delta);
    to.cumulative_time += delta;
  }

  double fire_time = to.cumulative_time;

  to.marking = matrix_ptpn::MatrixPTPN::fire(to.marking, ptpn_, trans_idx);

  if (to.marking.empty()) {
    spdlog::debug("    {}: 触发后标识为空", format_transitions({trans_idx}, false));
    return {false, StateClass(), 0.0};
  }

  size_t clock_idx = trans_idx + 1;
  if (transition.suspendable) {
    if (clock_idx < to.Z2.size()) {
      to.Z2.reset_clock(clock_idx);
    }
    to.suspended.erase(trans_idx);
  } else {
    if (clock_idx < to.Z1.size()) {
      to.Z1.reset_clock(clock_idx);
    }
  }

  compute_enabled_and_clocks(to);

  spdlog::debug("    {}: 成功触发, fire_time = {}", format_transitions({trans_idx}, false), fire_time);

  return {true, to, fire_time};
}

void StateClassReachabilityGraph::compute_enabled_and_clocks(
    StateClass& state) {
  size_t num_transitions = ptpn_.num_transitions();

  std::set<size_t> new_enabled;
  for (size_t t = 0; t < num_transitions; ++t) {
    if (matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, t)) {
      new_enabled.insert(t);
    }
  }

  state.Z1.resize(num_transitions + 1);
  state.Z2.resize(num_transitions + 1);

  std::set<size_t> old_enabled = state.enabled;

  std::set<size_t> to_remove;
  for (size_t t : old_enabled) {
    if (new_enabled.find(t) == new_enabled.end()) {
      to_remove.insert(t);
    }
  }

  for (size_t t : to_remove) {
    size_t clock_idx = t + 1;
    if (clock_idx < state.Z1.size()) {
      state.Z1.reset_clock(clock_idx);
    }
    if (clock_idx < state.Z2.size()) {
      state.Z2.reset_clock(clock_idx);
    }
    state.Z1.unfreeze_clock(clock_idx);
    state.Z2.unfreeze_clock(clock_idx);
  }

  state.enabled = new_enabled;

  for (size_t t : state.enabled) {
    const auto& transition = ptpn_.get_transition(t);
    int alpha = transition.time_interval.earliest;
    int beta = transition.time_interval.latest == matrix_ptpn::INF
                   ? INF_TIME
                   : transition.time_interval.latest;

    size_t clock_idx = t + 1;

    bool clock_just_reset = false;
    if (transition.suspendable) {
      if (clock_idx < state.Z2.size()) {
        int upper = state.Z2.get_constraint(clock_idx, 0);
        int lower = -state.Z2.get_constraint(0, clock_idx);
        if (upper == INF_TIME && lower == 0) {
          clock_just_reset = true;
        }
      }
    } else {
      if (clock_idx < state.Z1.size()) {
        int upper = state.Z1.get_constraint(clock_idx, 0);
        int lower = -state.Z1.get_constraint(0, clock_idx);
        if (upper == INF_TIME && lower == 0) {
          clock_just_reset = true;
        }
      }
    }

    bool is_newly_enabled = old_enabled.find(t) == old_enabled.end();

    if (!is_newly_enabled && !clock_just_reset) {
      continue;
    }

    if (!transition.suspendable) {
      if (beta != INF_TIME) {
        state.Z1.set_constraint(clock_idx, 0, beta);
      }
      state.Z1.set_constraint(0, clock_idx, -alpha);
    } else {
      if (beta != INF_TIME) {
        state.Z2.set_constraint(clock_idx, 0, beta);
      }
      state.Z2.set_constraint(0, clock_idx, -alpha);
    }
  }

  state.Z1.minimize();
  state.Z2.minimize();

  spdlog::debug("    计算使能和时钟: 使能={}个变迁", state.enabled.size());
}

}  // namespace state_class