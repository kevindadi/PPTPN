#ifndef ANALYSIS_STATE_H
#define ANALYSIS_STATE_H

#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "clock_state.h"
#include "dbm.h"

namespace state_class {

// Forward declarations
struct StateClass;
struct StateKey;
struct StateKeyHash;
struct TransitionEdge;
struct SuccessorCandidate;

struct TransitionEdge {
  int transition_id;
  double firing_time;

  TransitionEdge() : transition_id(-1), firing_time(0.0) {}

  TransitionEdge(int tid, double time)
      : transition_id(tid), firing_time(time) {}

  bool operator==(const TransitionEdge& other) const {
    return transition_id == other.transition_id &&
           std::abs(firing_time - other.firing_time) < 1e-9;
  }

  [[nodiscard]] std::string to_string() const {
    std::ostringstream oss;
    oss << "T" << transition_id << "@" << firing_time;
    return oss.str();
  }
};

// Symbolic state used during reachability construction.
//
// Semantically it is determined by three components:
//   1. marking    : discrete Petri-net marking
//   2. clocks     : TransitionClock array for each transition
//   3. enabled    : raw enabled transitions from marking
//   4. active     : enabled but not suspended (clock ticking) 
//   5. suspended  : enabled but suspended (clock frozen)
//
// cumulative_time is auxiliary metadata and is not part of state identity.
struct StateClass {
  std::vector<int> marking;                      // Petri 网标识
  std::vector<TransitionClock> clocks;           // 迁移期兼容/调试视图
  DBM zone;                                     // 主时间语义表示
  std::vector<int> transition_to_clock;         // transition id -> DBM clock idx
  std::vector<size_t> clock_to_transition;      // DBM clock idx -> transition id

  std::set<size_t> enabled;     // 原始使能变迁
  std::set<size_t> active;      // 活跃变迁 = enabled ∩ 非挂起
  std::set<size_t> suspended;   // 挂起变迁（时钟冻结）

  double cumulative_time;  // 累计时间（不参与状态等价）
  size_t state_id;        // 状态 ID

  StateClass() : cumulative_time(0.0), state_id(0) {}

  explicit StateClass(const std::vector<int>& m, size_t num_transitions = 0)
      : marking(m), state_id(0), cumulative_time(0.0) {
    if (num_transitions > 0) {
      clocks.resize(num_transitions);
    }
  }

  StateClass(const StateClass& other) = default;
  StateClass& operator=(const StateClass& other) = default;

  bool operator==(const StateClass& other) const;
  bool operator<(const StateClass& other) const;

  [[nodiscard]] StateClass copy() const;
  [[nodiscard]] std::string to_string() const;

  void rebuild_zone_from_clocks();
  void sync_clocks_from_zone();
  [[nodiscard]] int clock_index_for_transition(size_t transition_id) const;
  [[nodiscard]] size_t transition_for_clock(size_t clock_idx) const;
  [[nodiscard]] bool has_zone_clock_for_transition(size_t transition_id) const;

  // Helper: 是否是有效活跃时钟
  [[nodiscard]] bool has_active_clocks() const {
    return !active.empty();
  }

  // Helper: 获取下一个到期时间
  [[nodiscard]] int get_next_deadline() const {
    int min_deadline = INF_TIME;
    for (size_t t : active) {
      if (t < clocks.size()) {
        min_deadline = std::min(min_deadline, clocks[t].upper_bound);
      }
    }
    return min_deadline;
  }
};

struct StateKey {
  std::vector<int> marking;
  std::vector<int> transition_to_clock;
  std::vector<size_t> clock_to_transition;
  std::vector<int> zone_matrix;
  std::set<size_t> frozen_clocks;
  std::set<size_t> enabled;
  std::set<size_t> active;
  std::set<size_t> suspended;

  bool operator==(const StateKey& other) const {
    return marking == other.marking &&
           transition_to_clock == other.transition_to_clock &&
           clock_to_transition == other.clock_to_transition &&
           zone_matrix == other.zone_matrix &&
           frozen_clocks == other.frozen_clocks &&
           enabled == other.enabled &&
           active == other.active &&
           suspended == other.suspended;
  }
};

struct StateKeyHash {
  size_t operator()(const StateKey& key) const;
};

struct SuccessorCandidate {
  StateKey source_key;
  StateClass state;
  TransitionEdge edge;
  size_t transition_id = 0;
};

}  // namespace state_class

#endif  // ANALYSIS_STATE_H