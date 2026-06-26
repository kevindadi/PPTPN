#ifndef ANALYSIS_CLOCK_STATE_H
#define ANALYSIS_CLOCK_STATE_H

#include <limits>
#include <string>

namespace state_class {

constexpr int INF_TIME = std::numeric_limits<int>::max();

enum class ClockState {
  UNACTIVE,  // 不在任何集合中（不活跃）
  ACTIVE,    // 正在执行,时钟正常流逝
  SUSPENDED  // 被挂起,时钟冻结
};

struct TransitionClock {
  int lower_bound;   // 时钟下界 [lower, upper]
  int upper_bound;   // 时钟上界
  ClockState state;  // 时钟状态

  TransitionClock()
      : lower_bound(0), upper_bound(INF_TIME), state(ClockState::UNACTIVE) {}

  explicit TransitionClock(int wcet, ClockState s = ClockState::UNACTIVE)
      : lower_bound(0), upper_bound(wcet), state(s) {}

  static TransitionClock make_active(int wcet) {
    return TransitionClock(wcet, ClockState::ACTIVE);
  }

  static TransitionClock make_suspended(int current_time, int original_wcet) {
    TransitionClock tc(original_wcet, ClockState::SUSPENDED);
    tc.lower_bound = current_time;
    return tc;
  }

  bool operator==(const TransitionClock& other) const {
    return lower_bound == other.lower_bound &&
           upper_bound == other.upper_bound && state == other.state;
  }

  bool operator<(const TransitionClock& other) const {
    if (state != other.state) return state < other.state;
    if (lower_bound != other.lower_bound)
      return lower_bound < other.lower_bound;
    return upper_bound < other.upper_bound;
  }

  std::string to_string() const {
    std::string state_str;
    switch (state) {
      case ClockState::UNACTIVE:
        state_str = "UNACTIVE";
        break;
      case ClockState::ACTIVE:
        state_str = "ACTIVE";
        break;
      case ClockState::SUSPENDED:
        state_str = "SUSPENDED";
        break;
    }
    return "[" + std::to_string(lower_bound) + ", " +
           (upper_bound == INF_TIME ? "inf" : std::to_string(upper_bound)) +
           ", " + state_str + "]";
  }
};

}  // namespace state_class

#endif  // ANALYSIS_CLOCK_STATE_H