#ifndef ANALYSIS_GRAPH_H
#define ANALYSIS_GRAPH_H

#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>

#include "petri/petri.h"
#include "canonicalization.h"
#include "state.h"

namespace state_class {

// =============================================================================
// Boost.Graph type aliases
// =============================================================================

typedef boost::adjacency_list<
    boost::vecS, boost::vecS, boost::directedS,
    boost::property<boost::vertex_name_t, StateClass>,
    boost::property<boost::edge_name_t, TransitionEdge> >
    SCGraph;

typedef boost::graph_traits<SCGraph>::vertex_descriptor SCVertex;
typedef boost::graph_traits<SCGraph>::edge_descriptor SCEdge;

// =============================================================================
// 状态可达图
//
// StateClassReachabilityGraph 在新的 StateClass 结构（clocks/active/suspended）
// 基础上构建可达性图.核心算法:
//
//   1. advance_time()       - 时间推进（仅 active 时钟）
//   2. fire_with_time()     - 变迁激发（带时间）
//   3. recompute_enabled_sets() - 重新计算使能/活跃/挂起集合
//
// 规范化模式（CanonicalizationMode）控制状态合并策略:
//   EQUALITY       - 标识、时钟完全相等才合并
//   MAX_LOWER_BOUND - 取最大下界
//   INTERSECTION   - 取约束交集
// =============================================================================

#ifdef PTPN_ENABLE_TEST_ACCESS
struct StateClassReachabilityGraphTestAccess;
#endif

struct StateExpansionResult {
  std::vector<SuccessorCandidate> candidates;
  size_t enabled_transitions_count = 0;
  size_t pruned_states_count = 0;
  size_t transition_enabled_checks = 0;
  size_t chosen_count = 0;
  size_t fired_count = 0;
};

class StateClassReachabilityGraph {
#ifdef PTPN_ENABLE_TEST_ACCESS
  friend struct StateClassReachabilityGraphTestAccess;
#endif

 public:
  explicit StateClassReachabilityGraph(const petri::PTPN& ptpn);

  /**
   * 设置规范化模式.
   *
   * @param mode EQUALITY / MAX_LOWER_BOUND / INTERSECTION
   */
  void set_canonicalization_mode(CanonicalizationMode mode);

  /**
   * 获取当前规范化模式.
   */
  [[nodiscard]] CanonicalizationMode get_canonicalization_mode() const;


  void set_pruning_enabled(bool enabled);

  [[nodiscard]] bool is_pruning_enabled() const { return pruning_enabled_; }


  size_t build(size_t max_states = std::numeric_limits<size_t>::max());

  /**
   * 构建可达性图.
   *
   * @param max_states 最大状态数限制
   * @param thread_count 线程数（0 = 自动）
   * @return 实际构建的状态数
   */
  size_t build(size_t max_states, size_t thread_count);


  [[nodiscard]] const SCGraph& get_graph() const { return graph_; }
  [[nodiscard]] SCGraph& get_graph() { return graph_; }


  [[nodiscard]] SCVertex get_initial_vertex() const { return initial_vertex_; }
  [[nodiscard]] StateClass create_initial_state();


  /**
   * advance_time - 时间推进
   *
   * 找到 active 集合中最紧的时间上界（min_ub）,推进所有 active 时钟.
   * suspended 时钟保持冻结.
   *
   * @param state 当前状态（就地修改）
   * @return 推进的时间量（秒）,0 表示死锁（无 active 变迁）
   *
   * 算法:
   *   1. min_ub = min(clocks[t].upper_bound) for t in active
   *   2. if min_ub == INF or min_ub <= 0: return 0
   *   3. for t in active: clocks[t].lower_bound += min_ub; clocks[t].upper_bound += min_ub
   *   4. cumulative_time += min_ub
   *   5. return min_ub
   */
  double advance_time(StateClass& state) const;

  /**
   * fire_with_time - 带时间的变迁激发
   *
   * 检查时钟是否在有效时间窗口内,计算触发时间,生成新状态.
   *
   * @param t 变迁索引
   * @param from 起始状态
   * @return {是否成功, 新状态, 激发时间}
   */
  std::tuple<bool, StateClass, double> fire_with_time(
      size_t t, const StateClass& from) const;

  /**
   * recompute_enabled_sets - 重新计算使能/活跃/挂起集合
   *
   * 根据当前 marking,从 Petri 网重新计算使能变迁,
   * 然后通过调度算法确定 active 和 suspended 集合.
   *
   * @param state 当前状态（就地修改 marking 域）
   */
  void recompute_enabled_sets(StateClass& state) const;

  /**
   * recompute_enabled_sets_from_marking - 从给定 marking 计算
   *
   * @param marking Petri 网标识
   * @param state 目标状态（会修改 marking, enabled, active, suspended）
   */
  void recompute_enabled_sets_from_marking(const std::vector<int>& marking,
                                           StateClass& state) const;


  /**
   * select_active_per_core - 每个核心上最高优先级变迁集合（可并列）
   */
  std::set<size_t> select_active_per_core(
      const std::set<size_t>& enabled) const;

  /**
   * compute_firing_time - 变迁在当前状态下的最早可发生时间;不可发生返回 -1
   */
  [[nodiscard]] int compute_firing_time(const StateClass& state,
                                        size_t transition) const;

  /**
   * compute_suspended - 计算应该挂起的变迁集合
   *
   * @param enabled 使能变迁集合
   * @param active 活跃变迁集合
   * @return 应该挂起的变迁集合
   */
  std::set<size_t> compute_suspended(const std::set<size_t>& enabled,
                                     const std::set<size_t>& active) const;


  /**
   * canonicalize - 规范化两个状态
   *
   * @param a 状态 A
   * @param b 状态 B
   * @return 规范化后的状态
   */
  StateClass canonicalize(const StateClass& a, const StateClass& b) const;

  /**
   * are_equivalent - 检查两个状态是否在当前模式下等价
   */
  bool are_equivalent(const StateClass& a, const StateClass& b) const;


  /**
   * suspend_transition - 挂起变迁
   *
   * 将变迁 t 从 active 移到 suspended,时钟状态设为 SUSPENDED（冻结）.
   *
   * @param t 变迁索引
   * @param state 目标状态
   */
  void suspend_transition(size_t t, StateClass& state) const;

  /**
   * restore_transition - 恢复变迁
   *
   * 将变迁 t 从 suspended 移回 active,时钟状态设为 ACTIVE（继续流逝）.
   *
   * @param t 变迁索引
   * @param state 目标状态
   */
  void restore_transition(size_t t, StateClass& state) const;


  struct Statistics {
    size_t total_states = 0;
    size_t total_transitions = 0;
    size_t enabled_transitions_count = 0;
    size_t pruned_states_count = 0;
    size_t dedup_hits_count = 0;
    size_t dedup_misses_count = 0;
    size_t transition_enabled_checks = 0;
    bool truncated = false;
  };

  [[nodiscard]] const Statistics& get_statistics() const { return stats_; }


  bool save_to_dot(const std::string& file_path) const;
  bool save_to_json(const std::string& file_path) const;


 private:
  const petri::PTPN& ptpn_;
  SCGraph graph_;
  SCVertex initial_vertex_;
  Statistics stats_;

  CanonicalizationMode canonicalization_mode_ = CanonicalizationMode::EQUALITY;

  [[nodiscard]] bool is_transition_enabled(const StateClass& state,
                                          size_t trans_idx) const;

  std::pair<int, int> get_transition_time_bounds(const StateClass& state,
                                                 size_t trans_idx) const;

  std::vector<size_t> select_per_core(const std::set<size_t>& enabled) const;
  void apply_preemption(const std::vector<size_t>& chosen,
                       StateClass& state) const;

  std::set<size_t> compute_effective_enabled(
      const std::vector<size_t>& raw_enabled) const;
  std::set<size_t> compute_suspended_transitions(
      const std::vector<size_t>& raw_enabled,
      const std::set<size_t>& effective_enabled) const;

  bool maximal_time_elapse(StateClass& state, double& dt) const;

  std::tuple<bool, StateClass, double> fire_with_dbm(
      size_t trans_idx, const StateClass& from_state);

  void compute_enabled_and_clocks(StateClass& state);

  bool is_suspended(size_t trans_idx, const std::vector<size_t>& enabled) const;

  void recompute_suspension(StateClass& state) const;
  std::vector<size_t> collect_enabled_transitions(
      const StateClass& state) const;

  SCVertex find_or_add_vertex(const StateClass& state);
  StateExpansionResult expand_state_candidates(const StateClass& cur);

  static std::string format_marking(const std::vector<int>& marking);
  std::string format_transitions(const std::set<size_t>& trans_indices,
                                 bool detailed = true) const;

  std::string format_places(const std::vector<int>& marking) const;

  void log_state_class_details(const StateClass& state,
                              const std::string& prefix = "") const;

  size_t next_state_id_ = 0;
  bool pruning_enabled_ = false;

  std::unordered_map<StateKey, SCVertex, StateKeyHash> state_to_vertex_;
};

}  // namespace state_class

#endif  // ANALYSIS_GRAPH_H