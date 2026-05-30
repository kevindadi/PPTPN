#ifndef ANALYSIS_PTPN_ANALYSIS_H
#define ANALYSIS_PTPN_ANALYSIS_H

/**
 * @file ptpn_analysis.h
 * @brief 公开的 PTPN 可达性分析接口
 *
 * 本文件提供对 PTPN（Priority Time Petri Net）可达性分析的高层封装.
 * 用户通过此类接口进行状态空间构建和分析.
 *
 * 核心设计:
 * - 使用 StateClass（clocks/active/suspended）替代旧的 Z1/Z2 结构
 * - 支持三种规范化模式:EQUALITY / MAX_LOWER_BOUND / INTERSECTION
 * - 时间推进仅作用于 active 时钟,suspended 时钟自动冻结
 * - 抢占/恢复语义显式管理
 *
 * 使用示例:
 * @code
 *   PTPNAnalyzer analyzer(ptpn);
 *   analyzer.set_canonicalization_mode(CanonicalizationMode::MAX_LOWER_BOUND);
 *   size_t state_count = analyzer.build(max_states);
 *   analyzer.save_to_dot("output.dot");
 * @endcode
 */

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "petri/petri.h"
#include "clock_state.h"
#include "canonicalization.h"
#include "scheduling.h"
#include "state.h"
#include "graph.h"

namespace state_class {
  
/**
 * PTPNAnalyzer - PTPN 可达性分析器
 *
 * 封装 StateClassReachabilityGraph,提供高层分析和持久化接口.
 * 所有底层可达性算法由 StateClassReachabilityGraph 实现.
 */
class PTPNAnalyzer {
 public:

  /**
   * 构造分析器.
   *
   * @param ptpn PTPN 网（拷贝构造）
   */
  explicit PTPNAnalyzer(const petri::PTPN& ptpn);

  /**
   * 构造分析器（移动语义）.
   *
   * @param ptpn PTPN 网（移动构造）
   */
  explicit PTPNAnalyzer(petri::PTPN&& ptpn) noexcept;

  ~PTPNAnalyzer();

  PTPNAnalyzer(const PTPNAnalyzer& other);
  PTPNAnalyzer& operator=(const PTPNAnalyzer& other);
  PTPNAnalyzer(PTPNAnalyzer&& other) noexcept;
  PTPNAnalyzer& operator=(PTPNAnalyzer&& other) noexcept;

  /**
   * 设置规范化模式.
   *
   * @param mode EQUALITY（默认）/ MAX_LOWER_BOUND / INTERSECTION
   */
  void set_canonicalization_mode(CanonicalizationMode mode);

  /**
   * 获取当前规范化模式.
   */
  [[nodiscard]] CanonicalizationMode get_canonicalization_mode() const;

  /**
   * 启用/禁用状态剪枝（默认禁用）.
   */
  void set_pruning_enabled(bool enabled);

  /**
   * 查询剪枝是否启用.
   */
  [[nodiscard]] bool is_pruning_enabled() const;

  /**
   * build - 构建可达性图（串行）
   *
   * @param max_states 最大状态数（默认无限制）
   * @return 实际构建的状态数
   */
  size_t build(
      size_t max_states = std::numeric_limits<size_t>::max());

  /**
   * build - 构建可达性图（并行）
   *
   * @param max_states 最大状态数
   * @param thread_count 线程数（0 = 自动）
   * @return 实际构建的状态数
   */
  size_t build(size_t max_states, size_t thread_count);

  /**
   * 获取底层可达性图（Boost.Graph）.
   */
  [[nodiscard]] const SCGraph& get_graph() const;

  /**
   * 获取初始状态顶点.
   */
  [[nodiscard]] SCVertex get_initial_vertex() const;

  /**
   * 获取状态数量.
   */
  [[nodiscard]] size_t state_count() const;

  /**
   * 获取迁移数量.
   */
  [[nodiscard]] size_t transition_count() const;

  /**
   * 获取统计信息.
   */
  [[nodiscard]] const typename StateClassReachabilityGraph::Statistics&
  get_statistics() const;

  /**
   * advance_time - 时间推进
   *
   * 推进所有 active 时钟.suspended 时钟保持冻结.
   *
   * @param state 当前状态（就地修改）
   * @return 推进的时间量
   */
  double advance_time(StateClass& state) const;

  /**
   * fire_with_time - 带时间的变迁激发
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
   * @param state 目标状态
   */
  void recompute_enabled_sets(StateClass& state) const;

  /**
   * recompute_enabled_sets_from_marking - 从给定 marking 计算
   */
  void recompute_enabled_sets_from_marking(
      const std::vector<int>& marking, StateClass& state) const;

  /**
   * select_active_per_core - 选择每个核心上最高优先级变迁
   */
  std::set<size_t> select_active_per_core(
      const std::set<size_t>& enabled) const;

  /**
   * compute_suspended - 计算应该挂起的变迁集合
   */
  std::set<size_t> compute_suspended(const std::set<size_t>& enabled,
                                     const std::set<size_t>& active) const;

  /**
   * suspend_transition - 挂起变迁
   */
  void suspend_transition(size_t t, StateClass& state) const;

  /**
   * restore_transition - 恢复变迁
   */
  void restore_transition(size_t t, StateClass& state) const;

  /**
   * canonicalize - 规范化两个状态
   */
  StateClass canonicalize(const StateClass& a, const StateClass& b) const;

  /**
   * are_equivalent - 检查两个状态是否等价
   */
  bool are_equivalent(const StateClass& a, const StateClass& b) const;

  /**
   * save_to_dot - 保存为 Graphviz DOT 格式
   *
   * @param file_path 输出文件路径
   * @return 是否成功
   */
  bool save_to_dot(const std::string& file_path) const;

  /**
   * save_to_json - 保存为 JSON 格式
   *
   * @param file_path 输出文件路径
   * @return 是否成功
   */
  bool save_to_json(const std::string& file_path) const;

  /**
   * create_initial_state - 创建初始状态
   *
   * 从 PTPN 的初始标识创建 StateClass.
   */
  [[nodiscard]] StateClass create_initial_state() const;

  /**
   * num_transitions - 变迁数量
   */
  [[nodiscard]] size_t num_transitions() const;

  /**
   * num_places - 库所数量
   */
  [[nodiscard]] size_t num_places() const;

  /**
   * get_ptpn - 获取底层 PTPN 网（const 引用）
   */
  [[nodiscard]] const petri::PTPN& get_ptpn() const;

 private:
  std::unique_ptr<petri::PTPN> ptpn_;
  std::unique_ptr<StateClassReachabilityGraph> graph_;
};

inline PTPNAnalyzer::PTPNAnalyzer(const petri::PTPN& ptpn)
    : ptpn_(std::make_unique<petri::PTPN>(ptpn)),
      graph_(std::make_unique<StateClassReachabilityGraph>(*ptpn_)) {}

inline PTPNAnalyzer::PTPNAnalyzer(petri::PTPN&& ptpn) noexcept
    : ptpn_(std::make_unique<petri::PTPN>(std::move(ptpn))),
      graph_(std::make_unique<StateClassReachabilityGraph>(*ptpn_)) {}

inline PTPNAnalyzer::~PTPNAnalyzer() = default;

inline PTPNAnalyzer::PTPNAnalyzer(const PTPNAnalyzer& other)
    : ptpn_(std::make_unique<petri::PTPN>(*other.ptpn_)),
      graph_(std::make_unique<StateClassReachabilityGraph>(*other.graph_)) {}

inline PTPNAnalyzer& PTPNAnalyzer::operator=(const PTPNAnalyzer& other) {
  if (this != &other) {
    ptpn_ = std::make_unique<petri::PTPN>(*other.ptpn_);
    graph_ = std::make_unique<StateClassReachabilityGraph>(*other.graph_);
  }
  return *this;
}

inline PTPNAnalyzer::PTPNAnalyzer(PTPNAnalyzer&& other) noexcept
    : ptpn_(std::move(other.ptpn_)),
      graph_(std::move(other.graph_)) {}

inline PTPNAnalyzer& PTPNAnalyzer::operator=(PTPNAnalyzer&& other) noexcept {
  if (this != &other) {
    ptpn_ = std::move(other.ptpn_);
    graph_ = std::move(other.graph_);
  }
  return *this;
}

inline void PTPNAnalyzer::set_canonicalization_mode(
    CanonicalizationMode mode) {
  graph_->set_canonicalization_mode(mode);
}

inline CanonicalizationMode PTPNAnalyzer::get_canonicalization_mode() const {
  return graph_->get_canonicalization_mode();
}

inline void PTPNAnalyzer::set_pruning_enabled(bool enabled) {
  graph_->set_pruning_enabled(enabled);
}

inline bool PTPNAnalyzer::is_pruning_enabled() const {
  return graph_->is_pruning_enabled();
}

inline size_t PTPNAnalyzer::build(
    size_t max_states) {
  return graph_->build(max_states);
}

inline size_t PTPNAnalyzer::build(size_t max_states, size_t thread_count) {
  return graph_->build(max_states, thread_count);
}

inline const SCGraph& PTPNAnalyzer::get_graph() const {
  return graph_->get_graph();
}

inline SCVertex PTPNAnalyzer::get_initial_vertex() const {
  return graph_->get_initial_vertex();
}

inline size_t PTPNAnalyzer::state_count() const {
  return graph_->get_statistics().total_states;
}

inline size_t PTPNAnalyzer::transition_count() const {
  return graph_->get_statistics().total_transitions;
}

inline const typename StateClassReachabilityGraph::Statistics&
PTPNAnalyzer::get_statistics() const {
  return graph_->get_statistics();
}

inline double PTPNAnalyzer::advance_time(StateClass& state) const {
  return graph_->advance_time(state);
}

inline std::tuple<bool, StateClass, double>
PTPNAnalyzer::fire_with_time(size_t t, const StateClass& from) const {
  return graph_->fire_with_time(t, from);
}

inline void PTPNAnalyzer::recompute_enabled_sets(
    StateClass& state) const {
  graph_->recompute_enabled_sets(state);
}

inline void PTPNAnalyzer::recompute_enabled_sets_from_marking(
    const std::vector<int>& marking, StateClass& state) const {
  graph_->recompute_enabled_sets_from_marking(marking, state);
}

inline std::set<size_t> PTPNAnalyzer::select_active_per_core(
    const std::set<size_t>& enabled) const {
  return graph_->select_active_per_core(enabled);
}

inline std::set<size_t> PTPNAnalyzer::compute_suspended(
    const std::set<size_t>& enabled,
    const std::set<size_t>& active) const {
  return graph_->compute_suspended(enabled, active);
}

inline void PTPNAnalyzer::suspend_transition(
    size_t t, StateClass& state) const {
  graph_->suspend_transition(t, state);
}

inline void PTPNAnalyzer::restore_transition(
    size_t t, StateClass& state) const {
  graph_->restore_transition(t, state);
}

inline StateClass PTPNAnalyzer::canonicalize(
    const StateClass& a, const StateClass& b) const {
  return graph_->canonicalize(a, b);
}

inline bool PTPNAnalyzer::are_equivalent(
    const StateClass& a, const StateClass& b) const {
  return graph_->are_equivalent(a, b);
}

inline bool PTPNAnalyzer::save_to_dot(
    const std::string& file_path) const {
  return graph_->save_to_dot(file_path);
}

inline bool PTPNAnalyzer::save_to_json(
    const std::string& file_path) const {
  return graph_->save_to_json(file_path);
}

inline StateClass PTPNAnalyzer::create_initial_state() const {
  return graph_->create_initial_state();
}

inline size_t PTPNAnalyzer::num_transitions() const {
  return ptpn_->num_transitions();
}

inline size_t PTPNAnalyzer::num_places() const {
  return ptpn_->num_places();
}

inline const petri::PTPN& PTPNAnalyzer::get_ptpn() const {
  return *ptpn_;
}

}  // namespace state_class

#endif  // ANALYSIS_PTPN_ANALYSIS_H