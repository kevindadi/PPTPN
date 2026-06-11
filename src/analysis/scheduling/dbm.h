#ifndef ANALYSIS_SCHEDULING_DBM_H
#define ANALYSIS_SCHEDULING_DBM_H

#include <set>
#include <string>
#include <sstream>
#include <vector>
#include <limits>
#include <stdexcept>
#include <algorithm>

namespace scheduling {

constexpr int INF_TIME = std::numeric_limits<int>::max();

class DBM {
 public:
  // 构造：size=1 表示只有参考时钟 c₀
  explicit DBM(size_t size = 1);

  DBM(const DBM& other);
  DBM& operator=(const DBM& other);
  DBM(DBM&& other) noexcept = default;
  DBM& operator=(DBM&& other) noexcept = default;

  // 大小查询
  [[nodiscard]] size_t size() const { return clock_count_; }

  // 时钟管理
  size_t add_clock();                      // 添加新时钟，返回索引
  void reset_clock(size_t clock_idx);       // 重置时钟到 0
  void freeze_clock(size_t clock_idx);      // 冻结时钟
  void unfreeze_clock(size_t clock_idx);   // 解冻时钟
  [[nodiscard]] bool is_frozen(size_t clock_idx) const;
  [[nodiscard]] const std::set<size_t>& frozen_clocks() const { return frozen_clocks_; }

  // 约束操作：设置 c_i - c_j ≤ bound
  void set_constraint(size_t i, size_t j, int bound);
  [[nodiscard]] int get_constraint(size_t i, size_t j) const;

  // 最小化：Floyd-Warshall 算法
  void minimize();

  // 时间推进：对所有非冻结时钟 c_i ≤ c_i + delta
  void elapse_time(int delta);

  // 区域操作
  [[nodiscard]] DBM intersection(const DBM& other) const;
  [[nodiscard]] bool contains(const DBM& other) const;
  [[nodiscard]] bool is_empty() const;  // c_i - c_i < 0 则为空

  // 查询
  [[nodiscard]] int get_lower_bound(size_t clock_idx) const;  // -c_0i
  [[nodiscard]] int get_upper_bound(size_t clock_idx) const;   // c_i0

  // 标识
  bool operator==(const DBM& other) const;
  bool operator<(const DBM& other) const;
  [[nodiscard]] std::string to_string() const;
  [[nodiscard]] const std::vector<int>& raw_matrix() const { return matrix_; }

 private:
  [[nodiscard]] size_t offset(size_t i, size_t j) const { return i * clock_count_ + j; }
  void check_index(size_t i, size_t j) const;
  void initialize_clock(size_t clock_idx);
  void resize(size_t new_size);

  std::vector<int> matrix_; // (size × size) DBM 矩阵
  size_t clock_count_;                // 时钟数量（包括 c₀）
  std::set<size_t> frozen_clocks_;    // 冻结时钟集合
};

}  // namespace scheduling

#endif // ANALYSIS_SCHEDULING_DBM_H