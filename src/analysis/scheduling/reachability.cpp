#include "reachability.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>

namespace scheduling {

ReachabilityGraph::ReachabilityGraph(const petri::PTPN& ptpn)
    : ptpn_(ptpn), next_state_id_(0) {}

StateKey ReachabilityGraph::make_key(const StateClass& state) const {
  StateKey key;
  key.marking = state.marking;
  key.zone_matrix = state.zone.raw_matrix();
  key.frozen_clocks = state.zone.frozen_clocks();
  key.enabled = state.enabled;
  key.active = state.active;
  key.suspended = state.suspended;
  return key;
}

ReachabilityGraph::Vertex ReachabilityGraph::find_or_add_state(const StateClass& state) {
  StateKey key = make_key(state);
  auto it = state_to_vertex_.find(key);
  if (it != state_to_vertex_.end()) {
    stats_.dedup_hits++;
    return it->second;
  }

  stats_.dedup_misses++;
  StateClass new_state = state;
  new_state.state_id = next_state_id_++;

  Vertex v = states_.size();
  states_.push_back(new_state);
  state_to_vertex_.emplace(std::move(key), v);

  return v;
}

std::set<size_t> ReachabilityGraph::compute_enabled(
    const std::vector<int>& marking) const {
  std::set<size_t> enabled;
  const size_t num_transitions = ptpn_.num_transitions();

  for (size_t t = 0; t < num_transitions; ++t) {
    if (petri::PTPN::is_enabled(marking, ptpn_, t)) {
      enabled.insert(t);
    }
  }

  return enabled;
}

int ReachabilityGraph::compute_firing_time(
    const StateClass& state, size_t t) const {
  if (!state.enabled.count(t)) {
    return -1;
  }

  if (state.suspended.count(t)) {
    return -1;  // 挂起的变迁不能发生
  }

  if (!state.has_clock_for_transition(t)) {
    return -1;
  }

  const auto& trans = ptpn_.get_transition(t);
  const int alpha = trans.time_interval.earliest;
  const int beta = trans.time_interval.latest == petri::INF
                       ? INF_TIME
                       : trans.time_interval.latest;

  const size_t clock_idx = static_cast<size_t>(
      state.clock_index_for_transition(t));
  const int lower_bound = state.zone.get_lower_bound(clock_idx);
  const int upper_bound = state.zone.get_upper_bound(clock_idx);

  // τ(t) = max(α(t), lb_t)
  const int firing_time = std::max(alpha, lower_bound);

  // 检查是否在时间窗口内
  if (firing_time > upper_bound) {
    return -1;
  }
  if (beta != INF_TIME && firing_time > beta) {
    return -1;
  }

  return firing_time;
}

void ReachabilityGraph::advance_time(StateClass& state, int delta) const {
  if (delta <= 0) return;

  // 推进所有非冻结的 active 时钟
  state.zone.elapse_time(delta);
  state.cumulative_time += delta;
}

size_t ReachabilityGraph::build(size_t max_states) {
  stats_ = Statistics();
  states_.clear();
  edges_.clear();
  state_to_vertex_.clear();
  next_state_id_ = 0;

  // 创建初始状态
  StateClass initial;
  initial.marking = ptpn_.get_marking();
  initial.cumulative_time = 0.0;
  initial.zone = DBM(1);  // 只有参考时钟 c₀

  recompute_sets(initial);

  Vertex initial_vertex = find_or_add_state(initial);
  stats_.total_states++;

  std::vector<Vertex> frontier{initial_vertex};

  // BFS 展开
  while (!frontier.empty()) {
    if (stats_.total_states >= max_states) {
      stats_.truncated = true;
      break;
    }

    std::vector<Vertex> next_frontier;

    for (Vertex v : frontier) {
      const StateClass& cur = states_[v];
      std::vector<StateClass> successors = expand(cur);

      for (auto& succ : successors) {
        const StateKey succ_key = make_key(succ);
        const bool is_new_state = state_to_vertex_.find(succ_key) == state_to_vertex_.end();
        Vertex succ_vertex = find_or_add_state(succ);
        edges_.push_back({v, succ_vertex});
        stats_.total_edges++;

        if (is_new_state) {
          stats_.total_states++;
          next_frontier.push_back(succ_vertex);
        }
      }
    }

    frontier = std::move(next_frontier);
  }

  return stats_.total_states;
}

const StateClass& ReachabilityGraph::get_state(Vertex v) const {
  return states_.at(v);
}

const StateClass& ReachabilityGraph::get_initial_state() const {
  return states_.front();
}

bool ReachabilityGraph::save_to_dot(const std::string& path) const {
  std::ofstream out(path);
  if (!out.is_open()) return false;

  out << "digraph ReachabilityGraph {\n";
  out << "  rankdir=LR;\n";
  out << "  node [shape=box];\n\n";

  for (size_t i = 0; i < states_.size(); ++i) {
    const auto& s = states_[i];
    out << "  s" << s.state_id << " [label=\"State " << s.state_id << "\\n";
    out << "M: [";
    for (size_t j = 0; j < s.marking.size(); ++j) {
      if (j > 0) out << ",";
      out << s.marking[j];
    }
    out << "]\\n";
    out << "Active: " << s.active.size() << "\\n";
    out << "Time: " << std::fixed << std::setprecision(2) << s.cumulative_time;
    out << "\"];\n";
  }

  out << "\n";

  for (const auto& e : edges_) {
    const auto& src = states_[e.first];
    const auto& tgt = states_[e.second];
    out << "  s" << src.state_id << " -> s" << tgt.state_id << ";\n";
  }

  out << "}\n";
  out.close();

  return true;
}

bool ReachabilityGraph::save_to_json(const std::string& path) const {
  std::ofstream out(path);
  if (!out.is_open()) return false;

  out << "{\n";
  out << "  \"states\": [\n";

  for (size_t i = 0; i < states_.size(); ++i) {
    const auto& s = states_[i];
    if (i > 0) out << ",\n";
    out << "    {\"id\": " << s.state_id << ", \"marking\": [";
    for (size_t j = 0; j < s.marking.size(); ++j) {
      if (j > 0) out << ", ";
      out << s.marking[j];
    }
    out << "], \"active_count\": " << s.active.size() << "}";
  }

  out << "\n  ],\n";
  out << "  \"transitions\": [\n";

  for (size_t i = 0; i < edges_.size(); ++i) {
    const auto& e = edges_[i];
    if (i > 0) out << ",\n";
    out << "    {\"source\": " << states_[e.first].state_id
        << ", \"target\": " << states_[e.second].state_id << "}";
  }

  out << "\n  ],\n";
  out << "  \"statistics\": {"
      << "\"total_states\": " << stats_.total_states
      << ", \"total_edges\": " << stats_.total_edges
      << ", \"dedup_hits\": " << stats_.dedup_hits
      << ", \"dedup_misses\": " << stats_.dedup_misses
      << ", \"truncated\": " << (stats_.truncated ? "true" : "false")
      << "}\n";
  out << "}\n";

  out.close();
  return true;
}

void ReachabilityGraph::recompute_sets(StateClass& state) {
  // 重新计算使能集合
  state.enabled = compute_enabled(state.marking);

  // 计算活跃集合（每核最高优先级）
  state.active = SchedulingAlgorithms::select_active_per_core(
      state.enabled, ptpn_);

  // 计算挂起集合
  state.suspended = SchedulingAlgorithms::compute_suspended(
      state.enabled, state.active, ptpn_);

  // 更新 DBM 时钟映射
  // 添加新使能变迁的时钟
  for (size_t t : state.enabled) {
    if (!state.has_clock_for_transition(t)) {
      // 添加新时钟
      size_t clock_idx = state.zone.add_clock();
      if (state.transition_to_clock.size() <= t) {
        state.transition_to_clock.resize(t + 1, -1);
      }
      state.transition_to_clock[t] = static_cast<int>(clock_idx);
      if (state.clock_to_transition.size() <= clock_idx) {
        state.clock_to_transition.resize(
            clock_idx + 1, std::numeric_limits<size_t>::max());
      }
      state.clock_to_transition[clock_idx] = t;

      // 设置初始约束
      const auto& trans = ptpn_.get_transition(t);
      state.zone.set_constraint(0, clock_idx, -trans.time_interval.earliest);
      if (trans.time_interval.latest != petri::INF) {
        state.zone.set_constraint(clock_idx, 0, trans.time_interval.latest);
      }
    }
  }

  // 冻结/解冻时钟
  for (size_t t : state.enabled) {
    if (state.has_clock_for_transition(t)) {
      size_t clock_idx = static_cast<size_t>(
          state.clock_index_for_transition(t));

      if (state.active.count(t)) {
        state.zone.unfreeze_clock(clock_idx);
      } else {
        state.zone.freeze_clock(clock_idx);
      }
    }
  }

  state.zone.minimize();
}

std::vector<StateClass> ReachabilityGraph::expand(const StateClass& state) {
  std::vector<StateClass> successors;

  // 1. 计算使能集合 E（已在 state 中）
  // 2. 计算活跃集合 X（已在 state 中）
  // 3. 计算挂起集合 R（已在 state 中）

  // 4. 计算最早发生时间
  int tau_min = INF_TIME;
  for (size_t t : state.active) {
    if (state.suspended.count(t)) continue;
    int tau = compute_firing_time(state, t);
    if (tau >= 0) {
      tau_min = std::min(tau_min, tau);
    }
  }

  if (tau_min == INF_TIME) {
    return successors;  // 无可发生变迁
  }

  // 5. 筛选可发生变迁 F = {t | τ(t) = tau_min}
  std::set<size_t> F;
  for (size_t t : state.active) {
    if (state.suspended.count(t)) continue;
    int tau = compute_firing_time(state, t);
    if (tau == tau_min) {
      F.insert(t);
    }
  }

  // 6. 在 F 上取每核最高优先级 X'
  std::set<size_t> X_prime = SchedulingAlgorithms::select_active_per_core(
      F, ptpn_);

  // 7. 从 X' 选一个变迁发生
  if (X_prime.empty()) {
    return successors;
  }

  size_t chosen = SchedulingAlgorithms::select_one_transition(X_prime, ptpn_);

  // 8. 激发变迁，生成后继
  auto successor = fire_transition(state, chosen, tau_min);
  if (successor) {
    successors.push_back(*successor);
  }

  return successors;
}

std::optional<StateClass> ReachabilityGraph::fire_transition(
    const StateClass& state, size_t transition_id, int firing_time) {

  const auto& trans = ptpn_.get_transition(transition_id);

  // 复制状态
  StateClass next = state.copy();

  // 1. 推进时间（仅 active 时钟）
  advance_time(next, firing_time);

  // 2. 更新 DBM：添加时间约束
  if (next.has_clock_for_transition(transition_id)) {
    size_t clock_idx = static_cast<size_t>(
        next.clock_index_for_transition(transition_id));

    // α(t) ≤ c_t ≤ β(t)
    next.zone.set_constraint(0, clock_idx, -trans.time_interval.earliest);
    if (trans.time_interval.latest != petri::INF) {
      next.zone.set_constraint(clock_idx, 0, trans.time_interval.latest);
    }
    next.zone.minimize();
  }

  // 3. 检查 DBM 是否为空
  if (next.zone.is_empty()) {
    return std::nullopt;
  }

  // 4. 激发变迁，更新标识
  next.marking = petri::PTPN::fire(next.marking, ptpn_, transition_id);
  if (next.marking.empty()) {
    return std::nullopt;
  }

  // 5. 重新计算使能/活跃/挂起集合
  recompute_sets(next);

  // 6. 更新累计时间
  next.cumulative_time = state.cumulative_time + firing_time;

  return next;
}

}  // namespace scheduling