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

}  // namespace scheduling