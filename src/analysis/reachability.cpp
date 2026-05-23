#include "analysis/state.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

namespace state_class {

namespace {
constexpr int kControlTransitionPriority = 0;

StateKey make_state_key(const StateClass& state) {
  return {state.marking, state.Z1, state.Z2, state.enabled, state.suspended};
}

void add_expansion_stats(StateClassReachabilityGraph::Statistics& target,
                         const StateExpansionResult& result) {
  target.enabled_transitions_count += result.enabled_transitions_count;
  target.pruned_states_count += result.pruned_states_count;
  target.transition_enabled_checks += result.transition_enabled_checks;
}

size_t effective_thread_count(size_t requested, size_t frontier_size) {
  if (frontier_size <= 1) {
    return 1;
  }

  size_t hardware_threads = std::thread::hardware_concurrency();
  if (hardware_threads == 0) {
    hardware_threads = 1;
  }

  size_t selected = requested;
  if (selected == 0) {
    selected = std::max<size_t>(1, hardware_threads / 4);
  }
  if (selected == 0) {
    selected = 1;
  }

  constexpr size_t kMaxAutoBuildThreads = 16;
  constexpr size_t kMaxRequestedBuildThreads = 16;
  const size_t cap = requested == 0 ? kMaxAutoBuildThreads : kMaxRequestedBuildThreads;
  return std::max<size_t>(1, std::min({selected, frontier_size, cap}));
}

bool has_higher_priority(const petri::Transition& lhs,
                         const petri::Transition& rhs) {
  if (lhs.priority == kControlTransitionPriority && lhs.core < 0) {
    return false;
  }
  if (rhs.priority == kControlTransitionPriority && rhs.core < 0) {
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

std::string format_transition_vector(const std::vector<size_t>& trans_indices,
                                     const petri::PTPN& ptpn,
                                     bool detailed = true) {
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

    if (detailed && t < ptpn.num_transitions()) {
      const auto& trans = ptpn.get_transition(t);
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

std::string format_dbm_bound(int value) {
  return value == INF_TIME ? "∞" : std::to_string(value);
}

std::string format_dbm_transition_summary(const DBM& dbm,
                                          const std::set<size_t>& transitions,
                                          const petri::PTPN& ptpn) {
  if (transitions.empty()) {
    return "(none)";
  }

  std::ostringstream oss;
  bool first = true;
  for (size_t t : transitions) {
    const size_t clock_idx = t + 1;
    if (clock_idx >= dbm.size()) {
      continue;
    }

    if (!first) {
      oss << "; ";
    }
    first = false;

    const auto& transition = ptpn.get_transition(t);
    const int lower = -dbm.get_constraint(0, clock_idx);
    const int upper = dbm.get_constraint(clock_idx, 0);
    oss << "T" << t << "(" << transition.name << ")"
        << "[" << lower << ", " << format_dbm_bound(upper) << "]";
    if (dbm.is_frozen(clock_idx)) {
      oss << " frozen";
    }
  }

  return first ? "(none)" : oss.str();
}

std::string format_semantic_dbm_summary(const StateClass& state,
                                        const petri::PTPN& ptpn) {
  std::set<size_t> active_non_suspendable;
  std::set<size_t> active_suspendable;

  for (size_t t : state.enabled) {
    const auto& transition = ptpn.get_transition(t);
    if (transition.suspendable) {
      active_suspendable.insert(t);
    } else {
      active_non_suspendable.insert(t);
    }
  }
  for (size_t t : state.suspended) {
    active_suspendable.insert(t);
  }

  std::ostringstream oss;
  oss << "Z1=" << format_dbm_transition_summary(state.Z1, active_non_suspendable, ptpn)
      << " | Z2=" << format_dbm_transition_summary(state.Z2, active_suspendable, ptpn);
  return oss.str();
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
  spdlog::debug("{}Timing: {}", prefix, format_semantic_dbm_summary(state, ptpn_));
  spdlog::debug("{}==========================================", prefix);
}

StateClassReachabilityGraph::StateClassReachabilityGraph(const petri::PTPN& ptpn)
    : ptpn_(ptpn), next_state_id_(0), pruning_enabled_(false) {}

/*
 * Symbolic reachability sketch for this implementation and related tools.
 *
 * Classical TPN state-class graph (Berthomieu-Diaz / Tina / Romeo):
 *
 *   StateClass := (M, D)
 *     M : discrete marking
 *     D : canonical firing-domain / zone for currently enabled transitions
 *
 *   build_state_class_graph(net):
 *     s0 = canonicalize(initial_marking, initial_domain)
 *     Q  = {s0}
 *     V  = {key(s0)}
 *
 *     while Q not empty:
 *       s = pop(Q)
 *
 *       for each firable transition t in enabled(s.M):
 *         D_fire = restrict(s.D, alpha_t <= x_t <= beta_t)
 *         if empty(D_fire):
 *           continue
 *
 *         M_next = fire(s.M, t)
 *         D_next = successor_domain(D_fire, t, M_next)
 *           remove clocks disabled by t
 *           preserve persistent enabled clocks
 *           initialize newly enabled clocks with their static interval
 *           project/eliminate fired clock
 *           canonicalize DBM / firing domain
 *
 *         s_next = (M_next, D_next)
 *         if key(s_next) not in V:
 *           V.insert(key(s_next))
 *           Q.push(s_next)
 *         add_edge(s, s_next, t)
 *
 * Tina-style fast solving:
 *   key(s) = (marking, canonical firing-domain)
 *   canonical DBM makes equality/hash lookup matrix-based
 *   optional reductions include inclusion/subsumption, contracted state classes,
 *   partial-order reductions, and property-driven on-the-fly exploration
 *
 * Romeo-style fast solving:
 *   ordinary TPNs use state classes / zones similarly to Tina
 *   parametric or stopwatch TPNs may need polyhedra or abstractions because
 *   stopped clocks can break pure DBM closure in the general case
 *   scheduling/priorities reduce branching by only exploring policy-legal firings
 *
 * Suspend/resume / stopwatch TPN research model:
 *   active clock:      dx/dt = 1
 *   suspended clock:   dx/dt = 0
 *   resumed clock:     keep old value, then dx/dt = 1 again
 *   disabled clock:    remove/reset according to TPN enabling semantics
 *
 * This implementation:
 *   StateKey := (marking, canonical Z1, canonical Z2, suspended-set)
 *   Z1       := non-suspendable / active transition zone
 *   Z2       := suspendable transition zone
 *   suspended:= suspendable transitions whose clocks are currently frozen
 *
 *   build(max_states):
 *     stats = {}
 *     s0 = canonicalize(create_initial_state_class())
 *     initial_vertex = find_or_add_vertex(s0)
 *     Q.push(s0)
 *
 *     while !Q.empty():
 *       if total_states >= max_states:
 *         truncated = true
 *         break
 *
 *       cur = Q.pop()
 *       u = find_or_add_vertex(cur)
 *       chosen = select_per_core(cur.enabled)
 *
 *       scheduled = cur.copy()
 *       apply_preemption(chosen, scheduled)
 *       maximal_time_elapse(scheduled)
 *
 *       for t in chosen:
 *         ok, nxt, tau = fire_with_dbm(t, scheduled)
 *         if !ok:
 *           continue
 *
 *         nxt = canonicalize(nxt)
 *         key = StateKey(nxt.marking, nxt.Z1, nxt.Z2, nxt.suspended)
 *
 *         if key in state_to_vertex:
 *           v = state_to_vertex[key]
 *         else:
 *           v = find_or_add_vertex(nxt)
 *           Q.push(nxt)
 *           total_states++
 *
 *         graph.add_edge(u, v, TransitionEdge(t, tau))
 *
 *     return total_states
 */
size_t StateClassReachabilityGraph::build(size_t max_states) {
  return build(max_states, 0);
}

size_t StateClassReachabilityGraph::build(size_t max_states,
                                          size_t thread_count) {
  stats_ = Statistics();
  reset_dbm_instrumentation();
  graph_.clear();
  state_to_vertex_.clear();
  next_state_id_ = 0;

  StateClass s0 = canonicalize(create_initial_state_class());

  SCVertex s0_vertex = find_or_add_vertex(s0);
  initial_vertex_ = s0_vertex;
  stats_.total_states++;

  std::vector<StateClass> frontier{s0};

  size_t iteration = 0;
  size_t max_frontier_size = frontier.size();
  while (!frontier.empty()) {
    if (stats_.total_states >= max_states) {
      stats_.truncated = true;
      break;
    }

    iteration++;

    const size_t worker_count = effective_thread_count(thread_count, frontier.size());
    std::vector<StateExpansionResult> results(frontier.size());
    std::vector<petri::PTPN> worker_nets(worker_count, ptpn_);
    std::vector<StateClassReachabilityGraph> workers;
    workers.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i) {
      workers.emplace_back(worker_nets[i]);
      workers.back().set_pruning_enabled(pruning_enabled_);
    }

    if (worker_count == 1) {
      for (size_t i = 0; i < frontier.size(); ++i) {
        results[i] = workers[0].expand_state_candidates(frontier[i]);
      }
    } else {
      std::atomic<size_t> next_index{0};
      std::vector<std::thread> threads;
      threads.reserve(worker_count);
      for (size_t worker_id = 0; worker_id < worker_count; ++worker_id) {
        threads.emplace_back([&, worker_id]() {
          while (true) {
            const size_t index = next_index.fetch_add(1, std::memory_order_relaxed);
            if (index >= frontier.size()) {
              break;
            }
            results[index] = workers[worker_id].expand_state_candidates(frontier[index]);
          }
        });
      }

      for (auto& thread : threads) {
        thread.join();
      }
    }

    std::vector<StateClass> next_frontier;

    for (size_t i = 0; i < frontier.size(); ++i) {
      StateClass cur = frontier[i];
      SCVertex u = find_or_add_vertex(cur);
      const StateClass& graph_cur = boost::get(boost::vertex_name, graph_, u);
      cur.state_id = graph_cur.state_id;

      log_state_class_details(cur,
                              "[State " + std::to_string(cur.state_id) + "] ");

      const StateExpansionResult& result = results[i];
      add_expansion_stats(stats_, result);

      for (const auto& candidate : result.candidates) {
        spdlog::debug("[STATE] State {} --T{}@{}--> candidate",
                      cur.state_id, candidate.transition_id,
                      candidate.edge.firing_time);

        auto state_it = state_to_vertex_.find(make_state_key(candidate.state));
        SCVertex v;
        if (state_it != state_to_vertex_.end()) {
          v = state_it->second;
          const StateClass& existing_state = boost::get(boost::vertex_name, graph_, v);
          stats_.dedup_hits_count++;
          spdlog::debug("[STATE]   [Existing] candidate merged into state {}",
                        existing_state.state_id);
          log_state_class_details(
              existing_state,
              "[Existing state " + std::to_string(existing_state.state_id) + "] ");
        } else {
          if (stats_.total_states >= max_states) {
            stats_.truncated = true;
            break;
          }

          v = find_or_add_vertex(candidate.state);
          const StateClass& new_graph_state = boost::get(boost::vertex_name, graph_, v);
          StateClass queued_state = candidate.state;
          queued_state.state_id = new_graph_state.state_id;
          next_frontier.push_back(queued_state);
          max_frontier_size = std::max(max_frontier_size, next_frontier.size());
          stats_.total_states++;
          stats_.dedup_misses_count++;
          spdlog::debug("[STATE]   [New] Add state {} to graph and frontier",
                        new_graph_state.state_id);
          log_state_class_details(
              new_graph_state,
              "[New state " + std::to_string(new_graph_state.state_id) + "] ");
        }

        boost::add_edge(u, v, candidate.edge, graph_);
        stats_.total_transitions++;
      }

      spdlog::debug(
          "[STATE] State {}: {} candidates, {} fired, frontier size: {}, total states: {}",
          cur.state_id, result.chosen_count, result.fired_count,
          next_frontier.size(), stats_.total_states);

      if (stats_.truncated) {
        break;
      }
    }

    frontier = std::move(next_frontier);
  }

  stats_.dbm_minimize_calls = get_dbm_instrumentation().minimize_calls;

  if (stats_.truncated) {
    spdlog::warn(
        "[STATE] Build truncated at max_states={} with {} states and {} frontier states remaining",
        max_states, stats_.total_states, frontier.size());
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
       ", max_frontier_size=" + std::to_string(max_frontier_size) +
       ", truncated=" + (stats_.truncated ? "true" : "false"));

  return stats_.total_states;
}

StateExpansionResult StateClassReachabilityGraph::expand_state_candidates(
    const StateClass& cur) {
  const size_t enabled_checks_before = stats_.transition_enabled_checks;

  StateExpansionResult result;
  StateClass scheduled = cur.copy();
  normalize_scheduling_state(scheduled);

  std::vector<size_t> chosen(scheduled.enabled.begin(), scheduled.enabled.end());
  result.chosen_count = chosen.size();
  result.enabled_transitions_count += chosen.size();

  double dt = 0;
  maximal_time_elapse(scheduled, dt);

  if (pruning_enabled_ && scheduled.Z1.is_empty()) {
    debug("  [Prune] Z1 empty, skip");
    result.pruned_states_count++;
    result.transition_enabled_checks +=
        stats_.transition_enabled_checks - enabled_checks_before;
    return result;
  } else if (!pruning_enabled_ && scheduled.Z1.is_empty()) {
    debug("  [Warning] Z1 empty but pruning disabled");
  }

  const StateKey source_key = make_state_key(cur);
  for (size_t t : chosen) {
    auto [ok, nxt, tau] = fire_with_dbm(t, scheduled);

    if (!ok) {
      if (pruning_enabled_) {
        spdlog::debug("  {}: fire failed", format_transitions({t}, false));
        result.pruned_states_count++;
        continue;
      }

      spdlog::debug("  {}: fire failed [pruning disabled]",
                    format_transitions({t}, false));
      continue;
    }

    StateClass canonical_nxt = canonicalize(nxt);
    result.candidates.push_back({source_key, canonical_nxt,
                                 TransitionEdge(static_cast<int>(t), tau), t});
    result.fired_count++;
  }

  result.transition_enabled_checks +=
      stats_.transition_enabled_checks - enabled_checks_before;
  return result;
}

StateClass StateClassReachabilityGraph::create_initial_state_class() {
  StateClass initial;
  initial.marking = ptpn_.get_marking();
  initial.state_id = next_state_id_++;
  initial.cumulative_time = 0.0;

  size_t num_transitions = ptpn_.num_transitions();

  initial.Z1.resize(num_transitions + 1);
  initial.Z2.resize(num_transitions + 1);

  normalize_scheduling_state(initial);

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
  auto& stats = const_cast<Statistics&>(stats_);
  stats.transition_enabled_checks++;
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
  canonical.Z1.minimize();
  canonical.Z2.minimize();
  return canonical;
}

std::set<size_t> StateClassReachabilityGraph::compute_effective_enabled(
    const std::vector<size_t>& raw_enabled) const {
  std::set<size_t> raw_enabled_set(raw_enabled.begin(), raw_enabled.end());
  std::vector<size_t> chosen = select_per_core(raw_enabled_set);
  return std::set<size_t>(chosen.begin(), chosen.end());
}

std::set<size_t> StateClassReachabilityGraph::compute_suspended_transitions(
    const std::vector<size_t>& raw_enabled,
    const std::set<size_t>& effective_enabled) const {
  std::set<size_t> suspended;
  for (size_t t : raw_enabled) {
    if (effective_enabled.find(t) != effective_enabled.end()) {
      continue;
    }

    const auto& transition = ptpn_.get_transition(t);
    if (!transition.suspendable) {
      continue;
    }
    if (transition.core < 0) {
      continue;
    }

    for (size_t chosen : effective_enabled) {
      const auto& chosen_transition = ptpn_.get_transition(chosen);
      if (chosen_transition.core < 0) {
        continue;
      }
      if (chosen_transition.core == transition.core &&
          has_higher_priority(chosen_transition, transition)) {
        suspended.insert(t);
        break;
      }
    }
  }
  return suspended;
}

void StateClassReachabilityGraph::reconcile_timing_domains(
    StateClass& state,
    const std::set<size_t>& previous_effective_enabled,
    const std::set<size_t>& previous_suspended) const {
  const size_t num_transitions = ptpn_.num_transitions();

  if (state.Z1.size() < num_transitions + 1) {
    state.Z1.resize(num_transitions + 1);
  }
  if (state.Z2.size() < num_transitions + 1) {
    state.Z2.resize(num_transitions + 1);
  }

  std::vector<bool> relevant_flags(num_transitions, false);
  for (size_t t : state.enabled) {
    relevant_flags[t] = true;
  }
  for (size_t t : state.suspended) {
    relevant_flags[t] = true;
  }

  for (size_t t = 0; t < num_transitions; ++t) {
    const size_t clock_idx = t + 1;
    const bool was_effective = previous_effective_enabled.find(t) != previous_effective_enabled.end();
    const bool was_suspended = previous_suspended.find(t) != previous_suspended.end();
    const bool is_effective = state.enabled.find(t) != state.enabled.end();
    const bool is_suspended_now = state.suspended.find(t) != state.suspended.end();
    const bool is_relevant = relevant_flags[t];

    if (!is_relevant) {
      if (clock_idx < state.Z1.size()) {
        state.Z1.forget_clock(clock_idx);
        state.Z1.unfreeze_clock(clock_idx);
      }
      if (clock_idx < state.Z2.size()) {
        state.Z2.forget_clock(clock_idx);
        state.Z2.unfreeze_clock(clock_idx);
      }
      continue;
    }

    const auto& transition = ptpn_.get_transition(t);
    const int alpha = transition.time_interval.earliest;
    const int beta = transition.time_interval.latest == petri::INF
                         ? INF_TIME
                         : transition.time_interval.latest;
    const bool became_relevant = (!was_effective && !was_suspended) &&
                                 (is_effective || is_suspended_now);

    if (transition.suspendable) {
      if (became_relevant) {
        state.Z2.set_constraint(0, clock_idx, alpha > 0 ? -alpha : 0);
        state.Z2.set_constraint(clock_idx, 0, beta);
      }

      if (clock_idx < state.Z1.size()) {
        state.Z1.forget_clock(clock_idx);
        state.Z1.unfreeze_clock(clock_idx);
      }

      if (is_suspended_now) {
        state.Z2.freeze_clock(clock_idx);
      } else {
        state.Z2.unfreeze_clock(clock_idx);
      }
    } else if (is_effective) {
      if (became_relevant) {
        state.Z1.set_constraint(0, clock_idx, alpha > 0 ? -alpha : 0);
        state.Z1.set_constraint(clock_idx, 0, beta);
      }

      if (clock_idx < state.Z2.size()) {
        state.Z2.forget_clock(clock_idx);
        state.Z2.unfreeze_clock(clock_idx);
      }
      state.Z1.unfreeze_clock(clock_idx);
    }
  }

  state.Z1.minimize();
  state.Z2.minimize();
}

void StateClassReachabilityGraph::rebuild_post_fire_timing_domains(
    StateClass& state, const StateClass& source_state, size_t fired_transition,
    const std::set<size_t>& previous_effective_enabled,
    const std::set<size_t>& previous_suspended) const {
  const size_t num_transitions = ptpn_.num_transitions();

  if (state.Z1.size() < num_transitions + 1) {
    state.Z1.resize(num_transitions + 1);
  }
  if (state.Z2.size() < num_transitions + 1) {
    state.Z2.resize(num_transitions + 1);
  }

  for (size_t t = 0; t < num_transitions; ++t) {
    const size_t clock_idx = t + 1;
    const bool was_effective = previous_effective_enabled.find(t) != previous_effective_enabled.end();
    const bool was_suspended = previous_suspended.find(t) != previous_suspended.end();
    const bool is_effective = state.enabled.find(t) != state.enabled.end();
    const bool is_suspended_now = state.suspended.find(t) != state.suspended.end();
    const bool was_relevant = was_effective || was_suspended;
    const bool is_relevant = is_effective || is_suspended_now;
    const bool preserved = t != fired_transition && was_relevant && is_relevant;
    const auto& transition = ptpn_.get_transition(t);

    if (!is_relevant) {
      state.Z1.forget_clock(clock_idx);
      state.Z1.unfreeze_clock(clock_idx);
      state.Z2.forget_clock(clock_idx);
      state.Z2.unfreeze_clock(clock_idx);
      continue;
    }

    if (transition.suspendable) {
      state.Z1.forget_clock(clock_idx);
      state.Z1.unfreeze_clock(clock_idx);

      if (preserved) {
        source_state.Z2.copy_clock_constraints(clock_idx, state.Z2);
      } else {
        state.Z2.reset_clock(clock_idx);
      }

      if (is_suspended_now) {
        state.Z2.freeze_clock(clock_idx);
      } else {
        state.Z2.unfreeze_clock(clock_idx);
      }
    } else {
      state.Z2.forget_clock(clock_idx);
      state.Z2.unfreeze_clock(clock_idx);

      if (preserved) {
        source_state.Z1.copy_clock_constraints(clock_idx, state.Z1);
      } else {
        state.Z1.reset_clock(clock_idx);
      }

      state.Z1.unfreeze_clock(clock_idx);
    }
  }

  state.Z1.minimize();
  state.Z2.minimize();
}

void StateClassReachabilityGraph::normalize_scheduling_state(
    StateClass& state) const {
  const std::set<size_t> previous_effective_enabled = state.enabled;
  const std::set<size_t> previous_suspended = state.suspended;

  const std::vector<size_t> raw_enabled = collect_enabled_transitions(state);
  state.enabled = compute_effective_enabled(raw_enabled);
  state.suspended =
      compute_suspended_transitions(raw_enabled, state.enabled);

  spdlog::debug("  Raw enabled: {}",
                format_transition_vector(raw_enabled, ptpn_));
  spdlog::debug("  Effective enabled: {}",
                format_transitions(state.enabled));
  spdlog::debug("  Suspended: {}",
                format_transitions(state.suspended));

  reconcile_timing_domains(state, previous_effective_enabled,
                           previous_suspended);
}

void StateClassReachabilityGraph::recompute_suspension(
    StateClass& state) const {
  normalize_scheduling_state(state);
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
  // Group transitions by core. core=-1 are control/dependency transitions
  // (fork/join/periodic/connector) — they are handled separately and do not
  // participate in per-core task scheduling competition.
  std::map<int, std::vector<size_t>> per_core_group;
  std::map<int, int> per_core_best_priority;

  for (size_t t : enabled) {
    const auto& transition = ptpn_.get_transition(t);
    int core = transition.core;
    if (core < 0) {
      // core=-1: control/dependency transitions, select all of them as-is
      // (they are not subject to priority-based exclusion on a "core")
      continue;
    }

    auto it = per_core_best_priority.find(core);
    if (it == per_core_best_priority.end() ||
        transition.priority > it->second) {
      per_core_best_priority[core] = transition.priority;
      per_core_group[core] = {t};
    } else if (transition.priority == it->second) {
      per_core_group[core].push_back(t);
    }
  }

  std::vector<size_t> chosen;
  for (const auto& [core, group] : per_core_group) {
    for (size_t t : group) {
      chosen.push_back(t);
    }
  }

  // Also include all core=-1 control transitions directly in the chosen set
  for (size_t t : enabled) {
    const auto& transition = ptpn_.get_transition(t);
    if (transition.core < 0) {
      chosen.push_back(t);
    }
  }

  spdlog::debug("  Per-core scheduling groups ({} total):", chosen.size());
  for (const auto& [core, group] : per_core_group) {
    spdlog::debug("    Core {}: {} transition(s)", core, group.size());
  }
  int control_count = 0;
  for (size_t t : enabled) {
    if (ptpn_.get_transition(t).core < 0) control_count++;
  }
  if (control_count > 0) {
    spdlog::debug("    Core -1 (control): {} transition(s)", control_count);
  }

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
  const DBM& active_domain = transition.suspendable ? from_state.Z2 : from_state.Z1;
  DBM firing_zone = restrict_for_firing(active_domain, trans_idx);

  if (pruning_enabled_) {
    if (firing_zone.is_empty() || !firing_zone.is_consistent()) {
      spdlog::debug("    {}: firing window check failed", format_transitions({trans_idx}, false));
      return {false, StateClass(), 0.0};
    }
  } else {
    if (firing_zone.is_empty() || !firing_zone.is_consistent()) {
      spdlog::debug("    {}: firing window check failed but pruning disabled", format_transitions({trans_idx}, false));
    }
  }

  double fire_time = from_state.cumulative_time +
                     compute_firing_time(firing_zone, trans_idx);

  if (transition.suspendable) {
    to.Z2 = firing_zone;
  } else {
    to.Z1 = firing_zone;
  }

  const std::set<size_t> previous_effective_enabled = from_state.enabled;
  const std::set<size_t> previous_suspended = from_state.suspended;

  to.marking = petri::PTPN::fire(from_state.marking, ptpn_, trans_idx);

  if (to.marking.empty()) {
    spdlog::debug("    {}: marking empty after fire", format_transitions({trans_idx}, false));
    return {false, StateClass(), 0.0};
  }

  to.cumulative_time = fire_time;
  const std::vector<size_t> raw_enabled = collect_enabled_transitions(to);
  to.enabled = compute_effective_enabled(raw_enabled);
  to.suspended = compute_suspended_transitions(raw_enabled, to.enabled);

  rebuild_post_fire_timing_domains(to, from_state, trans_idx,
                                   previous_effective_enabled,
                                   previous_suspended);
  reconcile_timing_domains(to, previous_effective_enabled,
                           previous_suspended);

  spdlog::debug("    {}: fired successfully, fire_time = {}", format_transitions({trans_idx}, false), fire_time);

  return {true, to, fire_time};
}

void StateClassReachabilityGraph::compute_enabled_and_clocks(
    StateClass& state) {
  normalize_scheduling_state(state);
  spdlog::debug("    Compute enabled and clocks: {} enabled", state.enabled.size());
}

}  // namespace state_class