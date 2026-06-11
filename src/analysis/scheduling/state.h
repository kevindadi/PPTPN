#ifndef ANALYSIS_SCHEDULING_STATE_H
#define ANALYSIS_SCHEDULING_STATE_H

#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <limits>

#include "../../petri/petri.h"
#include "dbm.h"
#include "scheduler.h"

namespace scheduling {

struct StateClass;
struct StateKey;
struct StateKeyHash;

/**
 * StateClass - 状态类
 *
 * 状态类由以下分量组成：
 * - marking：Petri 网标识
 * - zone：DBM 时钟区域
 * - enabled：使能集合（仅由标识决定）
 * - active：活跃集合（每核最高优先级变迁）
 * - suspended：挂起集合（被抢占的可挂起变迁）
 *
 * 辅助信息（不参与等价判断）：
 * - state_id：状态 ID
 * - cumulative_time：累计时间
 */
struct StateClass {
  // ===== 核心状态（参与等价判断）=====
  std::vector<int> marking;    // Petri 网标识
  DBM zone;                    // DBM 时钟区域

  std::set<size_t> enabled; // 使能集合
  std::set<size_t> active;     // 活跃集合
  std::set<size_t> suspended;   // 挂起集合

  // ===== 辅助信息（不参与等价判断）=====
  size_t state_id = 0;
  double cumulative_time = 0.0;

  // ===== 时钟映射 =====
  std::vector<int> transition_to_clock;     // transition_id → clock_idx
  std::vector<size_t> clock_to_transition;  // clock_idx → transition_id

  StateClass() = default;
  explicit StateClass(const std::vector<int>& m, size_t num_transitions = 0)
      : marking(m) {
    if (num_transitions > 0) {
      transition_to_clock.assign(num_transitions, -1);
      clock_to_transition.push_back(std::numeric_limits<size_t>::max());
    }
  }

  StateClass(const StateClass& other) = default;
  StateClass& operator=(const StateClass& other) = default;

  bool operator==(const StateClass& other) const;
  bool operator!=(const StateClass& other) const;
  bool operator<(const StateClass& other) const;

  [[nodiscard]] StateClass copy() const;
  [[nodiscard]] std::string to_string() const;

  // 时钟映射操作
  [[nodiscard]] int clock_index_for_transition(size_t tid) const;
  [[nodiscard]] size_t transition_for_clock(size_t idx) const;
  [[nodiscard]] bool has_clock_for_transition(size_t tid) const;

  // 辅助方法
  [[nodiscard]] bool has_active_clocks() const { return !active.empty(); }

  [[nodiscard]] int get_next_deadline() const {
    int min_deadline = INF_TIME;
    for (size_t t : active) {
      if (t < transition_to_clock.size() &&
          has_clock_for_transition(t)) {
        min_deadline = std::min(
            min_deadline,
            zone.get_upper_bound(static_cast<size_t>(clock_index_for_transition(t))));
      }
    }
    return min_deadline;
  }
};

/**
 * StateKey - 用于哈希和等价判断的状态键
 */
struct StateKey {
  std::vector<int> marking;
  std::vector<int> zone_matrix;
  std::set<size_t> frozen_clocks;
  std::set<size_t> enabled;
  std::set<size_t> active;
  std::set<size_t> suspended;

  bool operator==(const StateKey& other) const;
};

struct StateKeyHash {
  size_t operator()(const StateKey& key) const;
};

/**
 * 状态等价判断（精确匹配）
 */
bool are_equivalent(const StateClass& a, const StateClass& b);

}  // namespace scheduling

#endif  // ANALYSIS_SCHEDULING_STATE_H