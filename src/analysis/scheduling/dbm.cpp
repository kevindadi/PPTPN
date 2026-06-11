#include "dbm.h"

#include <limits>

namespace scheduling {

DBM::DBM(size_t size) : clock_count_(size), matrix_(size * size, INF_TIME) {
  if (size > 0) {
    for (size_t i = 0; i < size; ++i) {
      matrix_[offset(i, i)] = 0;  // c_i - c_i ≤ 0
    }
    if (size > 1) {
      for (size_t i = 1; i < size; ++i) {
        matrix_[offset(i, 0)] = INF_TIME;  // c_i - c_0 ≤ ∞（无下界）
        matrix_[offset(0, i)] = 0;         // c_0 - c_i ≤ 0（c_i ≥ c_0）
      }
    }
  }
}

DBM::DBM(const DBM& other)
    : matrix_(other.matrix_),
      clock_count_(other.clock_count_),
      frozen_clocks_(other.frozen_clocks_) {}

DBM& DBM::operator=(const DBM& other) {
  if (this != &other) {
    matrix_ = other.matrix_;
    clock_count_ = other.clock_count_;
    frozen_clocks_ = other.frozen_clocks_;
  }
  return *this;
}

size_t DBM::offset(size_t i, size_t j) const {
  return i * clock_count_ + j;
}

void DBM::check_index(size_t i, size_t j) const {
  if (i >= clock_count_ || j >= clock_count_) {
    throw std::out_of_range("DBM index out of range");
  }
}

void DBM::set_constraint(size_t i, size_t j, int bound) {
  check_index(i, j);
  matrix_[offset(i, j)] = bound;
}

int DBM::get_constraint(size_t i, size_t j) const {
  check_index(i, j);
  return matrix_[offset(i, j)];
}

bool DBM::is_frozen(size_t clock_idx) const {
  return frozen_clocks_.find(clock_idx) != frozen_clocks_.end();
}

void DBM::freeze_clock(size_t clock_idx) {
  if (clock_idx < clock_count_ && clock_idx != 0) {
    frozen_clocks_.insert(clock_idx);
  }
}

void DBM::unfreeze_clock(size_t clock_idx) {
  frozen_clocks_.erase(clock_idx);
}

size_t DBM::add_clock() {
  size_t new_idx = clock_count_;
  resize(clock_count_ + 1);
  return new_idx;
}

int DBM::get_lower_bound(size_t clock_idx) const {
  if (clock_idx >= clock_count_) {
    return 0;
  }
  return -matrix_[offset(0, clock_idx)];
}

int DBM::get_upper_bound(size_t clock_idx) const {
  if (clock_idx >= clock_count_) {
    return INF_TIME;
  }
  return matrix_[offset(clock_idx, 0)];
}

std::string DBM::to_string() const {
  if (clock_count_ == 0) {
    return "DBM(empty)";
  }

  std::ostringstream oss;
  oss << "DBM(size=" << clock_count_ << "):\n ";
  for (size_t j = 0; j < clock_count_; ++j) {
    oss << "c" << j << "      ";
  }
  oss << "\n";

  for (size_t i = 0; i < clock_count_; ++i) {
    oss << "c" << i << " ";
    for (size_t j = 0; j < clock_count_; ++j) {
      int value = matrix_[offset(i, j)];
      if (value == INF_TIME) {
        oss << " inf ";
      } else {
        oss << value << " ";
      }
    }
    oss << "\n";
  }

  if (!frozen_clocks_.empty()) {
    oss << "Frozen: {";
    bool first = true;
    for (size_t idx : frozen_clocks_) {
      if (!first) oss << ", ";
      first = false;
      oss << "c" << idx;
    }
    oss << "}\n";
  }

  return oss.str();
}

bool DBM::operator==(const DBM& other) const {
  if (clock_count_ != other.clock_count_) {
    return false;
  }
  return frozen_clocks_ == other.frozen_clocks_ && matrix_ == other.matrix_;
}

bool DBM::operator<(const DBM& other) const {
  if (clock_count_ != other.clock_count_) {
    return clock_count_ < other.clock_count_;
  }
  if (frozen_clocks_ != other.frozen_clocks_) {
    return frozen_clocks_ < other.frozen_clocks_;
  }
  return matrix_ < other.matrix_;
}

DBM DBM::intersection(const DBM& other) const {
  if (clock_count_ != other.clock_count_) {
    throw std::invalid_argument("DBM sizes must match for intersection");
  }

  DBM result(clock_count_);

  for (size_t i = 0; i < clock_count_; ++i) {
    for (size_t j = 0; j < clock_count_; ++j) {
      int bound1 = matrix_[offset(i, j)];
      int bound2 = other.matrix_[other.offset(i, j)];

      if (bound1 == INF_TIME) {
        result.matrix_[result.offset(i, j)] = bound2;
      } else if (bound2 == INF_TIME) {
        result.matrix_[result.offset(i, j)] = bound1;
      } else {
        result.matrix_[result.offset(i, j)] = std::min(bound1, bound2);
      }
    }
  }

  result.minimize();
  return result;
}

bool DBM::is_empty() const {
  if (clock_count_ == 0) return true;

  DBM copy(*this);
  copy.minimize();

  // 检查对角线：c_i - c_i < 0 表示不一致
  for (size_t i = 0; i < clock_count_; ++i) {
    if (copy.matrix_[copy.offset(i, i)] < 0) {
      return true;
    }
  }
  return false;
}

bool DBM::contains(const DBM& other) const {
  if (clock_count_ != other.clock_count_) {
    return false;
  }

  for (size_t i = 0; i < clock_count_; ++i) {
    for (size_t j = 0; j < clock_count_; ++j) {
      int this_bound = matrix_[offset(i, j)];
      int other_bound = other.matrix_[other.offset(i, j)];

      if (other_bound != INF_TIME) {
        if (this_bound == INF_TIME || other_bound < this_bound) {
          return false;
        }
      }
    }
  }

  return frozen_clocks_ == other.frozen_clocks_;
}

void DBM::elapse_time(int delta) {
  if (delta <= 0 || clock_count_ == 0) return;

  bool changed = false;
  for (size_t i = 1; i < clock_count_; ++i) {
    if (is_frozen(i)) {
      continue;  // 冻结时钟不推进
    }

    // c_i - c_0 ≤ c_i - c_0 + delta
    int current_upper = matrix_[offset(i, 0)];
    if (current_upper != INF_TIME) {
      matrix_[offset(i, 0)] = current_upper + delta;
      changed = true;
    }

    // c_0 - c_i ≤ c_0 - c_i - delta（即 c_i ≥ c_0 + delta）
    int current_lower = matrix_[offset(0, i)];
    if (current_lower != INF_TIME) {
      matrix_[offset(0, i)] = current_lower - delta;
      changed = true;
    }
  }

  if (changed) {
    minimize();
  }
}

void DBM::reset_clock(size_t clock_idx) {
  check_index(clock_idx, clock_idx);

  if (clock_idx == 0) {
    return;  // 不能重置参考时钟
  }

  bool changed = false;

  // 重置 c_i - c_0 ≤ 0 和 c_0 - c_i ≤ 0
  if (matrix_[offset(0, clock_idx)] != 0) {
    matrix_[offset(0, clock_idx)] = 0;
    changed = true;
  }
  if (matrix_[offset(clock_idx, 0)] != INF_TIME) {
    matrix_[offset(clock_idx, 0)] = INF_TIME;
    changed = true;
  }

  // c_i - c_j ≤ c_0 - c_j（复制 c₀ 的约束）
  for (size_t k = 0; k < clock_count_; ++k) {
    const int row0 = matrix_[offset(0, k)];
    if (matrix_[offset(clock_idx, k)] != row0) {
      matrix_[offset(clock_idx, k)] = row0;
      changed = true;
    }

    const int col0 = matrix_[offset(k, 0)];
    if (matrix_[offset(k, clock_idx)] != col0) {
      matrix_[offset(k, clock_idx)] = col0;
      changed = true;
    }
  }

  matrix_[offset(clock_idx, clock_idx)] = 0;

  if (changed) {
    minimize();
  }
}

void DBM::initialize_clock(size_t clock_idx) {
  if (clock_idx >= clock_count_) return;

  matrix_[offset(clock_idx, clock_idx)] = 0;

  if (clock_idx == 0) {
    // c₀初始化：c₀ - c_i ≤ 0，c_i - c₀ ≤ ∞
    for (size_t i = 1; i < clock_count_; ++i) {
      matrix_[offset(0, i)] = 0;
      matrix_[offset(i, 0)] = INF_TIME;
    }
  } else {
    // 普通时钟：c_i - c₀ ≤ ∞，c₀ - c_i ≤ 0
    matrix_[offset(clock_idx, 0)] = INF_TIME;
    matrix_[offset(0, clock_idx)] = 0;

    for (size_t i = 1; i < clock_count_; ++i) {
      if (i != clock_idx) {
        matrix_[offset(clock_idx, i)] = INF_TIME;
        matrix_[offset(i, clock_idx)] = INF_TIME;
      }
    }
  }
}

void DBM::resize(size_t new_size) {
  if (new_size == clock_count_) return;

  const size_t old_size = clock_count_;
  const std::vector<int> old_matrix = matrix_;

  clock_count_ = new_size;
  matrix_.assign(new_size * new_size, INF_TIME);

  for (size_t i = 0; i < new_size; ++i) {
    matrix_[offset(i, i)] = 0;
  }
  if (new_size > 1) {
    for (size_t i = 1; i < new_size; ++i) {
      matrix_[offset(i, 0)] = INF_TIME;
      matrix_[offset(0, i)] = 0;
    }
  }

  const size_t preserved = std::min(old_size, new_size);
  for (size_t i = 0; i < preserved; ++i) {
    for (size_t j = 0; j < preserved; ++j) {
      matrix_[offset(i, j)] = old_matrix[i * old_size + j];
    }
  }

  for (size_t i = old_size; i < new_size; ++i) {
    initialize_clock(i);
  }

  // 更新冻结时钟集合（移除超出范围的索引）
  std::set<size_t> new_frozen;
  for (size_t idx : frozen_clocks_) {
    if (idx < new_size) {
      new_frozen.insert(idx);
    }
  }
  frozen_clocks_ = std::move(new_frozen);
}

void DBM::minimize() {
  if (clock_count_ == 0) return;

  // Floyd-Warshall 算法
  for (size_t k = 0; k < clock_count_; ++k) {
    for (size_t i = 0; i < clock_count_; ++i) {
      if (matrix_[offset(i, k)] == INF_TIME) continue;
      for (size_t j = 0; j < clock_count_; ++j) {
        if (matrix_[offset(k, j)] == INF_TIME) continue;

        int new_bound = matrix_[offset(i, k)] + matrix_[offset(k, j)];
        if (matrix_[offset(i, j)] == INF_TIME || new_bound < matrix_[offset(i, j)]) {
          matrix_[offset(i, j)] = new_bound;
        }
      }
    }
  }
}

}  // namespace scheduling