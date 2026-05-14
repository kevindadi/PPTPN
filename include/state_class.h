#ifndef STATE_CLASS_H
#define STATE_CLASS_H

#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace state_class {

// 无穷大时间常量
constexpr int INF_TIME = std::numeric_limits<int>::max();
constexpr double INF_DOUBLE = std::numeric_limits<double>::infinity();

/**
 * DBM (Difference Bound Matrix) 类
 * 用于表示时间约束区间 Z
 * DBM[i][j] 表示约束 x_i - x_j <= bound
 * 其中 x_0 表示参考时钟(通常为 0)
 */
class DBM {
 public:
  explicit DBM(size_t size = 0);

  // 复制构造和赋值
  DBM(const DBM& other);
  DBM& operator=(const DBM& other);
  DBM(DBM&& other) noexcept = default;
  DBM& operator=(DBM&& other) noexcept = default;

  /**
   * 获取矩阵大小(时钟数量)
   */
  [[nodiscard]] size_t size() const { return matrix_.size(); }

  /**
   * 设置约束:x_i - x_j <= bound
   * @param i 时钟 i 的索引
   * @param j 时钟 j 的索引
   * @param bound 上界值
   */
  void set_constraint(size_t i, size_t j, int bound);

  /**
   * 获取约束值:x_i - x_j <= ?
   * @return 约束值,如果为 INF_TIME 表示无上界
   */
  [[nodiscard]] int get_constraint(size_t i, size_t j) const;

  /**
   * 检查约束是否一致(是否存在负环)
   * @return true 如果约束一致,false 如果存在矛盾
   */
  [[nodiscard]] bool is_consistent() const;

  /**
   * 使用 Floyd-Warshall 算法最小化 DBM
   * 计算所有时钟对之间的最短路径
   */
  void minimize();

  /**
   * 添加新时钟并初始化约束
   * @return 新时钟的索引
   */
  size_t add_clock();

  /**
   * 扩展 DBM 以包含新的时钟数量
   * @param new_size 新的时钟数量
   */
  void resize(size_t new_size);

  /**
   * 时间推进:所有非冻结时钟同步增加 delta
   * @param delta 时间增量
   */
  void elapse_time(int delta);

  /**
   * 重置时钟:将指定时钟重置为 0
   * @param clock_idx 要重置的时钟索引
   */
  void reset_clock(size_t clock_idx);

  /**
   * 移除时钟(当变迁不再使能时)
   * @param clock_idx 要移除的时钟索引
   */
  void remove_clock(size_t clock_idx);

  /**
   * 限制DBM以反映变迁的触发时间窗口 [α, β]
   * @param transition_id 变迁ID
   * @param alpha 最早触发时间
   * @param beta 最晚触发时间(可为 INF_TIME)
   * @return 新的DBM,表示限制后的约束
   */
  [[nodiscard]] DBM restrict_for_firing(size_t transition_id, int alpha,
                                        int beta) const;

  /**
   * 冻结时钟:标记时钟为冻结状态(挂起变迁的时钟)
   * @param clock_idx 要冻结的时钟索引
   */
  void freeze_clock(size_t clock_idx);

  /**
   * 解冻时钟:移除冻结状态
   * @param clock_idx 要解冻的时钟索引
   */
  void unfreeze_clock(size_t clock_idx);

  /**
   * 检查时钟是否被冻结
   * @param clock_idx 时钟索引
   * @return true 如果时钟被冻结
   */
  [[nodiscard]] bool is_frozen(size_t clock_idx) const;

  /**
   * 复制时钟约束到另一个DBM
   * @param clock_idx 要复制的时钟索引
   * @param target 目标DBM
   */
  void copy_clock_constraints(size_t clock_idx, DBM& target) const;

  /**
   * 交集操作:计算两个 DBM 的交集
   * @param other 另一个 DBM
   * @return 新的 DBM,表示交集
   */
  [[nodiscard]] DBM intersection(const DBM& other) const;

  /**
   * 检查是否为空的约束集合(无解)
   * @return true 如果为空,false 如果有解
   */
  [[nodiscard]] bool is_empty() const;

  /**
   * 剪枝操作:移除冗余约束
   */
  void prune();

  /**
   * 检查是否包含另一个 DBM(子集关系)
   * @param other 另一个 DBM
   * @return true 如果 this 包含 other
   */
  [[nodiscard]] bool contains(const DBM& other) const;

  /**
   * 转换为字符串表示
   */
  [[nodiscard]] std::string to_string() const;

  /**
   * 比较操作符(用于容器中的查找)
   */
  bool operator==(const DBM& other) const;
  bool operator<(const DBM& other) const;

 private:
  std::vector<std::vector<int>> matrix_;  // DBM 矩阵
  size_t clock_count_;                    // 时钟数量
  std::set<size_t> frozen_clocks_;        // 冻结时钟集合(挂起变迁的时钟)

  /**
   * 检查索引是否有效
   */
  void check_index(size_t i, size_t j) const;

  /**
   * 初始化新时钟的约束
   */
  void initialize_clock(size_t clock_idx);
};

/**
 * 状态类结构
 * 表示 P-PTPN 中的一个状态类
 */
struct StateClass {
  std::vector<int> marking;  // 当前标识 M
  DBM Z1;                    // 不可挂起变迁的时间约束区间
  DBM Z2;                    // 可挂起变迁的时间约束区间

  size_t state_id;             // 状态唯一 ID
  double cumulative_time;      // 累积时间
  std::set<size_t> enabled;    // 当前使能的变迁集合
  std::set<size_t> suspended;  // 当前被挂起的变迁集合

  StateClass() : state_id(0), cumulative_time(0.0) {}

  explicit StateClass(const std::vector<int>& m)
      : marking(m), state_id(0), cumulative_time(0.0) {}

  StateClass(const std::vector<int>& m, const DBM& z1, const DBM& z2)
      : marking(m), Z1(z1), Z2(z2), state_id(0), cumulative_time(0.0) {}

  StateClass(const StateClass& other) = default;
  StateClass& operator=(const StateClass& other) = default;

  bool operator==(const StateClass& other) const;
  bool operator<(const StateClass& other) const;

  [[nodiscard]] StateClass copy() const;
  [[nodiscard]] std::string to_string() const;
};

struct TransitionEdge {
  int transition_id;   // 变迁 ID
  double firing_time;  // 变迁触发时间

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

}  // namespace state_class

#endif  // STATE_CLASS_H
