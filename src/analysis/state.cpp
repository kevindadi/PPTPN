#include "analysis/state.h"

#include <algorithm>
#include <iomanip>
#include <stdexcept>

namespace state_class {

namespace {
DBMInstrumentation g_dbm_instrumentation;
}

void reset_dbm_instrumentation() { g_dbm_instrumentation = {}; }

DBMInstrumentation get_dbm_instrumentation() { return g_dbm_instrumentation; }

DBM::DBM(size_t size) : clock_count_(size) {
  if (size > 0) {
    matrix_.resize(size, std::vector<int>(size, INF_TIME));
    for (size_t i = 0; i < size; ++i) {
      matrix_[i][i] = 0;
    }
    if (size > 1) {
      for (size_t i = 1; i < size; ++i) {
        matrix_[i][0] = INF_TIME;
        matrix_[0][i] = 0;
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

void DBM::check_index(size_t i, size_t j) const {
  if (i >= clock_count_ || j >= clock_count_) {
    throw std::out_of_range("DBM index out of range");
  }
}

void DBM::set_constraint(size_t i, size_t j, int bound) {
  check_index(i, j);
  matrix_[i][j] = bound;
}

int DBM::get_constraint(size_t i, size_t j) const {
  check_index(i, j);
  return matrix_[i][j];
}

bool DBM::is_consistent() const {
  if (clock_count_ == 0) return true;

  for (size_t i = 0; i < clock_count_; ++i) {
    if (matrix_[i][i] < 0) {
      return false;
    }
  }

  DBM temp = *this;
  temp.minimize();

  for (size_t i = 0; i < clock_count_; ++i) {
    if (temp.matrix_[i][i] < 0) {
      return false;
    }
  }

  return true;
}

void DBM::minimize() {
  ++g_dbm_instrumentation.minimize_calls;

  if (clock_count_ == 0) return;

  for (size_t k = 0; k < clock_count_; ++k) {
    for (size_t i = 0; i < clock_count_; ++i) {
      if (matrix_[i][k] == INF_TIME) continue;

      for (size_t j = 0; j < clock_count_; ++j) {
        if (matrix_[k][j] == INF_TIME) continue;

        int new_bound = matrix_[i][k] + matrix_[k][j];
        if (matrix_[i][j] == INF_TIME || new_bound < matrix_[i][j]) {
          matrix_[i][j] = new_bound;
        }
      }
    }
  }
}

size_t DBM::add_clock() {
  size_t new_idx = clock_count_;
  resize(clock_count_ + 1);
  return new_idx;
}

void DBM::resize(size_t new_size) {
  if (new_size == clock_count_) return;

  size_t old_size = clock_count_;
  clock_count_ = new_size;

  matrix_.resize(new_size);
  for (auto& row : matrix_) {
    row.resize(new_size, INF_TIME);
  }

  for (size_t i = old_size; i < new_size; ++i) {
    initialize_clock(i);
  }
}

void DBM::initialize_clock(size_t clock_idx) {
  if (clock_idx >= clock_count_) return;

  matrix_[clock_idx][clock_idx] = 0;

  if (clock_idx == 0) {
    for (size_t i = 1; i < clock_count_; ++i) {
      matrix_[0][i] = 0;
      matrix_[i][0] = INF_TIME;
    }
  } else {
    matrix_[clock_idx][0] = INF_TIME;
    matrix_[0][clock_idx] = 0;

    for (size_t i = 1; i < clock_count_; ++i) {
      if (i != clock_idx) {
        matrix_[clock_idx][i] = INF_TIME;
        matrix_[i][clock_idx] = INF_TIME;
      }
    }
  }
}

void DBM::elapse_time(int delta) {
  if (delta <= 0) return;

  for (size_t i = 1; i < clock_count_; ++i) {
    if (!is_frozen(i)) {
      int current_upper = matrix_[i][0];
      if (current_upper != INF_TIME) {
        matrix_[i][0] = INF_TIME;
      }
    }
  }

  minimize();
}

void DBM::reset_clock(size_t clock_idx) {
  check_index(clock_idx, clock_idx);

  if (clock_idx == 0) {
    return;
  }

  matrix_[clock_idx][0] = 0;
  matrix_[0][clock_idx] = 0;

  minimize();
}

DBM DBM::intersection(const DBM& other) const {
  if (clock_count_ != other.clock_count_) {
    throw std::invalid_argument("DBM sizes must match for intersection");
  }

  DBM result(clock_count_);

  for (size_t i = 0; i < clock_count_; ++i) {
    for (size_t j = 0; j < clock_count_; ++j) {
      int bound1 = matrix_[i][j];
      int bound2 = other.matrix_[i][j];

      if (bound1 == INF_TIME) {
        result.matrix_[i][j] = bound2;
      } else if (bound2 == INF_TIME) {
        result.matrix_[i][j] = bound1;
      } else {
        result.matrix_[i][j] = std::min(bound1, bound2);
      }
    }
  }

  result.minimize();

  return result;
}

bool DBM::is_empty() const { return !is_consistent(); }

void DBM::prune() {
  minimize();
}

bool DBM::contains(const DBM& other) const {
  if (clock_count_ != other.clock_count_) {
    return false;
  }

  for (size_t i = 0; i < clock_count_; ++i) {
    for (size_t j = 0; j < clock_count_; ++j) {
      int this_bound = matrix_[i][j];
      int other_bound = other.matrix_[i][j];

      if (other_bound != INF_TIME &&
          (this_bound == INF_TIME || other_bound < this_bound)) {
        return false;
      }
    }
  }

  return true;
}

std::string DBM::to_string() const {
  if (clock_count_ == 0) {
    return "DBM(empty)";
  }

  std::ostringstream oss;
  oss << "DBM(size=" << clock_count_ << "):\n";
  oss << "   ";
  for (size_t j = 0; j < clock_count_; ++j) {
    oss << std::setw(8) << "x" << j;
  }
  oss << "\n";

  for (size_t i = 0; i < clock_count_; ++i) {
    oss << "x" << i << " ";
    for (size_t j = 0; j < clock_count_; ++j) {
      if (matrix_[i][j] == INF_TIME) {
        oss << std::setw(8) << "∞";
      } else {
        oss << std::setw(8) << matrix_[i][j];
      }
    }
    oss << "\n";
  }

  return oss.str();
}

bool DBM::operator==(const DBM& other) const {
  if (clock_count_ != other.clock_count_) {
    return false;
  }

  if (frozen_clocks_ != other.frozen_clocks_) {
    return false;
  }

  for (size_t i = 0; i < clock_count_; ++i) {
    for (size_t j = 0; j < clock_count_; ++j) {
      if (matrix_[i][j] != other.matrix_[i][j]) {
        return false;
      }
    }
  }

  return true;
}

bool DBM::operator<(const DBM& other) const {
  if (clock_count_ != other.clock_count_) {
    return clock_count_ < other.clock_count_;
  }

  for (size_t i = 0; i < clock_count_; ++i) {
    for (size_t j = 0; j < clock_count_; ++j) {
      if (matrix_[i][j] != other.matrix_[i][j]) {
        if (matrix_[i][j] == INF_TIME) return false;
        if (other.matrix_[i][j] == INF_TIME) return true;
        return matrix_[i][j] < other.matrix_[i][j];
      }
    }
  }

  return false;
}

void DBM::remove_clock(size_t clock_idx) {
  if (clock_idx >= clock_count_) {
    return;
  }

  if (clock_idx == 0) {
    return;
  }

  frozen_clocks_.erase(clock_idx);

  std::vector<std::vector<int>> new_matrix;
  new_matrix.resize(clock_count_ - 1);

  for (size_t i = 0; i < clock_count_; ++i) {
    if (i == clock_idx) continue;

    size_t new_i = (i < clock_idx) ? i : i - 1;
    new_matrix[new_i].resize(clock_count_ - 1);

    for (size_t j = 0; j < clock_count_; ++j) {
      if (j == clock_idx) continue;

      size_t new_j = (j < clock_idx) ? j : j - 1;
      new_matrix[new_i][new_j] = matrix_[i][j];
    }
  }

  matrix_ = new_matrix;
  clock_count_--;

  std::set<size_t> new_frozen;
  for (size_t idx : frozen_clocks_) {
    if (idx < clock_idx) {
      new_frozen.insert(idx);
    } else if (idx > clock_idx) {
      new_frozen.insert(idx - 1);
    }
  }
  frozen_clocks_ = new_frozen;
}

DBM DBM::restrict_for_firing(size_t transition_id, int alpha, int beta) const {
  size_t clock_idx = transition_id + 1;

  if (clock_idx >= clock_count_) {
    return *this;
  }

  DBM result = *this;

  int current_lower = -result.get_constraint(0, clock_idx);
  if (alpha > current_lower) {
    result.set_constraint(0, clock_idx, -alpha);
  }

  int current_upper = result.get_constraint(clock_idx, 0);
  if (beta != INF_TIME && (current_upper == INF_TIME || beta < current_upper)) {
    result.set_constraint(clock_idx, 0, beta);
  }

  result.minimize();

  if (result.is_empty()) {
    return DBM(0);
  }

  return result;
}

void DBM::freeze_clock(size_t clock_idx) {
  if (clock_idx >= clock_count_ || clock_idx == 0) {
    return;
  }

  frozen_clocks_.insert(clock_idx);
}

void DBM::unfreeze_clock(size_t clock_idx) {
  frozen_clocks_.erase(clock_idx);
}

bool DBM::is_frozen(size_t clock_idx) const {
  return frozen_clocks_.find(clock_idx) != frozen_clocks_.end();
}

void DBM::copy_clock_constraints(size_t clock_idx, DBM& target) const {
  if (clock_idx >= clock_count_) {
    return;
  }

  if (clock_idx >= target.size()) {
    target.resize(clock_idx + 1);
  }

  for (size_t i = 0; i < clock_count_; ++i) {
    if (i < target.size()) {
      target.set_constraint(clock_idx, i, matrix_[clock_idx][i]);
      target.set_constraint(i, clock_idx, matrix_[i][clock_idx]);
    }
  }

  if (is_frozen(clock_idx)) {
    target.freeze_clock(clock_idx);
  }
}

bool StateClass::operator==(const StateClass& other) const {
  if (marking != other.marking) return false;
  if (!(Z1 == other.Z1)) return false;
  if (!(Z2 == other.Z2)) return false;
  if (enabled != other.enabled) return false;
  if (suspended != other.suspended) return false;
  return true;
}

bool StateClass::operator<(const StateClass& other) const {
  if (marking < other.marking) return true;
  if (other.marking < marking) return false;

  if (Z1 < other.Z1) return true;
  if (other.Z1 < Z1) return false;

  if (Z2 < other.Z2) return true;
  if (other.Z2 < Z2) return false;

  if (enabled < other.enabled) return true;
  if (other.enabled < enabled) return false;

  return suspended < other.suspended;
}

StateClass StateClass::copy() const {
  StateClass result;
  result.marking = marking;
  result.Z1 = Z1;
  result.Z2 = Z2;
  result.state_id = state_id;
  result.cumulative_time = cumulative_time;
  result.enabled = enabled;
  result.suspended = suspended;
  return result;
}

std::string StateClass::to_string() const {
  std::ostringstream oss;
  oss << "StateClass(id=" << state_id << ", time=" << cumulative_time << ")\n";
  oss << "  Marking: [";
  for (size_t i = 0; i < marking.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << marking[i];
  }
  oss << "]\n";
  oss << "  Z1 (non-suspendable):\n" << Z1.to_string();
  oss << "  Z2 (suspendable):\n" << Z2.to_string();
  return oss.str();
}

}  // namespace state_class
