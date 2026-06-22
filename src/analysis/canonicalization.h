#ifndef ANALYSIS_CANONICALIZATION_H
#define ANALYSIS_CANONICALIZATION_H

namespace state_class {

enum class CanonicalizationMode {
  EQUALITY,          // 标识、时钟完全相等才合并
  MAX_LOWER_BOUND,   // 取最大下界
  INTERSECTION       // 取约束交集
};

// 前向声明
struct ReachabilityState;

// 规范化两个状态
ReachabilityState canonicalize(const ReachabilityState& a, const ReachabilityState& b,
                        CanonicalizationMode mode);

// 检查两个状态是否在给定模式下等价
bool are_equivalent(const ReachabilityState& a, const ReachabilityState& b,
                    CanonicalizationMode mode);

}  // namespace state_class

#endif  // ANALYSIS_CANONICALIZATION_H