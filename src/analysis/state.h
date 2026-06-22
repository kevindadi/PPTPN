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
struct ReachabilityState;
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

// SchedulingState — 调度投影部分.
//
// 这一部分是从 marking 派生使能集合后,再叠加每核优先级投影得到的视图数据,
// 不属于纯 Petri 网核. 设计上把它单独成一个类型,使得状态等价/去重逻辑可以
// 按"标识 / 时间 / 调度"三个关注点分别比较（见设计文档 invariant #6）.
//
//   enabled   : 原始使能变迁（仅由 marking 决定）
//   active    : 活跃变迁 = enabled ∩ 每核最高优先级（非挂起,时钟流逝）
//   suspended : 挂起变迁（使能但被同核高优先级压制,时钟冻结）
struct SchedulingState {
  std::set<size_t> enabled;
  std::set<size_t> active;
  std::set<size_t> suspended;

  bool operator==(const SchedulingState& other) const {
    return enabled == other.enabled && active == other.active &&
           suspended == other.suspended;
  }

  bool operator<(const SchedulingState& other) const {
    if (enabled < other.enabled) return true;
    if (other.enabled < enabled) return false;
    if (active < other.active) return true;
    if (other.active < active) return false;
    return suspended < other.suspended;
  }

  void clear() {
    enabled.clear();
    active.clear();
    suspended.clear();
  }
};

// SearchMetadata — 可达图构造过程中的搜索元数据.
//
// 这些字段只用于标识与调试/导出输出,不属于状态语义,也不参与状态等价/去重
// (见 ReachabilityState::operator== / operator< / StateKeyHash 均不比较本部分).
// 单独成一个类型,使"不参与等价"这一约束在类型层面可见（设计文档分层第 4 关注点）.
struct SearchMetadata {
  double cumulative_time = 0.0;  // 累计时间（仅元数据,不参与等价）
  size_t state_id = 0;           // 状态 ID（仅元数据,不参与等价）

  void clear() {
    cumulative_time = 0.0;
    state_id = 0;
  }
};

// Symbolic state used during reachability construction.
//
// 语义上由三个关注点决定（与设计文档的分层一致）:
//   1. 标识 (marking)                : discrete Petri-net marking
//   2. 时间 (clocks / zone / 映射)   : symbolic timing constraints
//   3. 调度 (scheduling)             : derived scheduling projection
//
// metadata (cumulative_time / state_id) 属于 search metadata,不参与状态等价.

// TimingState — 符号时间部分.
//
// 这一部分承载状态的时间语义: DBM zone 是主表示,clocks 是按迁移 id 的
// 兼容/调试视图,transition_to_clock / clock_to_transition 是迁移 id 与 DBM
// 时钟下标之间的双向映射. 单独成一个类型,使状态等价/去重可以把"时间"作为
// 一个整体关注点比较（见设计文档 invariant #6）.
//
// 注意: StateKey 出于哈希需要保留自己扁平的 transition_to_clock /
// clock_to_transition / zone_matrix / frozen_clocks 布局,与本类型无关.
struct TimingState {
  std::vector<TransitionClock> clocks;        // 迁移期兼容/调试视图
  DBM zone;                                  // 主时间语义表示
  std::vector<int> transition_to_clock;       // transition id -> DBM clock idx
  std::vector<size_t> clock_to_transition;    // DBM clock idx -> transition id

  bool operator==(const TimingState& other) const {
    return clocks == other.clocks && zone == other.zone &&
           transition_to_clock == other.transition_to_clock &&
           clock_to_transition == other.clock_to_transition;
  }

  // 保持与原 ReachabilityState::operator< 完全一致的子顺序:
  //   transition_to_clock, clock_to_transition, zone, clocks.
  bool operator<(const TimingState& other) const {
    if (transition_to_clock < other.transition_to_clock) return true;
    if (other.transition_to_clock < transition_to_clock) return false;
    if (clock_to_transition < other.clock_to_transition) return true;
    if (other.clock_to_transition < clock_to_transition) return false;
    if (zone < other.zone) return true;
    if (other.zone < zone) return false;
    return clocks < other.clocks;
  }

  void clear() {
    clocks.clear();
    zone = DBM{};
    transition_to_clock.clear();
    clock_to_transition.clear();
  }
};

struct ReachabilityState {
  std::vector<int> marking;     // Petri 网标识

  TimingState timing;           // 符号时间（DBM / clocks / 映射）

  SchedulingState scheduling;   // 调度投影（派生视图,不是核行为）

  SearchMetadata metadata;      // 搜索元数据（不参与状态等价）

  ReachabilityState() = default;

  explicit ReachabilityState(const std::vector<int>& m, size_t num_transitions = 0)
      : marking(m) {
    if (num_transitions > 0) {
      timing.clocks.resize(num_transitions);
    }
  }

  ReachabilityState(const ReachabilityState& other) = default;
  ReachabilityState& operator=(const ReachabilityState& other) = default;

  bool operator==(const ReachabilityState& other) const;
  bool operator<(const ReachabilityState& other) const;

  [[nodiscard]] ReachabilityState copy() const;
  [[nodiscard]] std::string to_string() const;

  void rebuild_zone_from_clocks();
  void sync_clocks_from_zone();
  [[nodiscard]] int clock_index_for_transition(size_t transition_id) const;
  [[nodiscard]] size_t transition_for_clock(size_t clock_idx) const;
  [[nodiscard]] bool has_zone_clock_for_transition(size_t transition_id) const;

  // Helper: 是否是有效活跃时钟
  [[nodiscard]] bool has_active_clocks() const {
    return !scheduling.active.empty();
  }

  // Helper: 获取下一个到期时间
  [[nodiscard]] int get_next_deadline() const {
    int min_deadline = INF_TIME;
    for (size_t t : scheduling.active) {
      if (t < timing.clocks.size()) {
        min_deadline = std::min(min_deadline, timing.clocks[t].upper_bound);
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
  SchedulingState scheduling;

  bool operator==(const StateKey& other) const {
    return marking == other.marking &&
           transition_to_clock == other.transition_to_clock &&
           clock_to_transition == other.clock_to_transition &&
           zone_matrix == other.zone_matrix &&
           frozen_clocks == other.frozen_clocks &&
           scheduling == other.scheduling;
  }
};

struct StateKeyHash {
  size_t operator()(const StateKey& key) const;
};

struct SuccessorCandidate {
  StateKey source_key;
  ReachabilityState state;
  TransitionEdge edge;
  size_t transition_id = 0;
};

}  // namespace state_class

#endif  // ANALYSIS_STATE_H