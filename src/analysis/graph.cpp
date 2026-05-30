#include "analysis/graph.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <sstream>
#include <thread>
#include <vector>

#include "scheduling.h"

namespace state_class {

namespace {
constexpr int kControlTransitionPriority = 0;

// State key helper using new clocks/active/suspended structure
StateKey make_state_key(const StateClass& state) {
  return {state.marking, state.clocks, state.enabled, state.suspended};
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

std::string format_transition_clock_summary(const StateClass& state,
                                           const petri::PTPN& ptpn) {
  std::ostringstream oss;
  bool first = true;

  for (size_t t : state.enabled) {
    if (t >= state.clocks.size()) continue;

    if (!first) oss << "; ";
    first = false;

    const auto& trans = ptpn.get_transition(t);
    const auto& clock = state.clocks[t];
    std::string state_str;
    switch (clock.state) {
      case ClockState::UNACTIVE: state_str = "UNACTIVE"; break;
      case ClockState::ACTIVE: state_str = "ACTIVE"; break;
      case ClockState::SUSPENDED: state_str = "SUSPENDED"; break;
    }

    std::string ub_str = (clock.upper_bound == INF_TIME)
                             ? "inf"
                             : std::to_string(clock.upper_bound);

    oss << "T" << t << "(" << trans.name << ")"
        << "[" << clock.lower_bound << ", " << ub_str << "]"
        << "@" << state_str;
  }

  return first ? "(none)" : oss.str();
}

}  // namespace

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
  spdlog::debug("{}Active: {}", prefix, format_transitions(state.active));
  if (!state.suspended.empty()) {
    spdlog::debug("{}Suspended: {}", prefix, format_transitions(state.suspended));
  }
  spdlog::debug("{}Clocks: {}", prefix, format_transition_clock_summary(state, ptpn_));
  spdlog::debug("{}Cumulative time: {}", prefix, state.cumulative_time);
  spdlog::debug("{}==========================================", prefix);
}

StateClassReachabilityGraph::StateClassReachabilityGraph(const petri::PTPN& ptpn)
    : ptpn_(ptpn), next_state_id_(0), pruning_enabled_(false) {}

size_t StateClassReachabilityGraph::build(size_t max_states) {
  return build(max_states, 0);
}

size_t StateClassReachabilityGraph::build(size_t max_states,
                                          size_t thread_count) {
  stats_ = Statistics();
  graph_.clear();
  state_to_vertex_.clear();
  next_state_id_ = 0;

  StateClass s0 = create_initial_state();
  s0 = canonicalize(s0, s0);

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
       ", max_frontier_size=" + std::to_string(max_frontier_size) +
       ", truncated=" + (stats_.truncated ? "true" : "false"));

  return stats_.total_states;
}

int StateClassReachabilityGraph::compute_firing_time(const StateClass& state,
                                                    size_t t) const {
  if (t >= state.clocks.size()) {
    return -1;
  }

  if (!state.enabled.count(t) || state.suspended.count(t)) {
    return -1;
  }

  const auto& clock = state.clocks[t];
  if (clock.state == ClockState::SUSPENDED) {
    return -1;
  }

  const auto& trans = ptpn_.get_transition(t);
  const int alpha = trans.time_interval.earliest;
  const int beta = (trans.time_interval.latest == petri::INF)
                       ? INF_TIME
                       : trans.time_interval.latest;
  const int firing_lower = std::max(alpha, clock.lower_bound);

  if (beta != INF_TIME && firing_lower > beta) {
    return -1;
  }

  return firing_lower;
}

StateExpansionResult StateClassReachabilityGraph::expand_state_candidates(
    const StateClass& cur) {
  const size_t enabled_checks_before = stats_.transition_enabled_checks;

  StateExpansionResult result;

  StateClass scheduled = cur.copy();
  recompute_enabled_sets(scheduled);

  if (pruning_enabled_ && scheduled.active.empty()) {
    debug("  [Prune] No active transitions, skip");
    result.pruned_states_count++;
    result.transition_enabled_checks +=
        stats_.transition_enabled_checks - enabled_checks_before;
    return result;
  }

  // 1. 在 enabled \ suspended 上按时钟筛选（先于优先级）:取 tau_min
  int tau_min = INF_TIME;
  for (size_t t : scheduled.enabled) {
    const int tau = compute_firing_time(scheduled, t);
    if (tau >= 0) {
      tau_min = std::min(tau_min, tau);
    }
  }

  if (tau_min == INF_TIME) {
    if (pruning_enabled_) {
      result.pruned_states_count++;
    }
    result.transition_enabled_checks +=
        stats_.transition_enabled_checks - enabled_checks_before;
    return result;
  }

  std::set<size_t> firable_now;
  for (size_t t : scheduled.enabled) {
    const int tau = compute_firing_time(scheduled, t);
    if (tau == tau_min) {
      firable_now.insert(t);
    }
  }

  // 2. 在 firable_now 上按核心取最高优先级集合（可并列;控制变迁 core<0 全部保留）
  const std::set<size_t> schedulable =
      SchedulingAlgorithms::select_active_per_core(firable_now, ptpn_);
  result.chosen_count = schedulable.size();
  result.enabled_transitions_count += schedulable.size();

  if (schedulable.empty()) {
    if (pruning_enabled_) {
      result.pruned_states_count++;
    }
    result.transition_enabled_checks +=
        stats_.transition_enabled_checks - enabled_checks_before;
    return result;
  }

  // 3. 从 schedulable 中选择一个变迁发生
  const size_t chosen = SchedulingAlgorithms::select_one_transition(schedulable, ptpn_);
  const StateKey source_key = make_state_key(cur);

  spdlog::debug("  Earliest firing time: {}, firable_now={}, schedulable={}, chosen={}",
                tau_min, firable_now.size(), schedulable.size(),
                format_transitions({chosen}, false));

  auto [ok, nxt, tau] = fire_with_time(chosen, scheduled);
  if (!ok) {
    if (pruning_enabled_) {
      spdlog::debug("  {}: fire failed", format_transitions({chosen}, false));
      result.pruned_states_count++;
    } else {
      spdlog::debug("  {}: fire failed [pruning disabled]",
                    format_transitions({chosen}, false));
    }
    result.transition_enabled_checks +=
        stats_.transition_enabled_checks - enabled_checks_before;
    return result;
  }

  StateClass canonical_nxt = canonicalize(nxt, nxt);
  result.candidates.push_back({source_key, canonical_nxt,
                               TransitionEdge(static_cast<int>(chosen), tau), chosen});
  result.fired_count = 1;

  result.transition_enabled_checks +=
      stats_.transition_enabled_checks - enabled_checks_before;
  return result;
}

// =======================================================================
// 核心算法实现
// =======================================================================

double StateClassReachabilityGraph::advance_time(StateClass& state) const {
  // 1. 找到 active 集合中最紧的时间上界
  int min_ub = INF_TIME;
  for (size_t t : state.active) {
    if (t < state.clocks.size()) {
      min_ub = std::min(min_ub, state.clocks[t].upper_bound);
    }
  }

  // 2. 如果没有上界或上界 <= 0,返回 0（死锁）
  if (min_ub == INF_TIME || min_ub <= 0) {
    return 0.0;
  }

  // 3. 所有 active 时钟流逝 min_ub
  for (size_t t : state.active) {
    if (t < state.clocks.size()) {
      state.clocks[t].lower_bound += min_ub;
      state.clocks[t].upper_bound += min_ub;
    }
  }
  // suspended 时钟保持不变（冻结）

  // 4. 累计时间
  state.cumulative_time += min_ub;

  spdlog::debug("  advance_time: dt={}, new cumulative={}", min_ub, state.cumulative_time);

  return static_cast<double>(min_ub);
}

std::tuple<bool, StateClass, double> StateClassReachabilityGraph::fire_with_time(
    size_t t, const StateClass& from) const {
  // 检查时钟是否在有效状态
  if (t >= from.clocks.size()) {
    return {false, StateClass(), 0.0};
  }

  if (!from.enabled.count(t) || from.suspended.count(t)) {
    return {false, StateClass(), 0.0};
  }

  const auto& clock = from.clocks[t];
  if (clock.state == ClockState::SUSPENDED) {
    return {false, StateClass(), 0.0};
  }

  const auto& trans = ptpn_.get_transition(t);
  int alpha = trans.time_interval.earliest;
  int beta = (trans.time_interval.latest == petri::INF)
                 ? INF_TIME
                 : trans.time_interval.latest;

  // 计算触发时间:max(earliest, lower_bound)
  int firing_lower = std::max(alpha, clock.lower_bound);

  // 检查窗口是否有效
  if (beta != INF_TIME && firing_lower > beta) {
    if (pruning_enabled_) {
      spdlog::debug("  {}: firing time {} > latest {}, skip",
                    format_transitions({t}, false), firing_lower, beta);
    }
    return {false, StateClass(), 0.0};
  }

  // 激发时间
  double fire_time = static_cast<double>(firing_lower);

  // 生成新状态
  StateClass to = from.copy();
  to.marking = petri::PTPN::fire(from.marking, ptpn_, t);

  if (to.marking.empty()) {
    spdlog::debug("  {}: marking empty after fire", format_transitions({t}, false));
    return {false, StateClass(), 0.0};
  }

  // 更新时钟:激发后重置该变迁的时钟
  // 上界设为 beta（latest）,下界从 0 开始
  if (t < to.clocks.size()) {
    to.clocks[t].lower_bound = 0;
    to.clocks[t].upper_bound = beta;
    to.clocks[t].state = ClockState::UNACTIVE;  // 刚激发的变迁暂时设为 UNACTIVE
  }

  // 累计时间
  to.cumulative_time = from.cumulative_time + fire_time;

  // 重新计算使能/活跃/挂起集合
  recompute_enabled_sets(to);

  spdlog::debug("  {}: fired successfully @time {}, new cumulative={}",
                format_transitions({t}, false), fire_time, to.cumulative_time);

  return {true, to, fire_time};
}

void StateClassReachabilityGraph::recompute_enabled_sets(StateClass& state) const {
  recompute_enabled_sets_from_marking(state.marking, state);
}

void StateClassReachabilityGraph::recompute_enabled_sets_from_marking(
    const std::vector<int>& marking, StateClass& state) const {
  // 1. 从 marking 计算原始使能变迁
  std::vector<size_t> raw_enabled;
  const size_t num_transitions = ptpn_.num_transitions();

  for (size_t t = 0; t < num_transitions; ++t) {
    if (petri::PTPN::is_enabled(marking, ptpn_, t)) {
      raw_enabled.push_back(t);
    }
  }

  std::set<size_t> raw_set(raw_enabled.begin(), raw_enabled.end());

  // 2. 确保 clocks 大小正确
  if (state.clocks.size() < num_transitions) {
    state.clocks.resize(num_transitions);
  }

  // 3. 初始化新使能变迁的时钟
  for (size_t t : raw_enabled) {
    if (t < state.clocks.size()) {
      // 如果时钟是 UNACTIVE,初始化它
      if (state.clocks[t].state == ClockState::UNACTIVE) {
        const auto& trans = ptpn_.get_transition(t);
        int beta = (trans.time_interval.latest == petri::INF)
                      ? INF_TIME
                      : trans.time_interval.latest;
        state.clocks[t].lower_bound = 0;
        state.clocks[t].upper_bound = beta;
        // 默认设为 ACTIVE（如果没有更高优先级抢占）
      }
    }
  }

  // 4. 使用调度算法选择 active 和 suspended
  state.enabled = std::set<size_t>(raw_enabled.begin(), raw_enabled.end());

  // 使用 SchedulingAlgorithms::select_active_per_core
  std::set<size_t> active_set = SchedulingAlgorithms::select_active_per_core(
      state.enabled, ptpn_);

  // 使用 SchedulingAlgorithms::compute_suspended
  std::set<size_t> suspended_set = SchedulingAlgorithms::compute_suspended(
      state.enabled, active_set, ptpn_);

  // 5. 更新集合
  state.active = active_set;
  state.suspended = suspended_set;

  // 6. 更新时钟状态
  // active 时钟设为 ACTIVE
  for (size_t t : state.active) {
    if (t < state.clocks.size()) {
      if (state.clocks[t].state != ClockState::ACTIVE) {
        state.clocks[t].state = ClockState::ACTIVE;
      }
    }
  }

  // suspended 时钟设为 SUSPENDED（保持冻结值）
  for (size_t t : state.suspended) {
    if (t < state.clocks.size()) {
      if (state.clocks[t].state != ClockState::SUSPENDED) {
        state.clocks[t].state = ClockState::SUSPENDED;
      }
    }
  }

  spdlog::debug("  recompute_enabled: enabled={}, active={}, suspended={}",
                state.enabled.size(), state.active.size(), state.suspended.size());
}

// =======================================================================
// 调度相关（使用 SchedulingAlgorithms）
// =======================================================================

std::set<size_t> StateClassReachabilityGraph::select_active_per_core(
    const std::set<size_t>& enabled) const {
  return SchedulingAlgorithms::select_active_per_core(enabled, ptpn_);
}

std::set<size_t> StateClassReachabilityGraph::compute_suspended(
    const std::set<size_t>& enabled,
    const std::set<size_t>& active) const {
  return SchedulingAlgorithms::compute_suspended(enabled, active, ptpn_);
}

// =======================================================================
// 抢占/恢复语义
// =======================================================================

void StateClassReachabilityGraph::suspend_transition(size_t t, StateClass& state) const {
  if (state.clocks[t].state != ClockState::ACTIVE) {
    return;
  }

  state.active.erase(t);
  state.suspended.insert(t);
  state.clocks[t].state = ClockState::SUSPENDED;

  spdlog::debug("  suspend_transition: T{} now frozen", t);
}

void StateClassReachabilityGraph::restore_transition(size_t t, StateClass& state) const {
  if (state.clocks[t].state != ClockState::SUSPENDED) {
    return;
  }

  state.suspended.erase(t);
  state.active.insert(t);
  state.clocks[t].state = ClockState::ACTIVE;

  spdlog::debug("  restore_transition: T{} resumed", t);
}

// =======================================================================
// 规范化
// =======================================================================

StateClass StateClassReachabilityGraph::canonicalize(
    const StateClass& a, const StateClass& b) const {
  return ::state_class::canonicalize(a, b, canonicalization_mode_);
}

bool StateClassReachabilityGraph::are_equivalent(
    const StateClass& a, const StateClass& b) const {
  return ::state_class::are_equivalent(a, b, canonicalization_mode_);
}

void StateClassReachabilityGraph::set_canonicalization_mode(CanonicalizationMode mode) {
  canonicalization_mode_ = mode;
}

CanonicalizationMode StateClassReachabilityGraph::get_canonicalization_mode() const {
  return canonicalization_mode_;
}

void StateClassReachabilityGraph::set_pruning_enabled(bool enabled) {
  pruning_enabled_ = enabled;
}

// =======================================================================
// 状态工厂
// =======================================================================

StateClass StateClassReachabilityGraph::create_initial_state() {
  StateClass initial;
  initial.marking = ptpn_.get_marking();
  initial.cumulative_time = 0.0;

  const size_t num_transitions = ptpn_.num_transitions();
  initial.clocks.resize(num_transitions);
  initial.state_id = next_state_id_++;

  // 初始化调度状态
  recompute_enabled_sets(initial);

  // 设置初始时钟状态
  for (size_t t : initial.enabled) {
    if (t < initial.clocks.size()) {
      const auto& trans = ptpn_.get_transition(t);
      int beta = (trans.time_interval.latest == petri::INF)
                    ? INF_TIME
                    : trans.time_interval.latest;
      initial.clocks[t].lower_bound = 0;
      initial.clocks[t].upper_bound = beta;

      // 根据 active/suspended 设置状态
      if (initial.active.count(t)) {
        initial.clocks[t].state = ClockState::ACTIVE;
      } else if (initial.suspended.count(t)) {
        initial.clocks[t].state = ClockState::SUSPENDED;
      } else {
        initial.clocks[t].state = ClockState::UNACTIVE;
      }
    }
  }

  log_state_class_details(initial, "[Initial] ");

  debug("[STATE] Create initial state:");
  spdlog::debug("  Marking: {}", format_marking(initial.marking));
  spdlog::debug("  Enabled: {}", format_transitions(initial.enabled));
  spdlog::debug("  Active: {}", format_transitions(initial.active));
  if (!initial.suspended.empty()) {
    spdlog::debug("  Suspended: {}", format_transitions(initial.suspended));
  }

  return initial;
}

// ===================================================================
// 辅助方法
// ===================================================================

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

std::vector<size_t> StateClassReachabilityGraph::select_per_core(
    const std::set<size_t>& enabled) const {
  std::set<size_t> result = SchedulingAlgorithms::select_active_per_core(enabled, ptpn_);
  return std::vector<size_t>(result.begin(), result.end());
}

void StateClassReachabilityGraph::apply_preemption(
    const std::vector<size_t>& chosen, StateClass& state) const {
  // 使用新的调度算法处理抢占
  std::set<size_t> active = SchedulingAlgorithms::select_active_per_core(
      state.enabled, ptpn_);
  std::set<size_t> suspended = SchedulingAlgorithms::compute_suspended(
      state.enabled, active, ptpn_);
  state.active = active;
  state.suspended = suspended;

  // 更新时钟状态
  for (size_t t : state.active) {
    if (t < state.clocks.size()) {
      state.clocks[t].state = ClockState::ACTIVE;
    }
  }
  for (size_t t : state.suspended) {
    if (t < state.clocks.size()) {
      state.clocks[t].state = ClockState::SUSPENDED;
    }
  }
}

std::set<size_t> StateClassReachabilityGraph::compute_effective_enabled(
    const std::vector<size_t>& raw_enabled) const {
  std::set<size_t> raw_set(raw_enabled.begin(), raw_enabled.end());
  return SchedulingAlgorithms::select_active_per_core(raw_set, ptpn_);
}

std::set<size_t> StateClassReachabilityGraph::compute_suspended_transitions(
    const std::vector<size_t>& raw_enabled,
    const std::set<size_t>& effective_enabled) const {
  std::set<size_t> raw_set(raw_enabled.begin(), raw_enabled.end());
  return SchedulingAlgorithms::compute_suspended(raw_set, effective_enabled, ptpn_);
}

bool StateClassReachabilityGraph::maximal_time_elapse(StateClass& state, double& dt) const {
  dt = advance_time(state);
  return dt > 0;
}

std::tuple<bool, StateClass, double> StateClassReachabilityGraph::fire_with_dbm(
    size_t trans_idx, const StateClass& from_state) {
  return fire_with_time(trans_idx, from_state);
}

void StateClassReachabilityGraph::compute_enabled_and_clocks(StateClass& state) {
  recompute_enabled_sets(state);
}

bool StateClassReachabilityGraph::is_suspended(
    size_t trans_idx, const std::vector<size_t>& enabled) const {
  const auto& trans = ptpn_.get_transition(trans_idx);
  if (!trans.suspendable) return false;

  for (size_t other_t : enabled) {
    if (other_t == trans_idx) continue;
    const auto& other_trans = ptpn_.get_transition(other_t);
    if (other_trans.core == trans.core && other_trans.suspendable &&
        other_trans.priority > trans.priority) {
      return true;
    }
  }
  return false;
}

void StateClassReachabilityGraph::recompute_suspension(StateClass& state) const {
  recompute_enabled_sets(state);
}

// =======================================================================
// 持久化
// =======================================================================

SCVertex StateClassReachabilityGraph::find_or_add_vertex(const StateClass& state) {
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
      out << "Active: " << state.active.size() << "\\n";
      out << "Time: " << std::fixed << std::setprecision(2) << state.cumulative_time;
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
      out << "      \"active_count\": " << state.active.size() << ",\n";
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

}  // namespace state_class