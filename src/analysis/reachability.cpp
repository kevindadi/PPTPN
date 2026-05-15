#include "analysis/state.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <queue>
#include <spdlog/spdlog.h>

namespace state_class {

static void debug(const std::string& msg) {
  spdlog::debug("[STATE] {}", msg);
}

static void info(const std::string& msg) {
  spdlog::info("[STATE] {}", msg);
}

// ===== DBM Implementation =====
DBM::DBM(size_t size) : clock_count_(size) {
  if (size > 0) {
    matrix_.resize(size, std::vector<int>(size, INF_TIME));
    for (size_t i = 0; i < size; ++i) {
      matrix_[i][i] = 0;
    }
    if (size > 1) {
      for (size_t i = 1; i < size; ++i) {
        matrix_[i][0] = INF_TIME;
        matrix_[0][i] = 0;
      }
    }
  }
}

DBM::DBM(const DBM& other)
    : matrix_(other.matrix_),
      clock_count_(other.clock_count_),
      frozen_clocks_(other.frozen_clocks_) {}

DBM& DBM::operator=(const DBM& other) {
  if (this != &other) {
    matrix_ = other.matrix_;
    clock_count_ = other.clock_count_;
    frozen_clocks_ = other.frozen_clocks_;
  }
  return *this;
}

void DBM::check_index(size_t i, size_t j) const {
  if (i >= clock_count_ || j >= clock_count_) {
    throw std::out_of_range("DBM index out of range");
  }
}

void DBM::set_constraint(size_t i, size_t j, int bound) {
  check_index(i, j);
  matrix_[i][j] = bound;
}

int DBM::get_constraint(size_t i, size_t j) const {
  check_index(i, j);
  return matrix_[i][j];
}

bool DBM::is_consistent() const {
  if (clock_count_ == 0) return true;

  for (size_t i = 0; i < clock_count_; ++i) {
    if (matrix_[i][i] < 0) {
      return false;
    }
  }

  DBM temp = *this;
  temp.minimize();

  for (size_t i = 0; i < clock_count_; ++i) {
    if (temp.matrix_[i][i] < 0) {
      return false;
    }
  }

  return true;
}

void DBM::minimize() {
  if (clock_count_ == 0) return;

  for (size_t k = 0; k < clock_count_; ++k) {
    for (size_t i = 0; i < clock_count_; ++i) {
      if (matrix_[i][k] == INF_TIME) continue;

      for (size_t j = 0; j < clock_count_; ++j) {
        if (matrix_[k][j] == INF_TIME) continue;

        int new_bound = matrix_[i][k] + matrix_[k][j];
        if (matrix_[i][j] == INF_TIME || new_bound < matrix_[i][j]) {
          matrix_[i][j] = new_bound;
        }
      }
    }
  }
}

size_t DBM::add_clock() {
  size_t new_idx = clock_count_;
  resize(clock_count_ + 1);
  return new_idx;
}

void DBM::resize(size_t new_size) {
  if (new_size == clock_count_) return;

  size_t old_size = clock_count_;
  clock_count_ = new_size;

  matrix_.resize(new_size);
  for (auto& row : matrix_) {
    row.resize(new_size, INF_TIME);
  }

  for (size_t i = old_size; i < new_size; ++i) {
    initialize_clock(i);
  }
}

void DBM::initialize_clock(size_t clock_idx) {
  if (clock_idx >= clock_count_) return;

  matrix_[clock_idx][clock_idx] = 0;

  if (clock_idx == 0) {
    for (size_t i = 1; i < clock_count_; ++i) {
      matrix_[0][i] = 0;
      matrix_[i][0] = INF_TIME;
    }
  } else {
    matrix_[clock_idx][0] = INF_TIME;
    matrix_[0][clock_idx] = 0;

    for (size_t i = 1; i < clock_count_; ++i) {
      if (i != clock_idx) {
        matrix_[clock_idx][i] = INF_TIME;
        matrix_[i][clock_idx] = INF_TIME;
      }
    }
  }
}

void DBM::elapse_time(int delta) {
  if (delta <= 0) return;

  for (size_t i = 1; i < clock_count_; ++i) {
    if (!is_frozen(i)) {
      int current_upper = matrix_[i][0];
      if (current_upper != INF_TIME) {
        matrix_[i][0] = INF_TIME;
      }
    }
  }

  minimize();
}

void DBM::reset_clock(size_t clock_idx) {
  check_index(clock_idx, clock_idx);

  if (clock_idx == 0) {
    return;
  }

  matrix_[clock_idx][0] = 0;
  matrix_[0][clock_idx] = 0;

  minimize();
}

DBM DBM::intersection(const DBM& other) const {
  if (clock_count_ != other.clock_count_) {
    throw std::invalid_argument("DBM sizes must match for intersection");
  }

  DBM result(clock_count_);

  for (size_t i = 0; i < clock_count_; ++i) {
    for (size_t j = 0; j < clock_count_; ++j) {
      int bound1 = matrix_[i][j];
      int bound2 = other.matrix_[i][j];

      if (bound1 == INF_TIME) {
        result.matrix_[i][j] = bound2;
      } else if (bound2 == INF_TIME) {
        result.matrix_[i][j] = bound1;
      } else {
        result.matrix_[i][j] = std::min(bound1, bound2);
      }
    }
  }

  result.minimize();

  return result;
}

bool DBM::is_empty() const { return !is_consistent(); }

void DBM::prune() {
  minimize();
}

bool DBM::contains(const DBM& other) const {
  if (clock_count_ != other.clock_count_) {
    return false;
  }

  for (size_t i = 0; i < clock_count_; ++i) {
    for (size_t j = 0; j < clock_count_; ++j) {
      int this_bound = matrix_[i][j];
      int other_bound = other.matrix_[i][j];

      if (other_bound != INF_TIME &&
          (this_bound == INF_TIME || other_bound < this_bound)) {
        return false;
      }
    }
  }

  return true;
}

std::string DBM::to_string() const {
  if (clock_count_ == 0) {
    return "DBM(empty)";
  }

  std::ostringstream oss;
  oss << "DBM(size=" << clock_count_ << "):\n";
  oss << "   ";
  for (size_t j = 0; j < clock_count_; ++j) {
    oss << std::setw(8) << "x" << j;
  }
  oss << "\n";

  for (size_t i = 0; i < clock_count_; ++i) {
    oss << "x" << i << " ";
    for (size_t j = 0; j < clock_count_; ++j) {
      if (matrix_[i][j] == INF_TIME) {
        oss << std::setw(8) << "∞";
      } else {
        oss << std::setw(8) << matrix_[i][j];
      }
    }
    oss << "\n";
  }

  return oss.str();
}

bool DBM::operator==(const DBM& other) const {
  if (clock_count_ != other.clock_count_) {
    return false;
  }

  if (frozen_clocks_ != other.frozen_clocks_) {
    return false;
  }

  for (size_t i = 0; i < clock_count_; ++i) {
    for (size_t j = 0; j < clock_count_; ++j) {
      if (matrix_[i][j] != other.matrix_[i][j]) {
        return false;
      }
    }
  }

  return true;
}

bool DBM::operator<(const DBM& other) const {
  if (clock_count_ != other.clock_count_) {
    return clock_count_ < other.clock_count_;
  }

  for (size_t i = 0; i < clock_count_; ++i) {
    for (size_t j = 0; j < clock_count_; ++j) {
      if (matrix_[i][j] != other.matrix_[i][j]) {
        if (matrix_[i][j] == INF_TIME) return false;
        if (other.matrix_[i][j] == INF_TIME) return true;
        return matrix_[i][j] < other.matrix_[i][j];
      }
    }
  }

  return false;
}

void DBM::remove_clock(size_t clock_idx) {
  if (clock_idx >= clock_count_) {
    return;
  }

  if (clock_idx == 0) {
    return;
  }

  frozen_clocks_.erase(clock_idx);

  std::vector<std::vector<int>> new_matrix;
  new_matrix.resize(clock_count_ - 1);

  for (size_t i = 0; i < clock_count_; ++i) {
    if (i == clock_idx) continue;

    size_t new_i = (i < clock_idx) ? i : i - 1;
    new_matrix[new_i].resize(clock_count_ - 1);

    for (size_t j = 0; j < clock_count_; ++j) {
      if (j == clock_idx) continue;

      size_t new_j = (j < clock_idx) ? j : j - 1;
      new_matrix[new_i][new_j] = matrix_[i][j];
    }
  }

  matrix_ = new_matrix;
  clock_count_--;

  std::set<size_t> new_frozen;
  for (size_t idx : frozen_clocks_) {
    if (idx < clock_idx) {
      new_frozen.insert(idx);
    } else if (idx > clock_idx) {
      new_frozen.insert(idx - 1);
    }
  }
  frozen_clocks_ = new_frozen;
}

DBM DBM::restrict_for_firing(size_t transition_id, int alpha, int beta) const {
  size_t clock_idx = transition_id + 1;

  if (clock_idx >= clock_count_) {
    return *this;
  }

  DBM result = *this;

  int current_lower = -result.get_constraint(0, clock_idx);
  if (alpha > current_lower) {
    result.set_constraint(0, clock_idx, -alpha);
  }

  int current_upper = result.get_constraint(clock_idx, 0);
  if (beta != INF_TIME && (current_upper == INF_TIME || beta < current_upper)) {
    result.set_constraint(clock_idx, 0, beta);
  }

  result.minimize();

  if (result.is_empty()) {
    return DBM(0);
  }

  return result;
}

void DBM::freeze_clock(size_t clock_idx) {
  if (clock_idx >= clock_count_ || clock_idx == 0) {
    return;
  }

  frozen_clocks_.insert(clock_idx);
}

void DBM::unfreeze_clock(size_t clock_idx) {
  frozen_clocks_.erase(clock_idx);
}

bool DBM::is_frozen(size_t clock_idx) const {
  return frozen_clocks_.find(clock_idx) != frozen_clocks_.end();
}

void DBM::copy_clock_constraints(size_t clock_idx, DBM& target) const {
  if (clock_idx >= clock_count_) {
    return;
  }

  if (clock_idx >= target.size()) {
    target.resize(clock_idx + 1);
  }

  for (size_t i = 0; i < clock_count_; ++i) {
    if (i < target.size()) {
      target.set_constraint(clock_idx, i, matrix_[clock_idx][i]);
      target.set_constraint(i, clock_idx, matrix_[i][clock_idx]);
    }
  }

  if (is_frozen(clock_idx)) {
    target.freeze_clock(clock_idx);
  }
}

bool StateClass::operator==(const StateClass& other) const {
  if (marking != other.marking) return false;
  if (!(Z1 == other.Z1)) return false;
  if (!(Z2 == other.Z2)) return false;
  if (enabled != other.enabled) return false;
  if (suspended != other.suspended) return false;
  return true;
}

bool StateClass::operator<(const StateClass& other) const {
  if (marking < other.marking) return true;
  if (other.marking < marking) return false;

  if (Z1 < other.Z1) return true;
  if (other.Z1 < Z1) return false;

  if (Z2 < other.Z2) return true;
  if (other.Z2 < Z2) return false;

  if (enabled < other.enabled) return true;
  if (other.enabled < enabled) return false;

  return suspended < other.suspended;
}

StateClass StateClass::copy() const {
  StateClass result;
  result.marking = marking;
  result.Z1 = Z1;
  result.Z2 = Z2;
  result.state_id = state_id;
  result.cumulative_time = cumulative_time;
  result.enabled = enabled;
  result.suspended = suspended;
  return result;
}

std::string StateClass::to_string() const {
  std::ostringstream oss;
  oss << "StateClass(id=" << state_id << ", time=" << cumulative_time << ")\n";
  oss << "  Marking: [";
  for (size_t i = 0; i < marking.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << marking[i];
  }
  oss << "]\n";
  oss << "  Z1 (non-suspendable):\n" << Z1.to_string();
  oss << "  Z2 (suspendable):\n" << Z2.to_string();
  return oss.str();
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

StateClassReachabilityGraph::StateClassReachabilityGraph(
    const petri::MatrixPTPN& ptpn)
    : ptpn_(ptpn), next_state_id_(0), pruning_enabled_(false) {}

size_t StateClassReachabilityGraph::build(size_t max_states) {
  stats_ = Statistics();
  state_to_vertex_.clear();

  StateClass s0 = create_initial_state_class();
  StateClass canonical_s0 = canonicalize(s0);

  SCVertex s0_vertex = find_or_add_vertex(s0);
  initial_vertex_ = s0_vertex;
  stats_.total_states++;

  std::map<std::tuple<std::vector<int>, DBM, DBM>, SCVertex> uniq;
  uniq[{s0.marking, s0.Z1, s0.Z2}] = s0_vertex;

  std::queue<StateClass> Q;
  Q.push(canonical_s0);

  size_t iteration = 0;
  while (!Q.empty() && stats_.total_states < max_states) {
    iteration++;
    StateClass cur = Q.front();
    Q.pop();

    SCVertex u = find_or_add_vertex(cur);

    log_state_class_details(cur,
                            "[State " + std::to_string(cur.state_id) + "] ");

    std::vector<size_t> chosen = select_per_core(cur.enabled);
    stats_.enabled_transitions_count += chosen.size();

    StateClass scheduled = cur.copy();
    apply_preemption(chosen, scheduled);

    double dt = 0;
    if (maximal_time_elapse(scheduled, dt)) {
      spdlog::debug("  Maximal time elapse: dt = {}", dt);
    }

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
        } else {
          spdlog::debug("  {}: fire failed [pruning disabled]", format_transitions({t}, false));
          continue;
        }
      }

      spdlog::debug("  {} -> successor: ID={}", format_transitions({t}, false), nxt.state_id);

      auto key = std::make_tuple(nxt.marking, nxt.Z1, nxt.Z2);
      SCVertex v;

      if (uniq.find(key) != uniq.end()) {
        v = uniq[key];
        debug("  [Existing] Use existing state");
      } else {
        StateClass canonical_nxt = canonicalize(nxt);
        v = find_or_add_vertex(nxt);
        Q.push(canonical_nxt);
        uniq[key] = v;
        stats_.total_states++;
        debug("  [New] Add to graph and queue");
        log_state_class_details(
            nxt, "[New state " + std::to_string(nxt.state_id) + "] ");
      }

      TransitionEdge edge(static_cast<int>(t), tau);
      boost::add_edge(u, v, edge, graph_);
      stats_.total_transitions++;
      fired_count++;
    }

    spdlog::info("[STATE] State {}: {} candidates, {} fired, queue size: {}, total states: {}",
                 cur.state_id, chosen.size(), fired_count, Q.size(), stats_.total_states);
  }

  spdlog::info("[STATE] Build complete: iterations={}, states={}", iteration, stats_.total_states);

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
  return petri::MatrixPTPN::is_enabled(state.marking, ptpn_, trans_idx);
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
          petri::MatrixPTPN::is_enabled(state.marking, ptpn_, trans_idx);
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

void StateClassReachabilityGraph::recompute_suspension(
    StateClass& state) const {
  std::set<size_t> enabled;
  for (size_t t = 0; t < ptpn_.num_transitions(); ++t) {
    if (petri::MatrixPTPN::is_enabled(state.marking, ptpn_, t)) {
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

  new_state.marking = petri::MatrixPTPN::fire(state.marking, ptpn_, trans_idx);

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

  std::vector<size_t> enabled;
  for (size_t t = 0; t < ptpn_.num_transitions(); ++t) {
    if (petri::MatrixPTPN::is_enabled(state.marking, ptpn_, t)) {
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
      }
    }
  }

  if (initialized_count > 0) {
    spdlog::debug("    Initialized {} new enabled clocks", initialized_count);
  }

  state.Z1.minimize();
  state.Z2.minimize();
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
  auto it = state_to_vertex_.find(state);
  if (it != state_to_vertex_.end()) {
    return it->second;
  }

  StateClass new_state = state;
  new_state.state_id = next_state_id_++;
  SCVertex v = boost::add_vertex(new_state, graph_);
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

    typedef boost::graph_traits<SCGraph>::edge_iterator SCVIterator;
    SCVIterator ei, ei_end;
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

    typedef boost::graph_traits<SCGraph>::edge_iterator SCVIterator;
    SCVIterator ei, ei_end;
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
          transition_t.priority > transition_u.priority) {
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
  to.state_id = next_state_id_++;

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

  to.marking = petri::MatrixPTPN::fire(to.marking, ptpn_, trans_idx);

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
    if (petri::MatrixPTPN::is_enabled(state.marking, ptpn_, t)) {
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