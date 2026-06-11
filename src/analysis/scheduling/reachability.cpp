#include "reachability.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

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

Vertex ReachabilityGraph::find_or_add_state(const StateClass& state) {
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
        Vertex succ_vertex = find_or_add_state(succ);
        edges_.push_back({v, succ_vertex});
        stats_.total_edges++;

        // 如果是新状态，加入下一轮 frontier
        if (states_[succ_vertex].state_id == succ.state_id) {
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

}  // namespace scheduling