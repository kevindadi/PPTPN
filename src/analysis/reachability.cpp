#include "analysis/state.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <queue>
#include <spdlog/spdlog.h>

namespace state_class {

namespace {
constexpr int kNoSchedulingPriority = INT_MAX;

StateKey make_state_key(const StateClass& state) {
  return {state.marking, state.Z1, state.Z2};
}

bool has_higher_priority(const petri::Transition& lhs,
                         const petri::Transition& rhs) {
  if (lhs.priority == kNoSchedulingPriority) {
    return false;
  }
  if (rhs.priority == kNoSchedulingPriority) {
    return true;
  }
  return lhs.priority > rhs.priority;
}
}

static void debug(const std::string& msg) {
  spdlog::debug("[STATE] {}", msg);
}

static void info(const std::string& msg) {
  spdlog::info("[STATE] {}", msg);
}

// ===== StateClassReachabilityGraph Implementation =====
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
    return "(none)";
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
      result += ", priority=" + std::to_string(trans.priority);
      result += ", core=" + std::to_string(trans.core);
      result += trans.suspendable ? ", suspendable" : "";
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
  spdlog::debug("{}========== State Class Details: ID={} ==========", prefix, state.state_id);
  spdlog::debug("{}Places: {}", prefix, format_places(state.marking));
  spdlog::debug("{}Enabled: {}", prefix, format_transitions(state.enabled));
  if (!state.suspended.empty()) {
    spdlog::debug("{}Suspended: {}", prefix, format_transitions(state.suspended));
  }
  spdlog::debug("{}Cumulative time: {}", prefix, state.cumulative_time);

  spdlog::debug("{}Z1 (non-suspendable):", prefix);
  std::string z1_str = state.Z1.to_string();
  std::istringstream z1_stream(z1_str);
  std::string z1_line;
  while (std::getline(z1_stream, z1_line)) {
    spdlog::debug("{}  {}", prefix, z1_line);
  }

  spdlog::debug("{}Z2 (suspendable):", prefix);
  std::string z2_str = state.Z2.to_string();
  std::istringstream z2_stream(z2_str);
  std::string z2_line;
  while (std::getline(z2_stream, z2_line)) {
    spdlog::debug("{}  {}", prefix, z2_line);
  }

  spdlog::debug("{}==========================================", prefix);
}

StateClassReachabilityGraph::StateClassReachabilityGraph(const petri::PTPN& ptpn)
    : ptpn_(ptpn), next_state_id_(0), pruning_enabled_(false) {}

size_t StateClassReachabilityGraph::build(size_t max_states) {
  stats_ = Statistics();
  reset_dbm_instrumentation();
  state_to_vertex_.clear();

  StateClass s0 = canonicalize(create_initial_state_class());

  SCVertex s0_vertex = find_or_add_vertex(s0);
  initial_vertex_ = s0_vertex;
  stats_.total_states++;

  std::queue<StateClass> Q;
  Q.push(s0);

  size_t iteration = 0;
  size_t max_queue_size = Q.size();
  while (!Q.empty()) {
    if (stats_.total_states >= max_states) {
      stats_.truncated = true;
      break;
    }

    iteration++;
    StateClass cur = Q.front();
    Q.pop();

    SCVertex u = find_or_add_vertex(cur);
    const StateClass& graph_cur = boost::get(boost::vertex_name, graph_, u);
    cur.state_id = graph_cur.state_id;

    log_state_class_details(cur,
                            "[State " + std::to_string(cur.state_id) + "] ");

    std::vector<size_t> chosen = select_per_core(cur.enabled);
    stats_.enabled_transitions_count += chosen.size();

    StateClass scheduled = cur.copy();
    apply_preemption(chosen, scheduled);

    double dt = 0;
    maximal_time_elapse(scheduled, dt);

    if (pruning_enabled_ && scheduled.Z1.is_empty()) {
      debug("  [Prune] Z1 empty, skip");
      stats_.pruned_states_count++;
      continue;
    } else if (!pruning_enabled_ && scheduled.Z1.is_empty()) {
      debug("  [Warning] Z1 empty but pruning disabled");
    }

    size_t fired_count = 0;
    for (size_t t : chosen) {
      auto [ok, nxt, tau] = fire_with_dbm(t, scheduled);

      if (!ok) {
        if (pruning_enabled_) {
          spdlog::debug("  {}: fire failed", format_transitions({t}, false));
          stats_.pruned_states_count++;
          continue;
        }

        spdlog::debug("  {}: fire failed [pruning disabled]",
                      format_transitions({t}, false));
        continue;
      }

      StateClass canonical_nxt = canonicalize(nxt);
      SCVertex v;

      auto key = make_state_key(canonical_nxt);
      auto state_it = state_to_vertex_.find(key);
      if (state_it != state_to_vertex_.end()) {
        v = state_it->second;
        stats_.dedup_hits_count++;
        debug("  [Existing] Use existing state");
      } else {
        v = find_or_add_vertex(canonical_nxt);
        Q.push(canonical_nxt);
        max_queue_size = std::max(max_queue_size, Q.size());
        stats_.total_states++;
        stats_.dedup_misses_count++;
        debug("  [New] Add to graph and queue");
        log_state_class_details(
            canonical_nxt,
            "[New state " + std::to_string(canonical_nxt.state_id) + "] ");
      }

      TransitionEdge edge(static_cast<int>(t), tau);
      boost::add_edge(u, v, edge, graph_);
      stats_.total_transitions++;
      fired_count++;
    }

    spdlog::debug(
        "[STATE] State {}: {} candidates, {} fired, queue size: {}, total states: {}",
        cur.state_id, chosen.size(), fired_count, Q.size(),
        stats_.total_states);
  }

  stats_.dbm_minimize_calls = get_dbm_instrumentation().minimize_calls;

  if (stats_.truncated) {
    spdlog::warn(
        "[STATE] Build truncated at max_states={} with {} states and {} queued states remaining",
        max_states, stats_.total_states, Q.size());
  }

  info("Build complete: iterations=" + std::to_string(iteration) +
       ", states=" + std::to_string(stats_.total_states) +
       ", transitions=" + std::to_string(stats_.total_transitions) +
       ", dedup_hits=" + std::to_string(stats_.dedup_hits_count) +
       ", dedup_misses=" + std::to_string(stats_.dedup_misses_count) +
       ", is_enabled_checks=" +
       std::to_string(stats_.transition_enabled_checks) +
       ", dbm_minimize_calls=" +
       std::to_string(stats_.dbm_minimize_calls) +
       ", max_queue_size=" + std::to_string(max_queue_size) +
       ", truncated=" + (stats_.truncated ? "true" : "false"));

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

  log_state_class_details(initial, "[Initial] ");

  debug("[STATE] Create initial state:");
  spdlog::debug("  Marking: {}", format_marking(initial.marking));
  spdlog::debug("  Enabled: {}", format_transitions(initial.enabled));
  if (!initial.suspended.empty()) {
    spdlog::debug("  Suspended: {}", format_transitions(initial.suspended));
  }

  return initial;
}

void StateClassReachabilityGraph::explore_successors(
    const StateClass& current_state, std::set<StateClass>& visited) {}

bool StateClassReachabilityGraph::is_transition_enabled(
    const StateClass& state, size_t trans_idx) const {
  const_cast<Statistics&>(stats_).transition_enabled_checks++;
  return petri::PTPN::is_enabled(state.marking, ptpn_, trans_idx);
}

std::vector<size_t> StateClassReachabilityGraph::collect_enabled_transitions(
    const StateClass& state) const {
  std::vector<size_t> enabled;
  const size_t num_transitions = ptpn_.num_transitions();
  enabled.reserve(num_transitions);
  for (size_t t = 0; t < num_transitions; ++t) {
    if (is_transition_enabled(state, t)) {
      enabled.push_back(t);
    }
  }
  return enabled;
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
          petri::PTPN::is_enabled(state.marking, ptpn_, trans_idx);
    }

    if (!is_enabled) {
      continue;
    }

    if (z1_up.is_frozen(i)) {
      spdlog::debug("    Clock{}({}): frozen, skip", i, format_transitions({trans_idx}, false));
      continue;
    }

    const auto& transition = ptpn_.get_transition(trans_idx);
    bool is_exact_time =
        (transition.time_interval.earliest == transition.time_interval.latest &&
         transition.time_interval.latest != petri::INF);

    if (is_exact_time && !transition.suspendable) {
      spdlog::debug("    Clock{}({}): exact time constraint", i, format_transitions({trans_idx}, false));
      continue;
    }

    int current_upper = z1_up.get_constraint(i, 0);
    if (current_upper != INF_TIME) {
      z1_up.set_constraint(i, 0, INF_TIME);
      relaxed_count++;
      spdlog::debug("    Clock{}({}): relaxed {} -> INF", i, format_transitions({trans_idx}, false), current_upper);
    }
  }

  spdlog::debug("  Time advance: relaxed {} clocks", relaxed_count);

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

  for (size_t other_t : enabled) {
    if (other_t == trans_idx) continue;

    const auto& other_trans = ptpn_.get_transition(other_t);
    if (other_trans.core == transition.core && !other_trans.suspendable &&
        has_higher_priority(other_trans, transition)) {
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
  int beta = transition.time_interval.latest == petri::INF
                 ? INF_TIME
                 : transition.time_interval.latest;

  DBM restricted = restrict_for_firing(z1, trans_idx);
  return !restricted.is_empty();
}

DBM StateClassReachabilityGraph::restrict_for_firing(const DBM& z,
                                                     size_t trans_idx) const {
  const auto& transition = ptpn_.get_transition(trans_idx);
  int alpha = transition.time_interval.earliest;
  int beta = transition.time_interval.latest == petri::INF
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
  int beta = transition.time_interval.latest == petri::INF
                 ? INF_TIME
                 : transition.time_interval.latest;

  int dbm_lower = -z1_up.get_constraint(0, clock_idx);

  int firing_time_int = std::max(alpha, dbm_lower);

  if (beta != INF_TIME && firing_time_int > beta) {
    firing_time_int = beta;
  }

  return static_cast<double>(firing_time_int);
}

StateClass StateClassReachabilityGraph::canonicalize(
    const StateClass& state) const {
  StateClass canonical = state;

  recompute_suspension(canonical);
  canonical.Z1.minimize();
  canonical.Z2.minimize();

  return canonical;
}

void StateClassReachabilityGraph::recompute_suspension(
    StateClass& state) const {
  std::vector<size_t> enabled_vec = collect_enabled_transitions(state);
  std::set<size_t> enabled(enabled_vec.begin(), enabled_vec.end());

  state.enabled = enabled;

  std::set<size_t> suspended;

  for (size_t t : enabled) {
    if (is_suspended(t, enabled_vec)) {
      suspended.insert(t);

      if (state.suspended.find(t) == state.suspended.end()) {
        spdlog::debug("    {}: suspended, freeze clock", format_transitions({t}, false));
        size_t clock_idx = t + 1;
        state.Z1.copy_clock_constraints(clock_idx, state.Z2);
        state.Z1.freeze_clock(clock_idx);
        state.Z2.freeze_clock(clock_idx);
      }
    } else {
      if (state.suspended.find(t) != state.suspended.end()) {
        spdlog::debug("    {}: not suspended, unfreeze clock", format_transitions({t}, false));
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
  spdlog::debug("    Fire transition {} @time {}", format_transitions({trans_idx}, false), firing_time);

  StateClass new_state = state;

  new_state.marking = petri::PTPN::fire(state.marking, ptpn_, trans_idx);

  spdlog::debug("      Marking: {} -> {}", format_marking(state.marking), format_marking(new_state.marking));

  new_state.cumulative_time = state.cumulative_time + firing_time;

  const auto& transition = ptpn_.get_transition(trans_idx);
  size_t clock_idx = trans_idx + 1;

  if (transition.suspendable) {
    spdlog::debug("      Reset Z2 clock {}", clock_idx);
    new_state.Z2.reset_clock(clock_idx);
  } else {
    spdlog::debug("      Reset Z1 clock {}", clock_idx);
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

  std::vector<size_t> enabled = collect_enabled_transitions(state);
  std::vector<bool> enabled_flags(num_transitions, false);
  for (size_t t : enabled) {
    enabled_flags[t] = true;
  }

  bool z1_changed = false;
  bool z2_changed = false;
  size_t cleared_count = 0;
  size_t initialized_count = 0;

  for (size_t t = 0; t < num_transitions; ++t) {
    if (!enabled_flags[t]) {
      size_t clock_idx = t + 1;

      if (clock_idx < state.Z1.size()) {
        int upper = state.Z1.get_constraint(clock_idx, 0);
        int lower = state.Z1.get_constraint(0, clock_idx);
        if (upper != INF_TIME || lower != 0) {
          state.Z1.forget_clock(clock_idx);
          z1_changed = true;
          cleared_count++;
        }
      }
      if (clock_idx < state.Z2.size()) {
        int upper = state.Z2.get_constraint(clock_idx, 0);
        int lower = state.Z2.get_constraint(0, clock_idx);
        if (upper != INF_TIME || lower != 0) {
          state.Z2.forget_clock(clock_idx);
          z2_changed = true;
        }
      }

      state.Z1.unfreeze_clock(clock_idx);
      state.Z2.unfreeze_clock(clock_idx);
    }
  }

  if (cleared_count > 0) {
    spdlog::debug("    Cleared {} clocks", cleared_count);
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
      std::string latest_str = transition.time_interval.latest == petri::INF
          ? "inf"
          : std::to_string(transition.time_interval.latest);
      spdlog::debug("    Initialize{} clock: [{}, {}]",
                    format_transitions({trans_idx}, false),
                    transition.time_interval.earliest,
                    latest_str);

      if (transition.suspendable) {
        if (transition.time_interval.earliest > 0) {
          state.Z2.set_constraint(0, clock_idx, -transition.time_interval.earliest);
        } else {
          state.Z2.set_constraint(0, clock_idx, 0);
        }
        if (transition.time_interval.latest != petri::INF) {
          state.Z2.set_constraint(clock_idx, 0, transition.time_interval.latest);
        } else {
          state.Z2.set_constraint(clock_idx, 0, INF_TIME);
        }
        z2_changed = true;
      } else {
        if (transition.time_interval.earliest > 0) {
          state.Z1.set_constraint(0, clock_idx, -transition.time_interval.earliest);
        } else {
          state.Z1.set_constraint(0, clock_idx, 0);
        }
        if (transition.time_interval.latest != petri::INF) {
          state.Z1.set_constraint(clock_idx, 0, transition.time_interval.latest);
        } else {
          state.Z1.set_constraint(clock_idx, 0, INF_TIME);
        }
        z1_changed = true;
      }
    }
  }

  if (initialized_count > 0) {
    spdlog::debug("    Initialized {} new enabled clocks", initialized_count);
  }

  if (z1_changed) {
    state.Z1.minimize();
  }
  if (z2_changed) {
    state.Z2.minimize();
  }
}

bool StateClassReachabilityGraph::should_prune(
    const StateClass& state, const std::set<StateClass>& visited) const {
  if (state.Z1.is_empty() || state.Z2.is_empty()) {
    spdlog::info("    State {} pruned due to empty Z1/Z2", state.state_id);
    return true;
  }

  return visited.find(state) != visited.end();
}

SCVertex StateClassReachabilityGraph::find_or_add_vertex(
    const StateClass& state) {
  auto key = make_state_key(state);
  auto it = state_to_vertex_.find(key);
  if (it != state_to_vertex_.end()) {
    return it->second;
  }

  StateClass new_state = state;
  new_state.state_id = next_state_id_++;
  SCVertex v = boost::add_vertex(new_state, graph_);
  state_to_vertex_.emplace(make_state_key(new_state), v);
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

    typedef boost::graph_traits<SCGraph>::vertex_iterator SCVIterator;
    SCVIterator vi, vi_end;
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

    typedef boost::graph_traits<SCGraph>::edge_iterator SCEIterator;
    SCEIterator ei, ei_end;
    for (std::tie(ei, ei_end) = boost::edges(graph_); ei != ei_end; ++ei) {
      SCVertex src = boost::source(*ei, graph_);
      SCVertex tgt = boost::target(*ei, graph_);
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

    typedef boost::graph_traits<SCGraph>::vertex_iterator SCVIterator;
    SCVIterator vi, vi_end;
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

    typedef boost::graph_traits<SCGraph>::edge_iterator SCEIterator;
    SCEIterator ei, ei_end;
    bool first_trans = true;
    for (std::tie(ei, ei_end) = boost::edges(graph_); ei != ei_end; ++ei) {
      SCVertex src = boost::source(*ei, graph_);
      SCVertex tgt = boost::target(*ei, graph_);
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
        << ",\n";
    out << "    \"dedup_hits_count\": " << stats_.dedup_hits_count << ",\n";
    out << "    \"dedup_misses_count\": " << stats_.dedup_misses_count << ",\n";
    out << "    \"transition_enabled_checks\": "
        << stats_.transition_enabled_checks << ",\n";
    out << "    \"dbm_minimize_calls\": " << stats_.dbm_minimize_calls << ",\n";
    out << "    \"truncated\": " << (stats_.truncated ? "true" : "false")
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

    auto it = best_per_core.find(core);
    if (it == best_per_core.end() ||
        has_higher_priority(transition, ptpn_.get_transition(it->second))) {
      best_per_core[core] = t;
    }
  }

  std::vector<size_t> chosen;
  for (const auto& [core, trans_idx] : best_per_core) {
    chosen.push_back(trans_idx);
  }

  spdlog::debug("  Per-core highest priority ({})", chosen.size());

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
          has_higher_priority(transition_t, transition_u)) {
        state.suspended.insert(u);

        size_t clock_idx = u + 1;
        if (clock_idx < state.Z1.size() && clock_idx < state.Z2.size()) {
          state.Z1.copy_clock_constraints(clock_idx, state.Z2);
          state.Z1.freeze_clock(clock_idx);
          state.Z2.freeze_clock(clock_idx);
        }

        spdlog::debug("    {}: preempted by {}, freeze",
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

  spdlog::debug("  Maximal time elapse: dt = {}", dt);

  return true;
}

std::tuple<bool, StateClass, double> StateClassReachabilityGraph::fire_with_dbm(
    size_t trans_idx, const StateClass& from_state) {
  StateClass to = from_state.copy();

  const auto& transition = ptpn_.get_transition(trans_idx);
  int alpha = transition.time_interval.earliest;
  int beta = transition.time_interval.latest == petri::INF
                 ? INF_TIME
                 : transition.time_interval.latest;

  const DBM& targetZ = transition.suspendable ? to.Z2 : to.Z1;

  DBM zcheck = restrict_for_firing(targetZ, trans_idx);

  if (pruning_enabled_) {
    if (zcheck.is_empty() || !zcheck.is_consistent()) {
      spdlog::debug("    {}: firing window check failed", format_transitions({trans_idx}, false));
      return {false, StateClass(), 0.0};
    }
  } else {
    if (zcheck.is_empty() || !zcheck.is_consistent()) {
      spdlog::debug("    {}: firing window check failed but pruning disabled", format_transitions({trans_idx}, false));
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

  to.marking = petri::PTPN::fire(to.marking, ptpn_, trans_idx);

  if (to.marking.empty()) {
    spdlog::debug("    {}: marking empty after fire", format_transitions({trans_idx}, false));
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

  spdlog::debug("    {}: fired successfully, fire_time = {}", format_transitions({trans_idx}, false), fire_time);

  return {true, to, fire_time};
}

void StateClassReachabilityGraph::compute_enabled_and_clocks(
    StateClass& state) {
  size_t num_transitions = ptpn_.num_transitions();

  std::set<size_t> new_enabled;
  for (size_t t = 0; t < num_transitions; ++t) {
    if (is_transition_enabled(state, t)) {
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
      state.Z1.forget_clock(clock_idx);
    }
    if (clock_idx < state.Z2.size()) {
      state.Z2.forget_clock(clock_idx);
    }
    state.Z1.unfreeze_clock(clock_idx);
    state.Z2.unfreeze_clock(clock_idx);
  }

  state.enabled = new_enabled;

  for (size_t t : state.enabled) {
    const auto& transition = ptpn_.get_transition(t);
    int alpha = transition.time_interval.earliest;
    int beta = transition.time_interval.latest == petri::INF
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

  spdlog::debug("    Compute enabled and clocks: {} enabled", state.enabled.size());
}

}  // namespace state_class