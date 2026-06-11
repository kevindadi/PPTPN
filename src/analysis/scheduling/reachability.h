#ifndef ANALYSIS_SCHEDULING_REACHABILITY_H
#define ANALYSIS_SCHEDULING_REACHABILITY_H

#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>

#include "../../petri/petri.h"
#include "state.h"
#include "scheduler.h"

namespace scheduling {

/**
 * ReachabilityGraph - 可达性图构建器
 *
 * 核心算法：
 * 1. 计算使能集合 E
 * 2. 计算活跃集合 X（每核最高优先级）
 * 3. 计算挂起集合 R
 * 4. 时间推进到最早可发生时间
 * 5. 选择变迁发生
 * 6. 生成后继状态
 */
class ReachabilityGraph {
 public:
  using Vertex = size_t;
  using Edge = std::pair<Vertex, Vertex>;

  explicit ReachabilityGraph(const petri::PTPN& ptpn);

  /**
   * 构建可达性图
   * @param max_states 最大状态数
   * @return 实际构建的状态数
   */
  size_t build(size_t max_states = std::numeric_limits<size_t>::max());

  // 查询
  [[nodiscard]] const StateClass& get_state(Vertex v) const;
  [[nodiscard]] const StateClass& get_initial_state() const;
  [[nodiscard]] size_t num_states() const { return states_.size(); }
  [[nodiscard]] size_t num_edges() const { return edges_.size(); }

  // 输出
  bool save_to_dot(const std::string& path) const;
  bool save_to_json(const std::string& path) const;

  // 统计
  struct Statistics {
    size_t total_states = 0;
    size_t total_edges = 0;
    size_t dedup_hits = 0;
    size_t dedup_misses = 0;
    bool truncated = false;
  };
  [[nodiscard]] const Statistics& get_statistics() const { return stats_; }

 private:
  const petri::PTPN& ptpn_;

  // 图结构
  std::vector<StateClass> states_;
  std::vector<Edge> edges_;

  // 状态索引
  std::unordered_map<StateKey, Vertex, StateKeyHash> state_to_vertex_;

  // 状态管理
  Vertex find_or_add_state(const StateClass& state);
  [[nodiscard]] StateKey make_key(const StateClass& state) const;

  // 展开
  [[nodiscard]] std::vector<StateClass> expand(const StateClass& state);
  [[nodiscard]] std::optional<StateClass> fire_transition(
      const StateClass& state, size_t transition_id, int firing_time);

  // 使能计算
  [[nodiscard]] std::set<size_t> compute_enabled(
      const std::vector<int>& marking) const;
  void recompute_sets(StateClass& state);

  //辅助
  [[nodiscard]] int compute_firing_time(const StateClass& state, size_t t) const;
  void advance_time(StateClass& state, int delta) const;

  size_t next_state_id_ = 0;
  Statistics stats_;
};

}  // namespace scheduling

#endif  // ANALYSIS_SCHEDULING_REACHABILITY_H