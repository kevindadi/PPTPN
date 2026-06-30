#ifndef ANALYSIS_DBM_H
#define ANALYSIS_DBM_H

#include <set>
#include <string>
#include <vector>

namespace state_class {

struct DBMInstrumentation {
  size_t minimize_calls = 0;
};

void reset_dbm_instrumentation();
[[nodiscard]] DBMInstrumentation get_dbm_instrumentation();

class DBM {
 public:
  explicit DBM(size_t size = 0);

  DBM(const DBM& other);
  DBM& operator=(const DBM& other);
  DBM(DBM&& other) noexcept = default;
  DBM& operator=(DBM&& other) noexcept = default;

  [[nodiscard]] size_t size() const { return clock_count_; }

  void set_constraint(size_t i, size_t j, int bound);
  [[nodiscard]] int get_constraint(size_t i, size_t j) const;
  [[nodiscard]] bool is_consistent() const;
  void minimize();
  size_t add_clock();
  void resize(size_t new_size);
  void elapse_time(int delta);
  void future();
  void reset_clock(size_t clock_idx);
  void forget_clock(size_t clock_idx);
  void remove_clock(size_t clock_idx);
  [[nodiscard]] DBM restrict_clock(size_t clock_idx, int alpha, int beta) const;
  void constrain_upper_bound(size_t clock_idx, int beta);
  void synchronize_clocks(const std::vector<size_t>& clock_indices);
  [[nodiscard]] DBM restrict_for_firing(size_t transition_id, int alpha,
                                        int beta) const;
  void freeze_clock(size_t clock_idx);
  void unfreeze_clock(size_t clock_idx);
  [[nodiscard]] bool is_frozen(size_t clock_idx) const;
  void copy_clock_constraints(size_t clock_idx, DBM& target) const;
  [[nodiscard]] DBM intersection(const DBM& other) const;
  [[nodiscard]] bool is_empty() const;
  void prune();
  [[nodiscard]] bool contains(const DBM& other) const;

  // Returns true when this zone is a subset of `other`, i.e. every constraint
  // of this DBM is at least as tight as the matching constraint in `other`
  // (this[i,j] <= other[i,j] for all i,j, with +inf treated as the loosest
  // bound). Both DBMs must already be canonical (minimized) for this to be a
  // sound geometric inclusion test.
  [[nodiscard]] bool included_in(const DBM& other) const;
  [[nodiscard]] std::string to_string() const;
  [[nodiscard]] const std::vector<int>& raw_matrix() const { return matrix_; }
  [[nodiscard]] const std::set<size_t>& frozen_clocks() const {
    return frozen_clocks_;
  }
  bool operator==(const DBM& other) const;
  bool operator<(const DBM& other) const;

 private:
  std::vector<int> matrix_;
  size_t clock_count_;
  std::set<size_t> frozen_clocks_;

  [[nodiscard]] size_t offset(size_t i, size_t j) const;
  void check_index(size_t i, size_t j) const;
  void initialize_clock(size_t clock_idx);
};

}  // namespace state_class

#endif  // ANALYSIS_DBM_H