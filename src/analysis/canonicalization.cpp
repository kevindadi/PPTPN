#include "canonicalization.h"
#include "state.h"

namespace state_class {

StateClass canonicalize(const StateClass& a, const StateClass& b,
                        CanonicalizationMode mode) {
  StateClass result;

  // 确保两个状态的 marking 相同
  if (a.marking != b.marking) {
    return result;  // 返回空结果
  }
  result.marking = a.marking;

  // 根据模式规范化时钟
  const size_t num_clocks = std::min(a.clocks.size(), b.clocks.size());
  result.clocks.resize(num_clocks);

  for (size_t i = 0; i < num_clocks; ++i) {
    const TransitionClock& ca = a.clocks[i];
    const TransitionClock& cb = b.clocks[i];

    TransitionClock cr;

    switch (mode) {
      case CanonicalizationMode::EQUALITY:
        // 完全相等:直接使用 a 的时钟
        cr = ca;
        break;

      case CanonicalizationMode::MAX_LOWER_BOUND:
        // 取最大下界
        cr.lower_bound = std::max(ca.lower_bound, cb.lower_bound);
        cr.upper_bound = std::min(ca.upper_bound, cb.upper_bound);
        // 状态取较不活跃的状态（UNACTIVE < SUSPENDED < ACTIVE）
        cr.state = (ca.state < cb.state) ? ca.state : cb.state;
        break;

      case CanonicalizationMode::INTERSECTION:
        // 取约束交集
        cr.lower_bound = std::max(ca.lower_bound, cb.lower_bound);
        cr.upper_bound = std::min(ca.upper_bound, cb.upper_bound);
        // 只有当两个状态相同时才保留该状态,否则降级
        cr.state = (ca.state == cb.state) ? ca.state : ClockState::UNACTIVE;
        break;
    }

    result.clocks[i] = cr;
  }

  // 合并 enabled, active, suspended 集合
  // 对于 active 和 suspended,取交集;对于 enabled,取并集
  std::set_union(a.enabled.begin(), a.enabled.end(),
                 b.enabled.begin(), b.enabled.end(),
                 std::inserter(result.enabled, result.enabled.end()));

  std::set_intersection(a.active.begin(), a.active.end(),
                        b.active.begin(), b.active.end(),
                        std::inserter(result.active, result.active.end()));

  std::set_intersection(a.suspended.begin(), a.suspended.end(),
                        b.suspended.begin(), b.suspended.end(),
                        std::inserter(result.suspended, result.suspended.end()));

  // 使用较大的 state_id 和累计时间
  result.state_id = std::max(a.state_id, b.state_id);
  result.cumulative_time = std::max(a.cumulative_time, b.cumulative_time);

  return result;
}

bool are_equivalent(const StateClass& a, const StateClass& b,
                    CanonicalizationMode mode) {
  // marking 必须完全相同
  if (a.marking != b.marking) {
    return false;
  }

  // 时钟数量必须相同
  if (a.clocks.size() != b.clocks.size()) {
    return false;
  }

  bool clocks_equiv = true;

  switch (mode) {
    case CanonicalizationMode::EQUALITY:
      // 完全相等检查
      clocks_equiv = (a.clocks == b.clocks);
      break;

    case CanonicalizationMode::MAX_LOWER_BOUND:
      // 检查每个时钟:下界相同且上界相同（忽略状态差异）
      for (size_t i = 0; i < a.clocks.size(); ++i) {
        if (a.clocks[i].lower_bound != b.clocks[i].lower_bound ||
            a.clocks[i].upper_bound != b.clocks[i].upper_bound) {
          clocks_equiv = false;
          break;
        }
      }
      break;

    case CanonicalizationMode::INTERSECTION:
      // 检查每个时钟:下界相同且上界相同
      for (size_t i = 0; i < a.clocks.size(); ++i) {
        if (a.clocks[i].lower_bound != b.clocks[i].lower_bound ||
            a.clocks[i].upper_bound != b.clocks[i].upper_bound) {
          clocks_equiv = false;
          break;
        }
      }
      break;
  }

  return clocks_equiv;
}

}  // namespace state_class