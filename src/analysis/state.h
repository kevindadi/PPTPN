#ifndef STATE_H
#define STATE_H

#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>

#include "petri/petri.h"

namespace state_class {

constexpr int INF_TIME = std::numeric_limits<int>::max();
constexpr double INF_DOUBLE = std::numeric_limits<double>::infinity();

class DBM {
 public:
  explicit DBM(size_t size = 0);

  DBM(const DBM& other);
  DBM& operator=(const DBM& other);
  DBM(DBM&& other) noexcept = default;
  DBM& operator=(DBM&& other) noexcept = default;

  [[nodiscard]] size_t size() const { return matrix_.size(); }

  void set_constraint(size_t i, size_t j, int bound);
  [[nodiscard]] int get_constraint(size_t i, size_t j) const;
  [[nodiscard]] bool is_consistent() const;
  void minimize();
  size_t add_clock();
  void resize(size_t new_size);
  void elapse_time(int delta);
  void reset_clock(size_t clock_idx);
  void remove_clock(size_t clock_idx);
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
  [[nodiscard]] std::string to_string() const;
  bool operator==(const DBM& other) const;
  bool operator<(const DBM& other) const;

 private:
  std::vector<std::vector<int>> matrix_;
  size_t clock_count_;
  std::set<size_t> frozen_clocks_;

  void check_index(size_t i, size_t j) const;
  void initialize_clock(size_t clock_idx);
};

struct StateClass {
  std::vector<int> marking;
  DBM Z1;
  DBM Z2;

  size_t state_id;
  double cumulative_time;
  std::set<size_t> enabled;
  std::set<size_t> suspended;

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
  int transition_id;
  double firing_time;

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

typedef boost::adjacency_list<
    boost::vecS, boost::vecS, boost::directedS,
    boost::property<boost::vertex_name_t, StateClass>,
    boost::property<boost::edge_name_t, TransitionEdge> >
    SCGraph;

typedef boost::graph_traits<SCGraph>::vertex_descriptor SCVertex;
typedef boost::graph_traits<SCGraph>::edge_descriptor SCEdge;

class StateClassReachabilityGraph {
 public:
  explicit StateClassReachabilityGraph(const petri::PTPN& ptpn);

  void set_pruning_enabled(bool enabled) { pruning_enabled_ = enabled; }
  [[nodiscard]] bool is_pruning_enabled() const { return pruning_enabled_; }

  size_t build(size_t max_states = std::numeric_limits<size_t>::max());

  [[nodiscard]] const SCGraph& get_graph() const { return graph_; }
  [[nodiscard]] SCGraph& get_graph() { return graph_; }

  [[nodiscard]] SCVertex get_initial_vertex() const {
    return initial_vertex_;
  }

  struct Statistics {
    size_t total_states;
    size_t total_transitions;
    size_t enabled_transitions_count;
    size_t pruned_states_count;

    Statistics()
        : total_states(0),
          total_transitions(0),
          enabled_transitions_count(0),
          pruned_states_count(0) {}
  };

  [[nodiscard]] const Statistics& get_statistics() const { return stats_; }

  bool save_to_dot(const std::string& file_path) const;
  bool save_to_json(const std::string& file_path) const;

 private:
  const petri::PTPN& ptpn_;
  SCGraph graph_;
  SCVertex initial_vertex_;
  Statistics stats_;

  StateClass create_initial_state_class();

  void explore_successors(const StateClass& current_state,
                          std::set<StateClass>& visited);

  bool is_transition_enabled(const StateClass& state, size_t trans_idx) const;

  std::pair<int, int> get_transition_time_bounds(const StateClass& state,
                                                 size_t trans_idx) const;

  std::vector<size_t> select_per_core(const std::set<size_t>& enabled) const;

  void apply_preemption(const std::vector<size_t>& chosen,
                        StateClass& state) const;

  bool maximal_time_elapse(StateClass& state, double& dt) const;

  std::tuple<bool, StateClass, double> fire_with_dbm(
      size_t trans_idx, const StateClass& from_state);

  void compute_enabled_and_clocks(StateClass& state);

  std::pair<DBM, DBM> time_advance(const StateClass& state) const;

  bool is_suspended(size_t trans_idx, const std::vector<size_t>& enabled) const;

  bool check_dbm_time_intersection(const DBM& z1, size_t trans_idx) const;

  DBM restrict_for_firing(const DBM& z, size_t trans_idx) const;

  double compute_firing_time(const DBM& z1_up, size_t trans_idx) const;

  StateClass canonicalize(const StateClass& state) const;

  void recompute_suspension(StateClass& state) const;

  DBM get_invariants_for(const std::vector<int>& marking) const;

  StateClass fire_transition(const StateClass& state, size_t trans_idx,
                             double firing_time);

  void update_dbm_constraints(StateClass& state);

  bool should_prune(const StateClass& state,
                    const std::set<StateClass>& visited) const;

  SCVertex find_or_add_vertex(const StateClass& state);

  static std::string format_marking(const std::vector<int>& marking);
  std::string format_transitions(const std::set<size_t>& trans_indices,
                                 bool detailed = true) const;

  std::string format_places(const std::vector<int>& marking) const;

  void log_state_class_details(const StateClass& state,
                               const std::string& prefix = "") const;

  std::map<StateClass, SCVertex> state_to_vertex_;
  size_t next_state_id_;
  bool pruning_enabled_ = false;
};

}  // namespace state_class

#endif