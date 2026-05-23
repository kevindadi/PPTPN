#ifndef PETRI_H
#define PETRI_H

#include <climits>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../types/types.h"

namespace petri {

constexpr int INF = std::numeric_limits<int>::max();
constexpr int kControlTransitionCore = -1;

struct TimeInterval {
  int earliest;
  int latest;

  TimeInterval(int e = 0, int l = INF) : earliest(e), latest(l) {
    if (earliest < 0) {
      throw std::invalid_argument("earliest time must be non-negative");
    }
    if (latest != INF && latest < earliest) {
      throw std::invalid_argument("latest time must be >= earliest time");
    }
  }

  [[nodiscard]] bool is_valid() const {
    return earliest >= 0 && (latest == INF || latest >= earliest);
  }

  [[nodiscard]] bool contains(int time) const {
    return time >= earliest && (latest == INF || time <= latest);
  }

  [[nodiscard]] std::string to_string() const {
    std::ostringstream oss;
    oss << "[" << earliest << ", ";
    if (latest == INF) {
      oss << "∞";
    } else {
      oss << latest;
    }
    oss << "]";
    return oss.str();
  }
};

struct Place {
  std::string id;
  std::string name;
  int capacity;

  Place(const std::string& id = "", const std::string& name = "", int cap = 1)
      : id(id), name(name), capacity(cap) {}
};

struct Transition {
  std::string id;
  std::string name;
  TimeInterval time_interval;
  int priority;
  int core;
  bool suspendable;

  Transition(const std::string& id = "", const std::string& name = "",
             const TimeInterval& interval = TimeInterval(),
             int priority = INT_MAX, int core = -1, bool suspendable = false)
      : id(id),
        name(name),
        time_interval(interval),
        priority(priority),
        core(core),
        suspendable(suspendable) {}
};

using Marking = std::vector<int>;

class PTPN {
 public:
  PTPN() = default;

  size_t add_place(const std::string& name, int capacity = 1) {
    places.emplace_back(std::to_string(places.size()), name, capacity);
    Pre.emplace_back(std::vector<int>(transitions.size(), 0));
    M0.push_back(0);
    for (auto& row : Post) {
      row.push_back(0);
    }
    return places.size() - 1;
  }

  size_t add_transition(const std::string& name,
                        const TimeInterval& interval = TimeInterval(),
                        int priority = INT_MAX, int core = -1,
                        bool suspendable = false) {
    transitions.emplace_back(std::to_string(transitions.size()), name, interval,
                             priority, core, suspendable);
    for (auto& row : Pre) {
      row.push_back(0);
    }
    Post.emplace_back(std::vector<int>(places.size(), 0));
    pre_arcs.emplace_back();
    post_arcs.emplace_back();
    return transitions.size() - 1;
  }

  void set_pre_arc(size_t place_idx, size_t trans_idx, int weight = 1) {
    if (place_idx >= Pre.size() || trans_idx >= transitions.size()) {
      throw std::out_of_range("Invalid place or transition index");
    }
    Pre[place_idx][trans_idx] = weight;
    rebuild_sparse_arcs();
  }

  void set_post_arc(size_t trans_idx, size_t place_idx, int weight = 1) {
    if (trans_idx >= Post.size() || place_idx >= places.size()) {
      throw std::out_of_range("Invalid transition or place index");
    }
    Post[trans_idx][place_idx] = weight;
    rebuild_sparse_arcs();
  }

  void set_initial_marking(const Marking& marking) {
    if (marking.size() != places.size()) {
      throw std::invalid_argument("Marking size must match number of places");
    }
    M0 = marking;
  }

  void set_initial_marking(size_t place_idx, int tokens) {
    if (place_idx >= places.size()) {
      throw std::out_of_range("Invalid place index");
    }
    if (tokens < 0) {
      throw std::invalid_argument("Token count cannot be negative");
    }
    M0[place_idx] = tokens;
  }

  [[nodiscard]] size_t num_places() const { return places.size(); }
  [[nodiscard]] size_t num_transitions() const { return transitions.size(); }

  [[nodiscard]] const Place& get_place(size_t idx) const {
    if (idx >= places.size()) {
      throw std::out_of_range("Invalid place index");
    }
    return places[idx];
  }

  [[nodiscard]] const Transition& get_transition(size_t idx) const {
    if (idx >= transitions.size()) {
      throw std::out_of_range("Invalid transition index");
    }
    return transitions[idx];
  }

  [[nodiscard]] const Marking& get_marking() const { return M0; }
  [[nodiscard]] const std::vector<std::vector<int>>& get_pre_matrix() const {
    return Pre;
  }
  [[nodiscard]] const std::vector<std::vector<int>>& get_post_matrix() const {
    return Post;
  }

  static bool is_enabled(const Marking& M, const PTPN& net,
                         size_t trans_idx) {
    if (trans_idx >= net.transitions.size()) {
      throw std::out_of_range("Invalid transition index");
    }
    if (M.size() != net.places.size()) {
      throw std::invalid_argument("Marking size must match number of places");
    }

    for (const auto& [place_idx, weight] : net.pre_arcs[trans_idx]) {
      if (M[place_idx] < weight) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool is_enabled(size_t trans_idx) const {
    return is_enabled(M0, *this, trans_idx);
  }

  static Marking fire(const Marking& M, const PTPN& net,
                      size_t trans_idx) {
    if (!is_enabled(M, net, trans_idx)) {
      throw std::runtime_error("Transition is not enabled");
    }

    Marking new_marking = M;

    for (const auto& [place_idx, weight] : net.pre_arcs[trans_idx]) {
      new_marking[place_idx] -= weight;
    }

    for (const auto& [place_idx, weight] : net.post_arcs[trans_idx]) {
      new_marking[place_idx] += weight;
      if (net.places[place_idx].capacity != INF &&
          new_marking[place_idx] > net.places[place_idx].capacity) {
        new_marking[place_idx] = net.places[place_idx].capacity;
      }
    }

    return new_marking;
  }

  void fire_transition(size_t trans_idx) { M0 = fire(M0, *this, trans_idx); }

  [[nodiscard]] std::string to_string() const {
    std::ostringstream oss;
    oss << "=== PTPN ===\n";
    oss << "Places (" << places.size() << "):\n";
    for (size_t i = 0; i < places.size(); ++i) {
      oss << "  P" << i << ": " << places[i].name << " [capacity="
          << (places[i].capacity == INF ? "∞"
                                        : std::to_string(places[i].capacity))
          << ", tokens=" << M0[i] << "]\n";
    }

    oss << "\nTransitions (" << transitions.size() << "):\n";
    for (size_t i = 0; i < transitions.size(); ++i) {
      oss << "  T" << i << ": " << transitions[i].name
          << " [time=" << transitions[i].time_interval.to_string()
          << ", priority=" << transitions[i].priority
          << ", core=" << transitions[i].core
          << ", suspendable=" << (transitions[i].suspendable ? "yes" : "no")
          << "]\n";
    }

    oss << "\nPre Matrix (" << Pre.size() << "x"
        << (Pre.empty() ? 0 : Pre[0].size()) << "):\n";
    for (size_t p = 0; p < Pre.size(); ++p) {
      oss << "  P" << p << ": ";
      for (size_t t = 0; t < Pre[p].size(); ++t) {
        oss << Pre[p][t] << " ";
      }
      oss << "\n";
    }

    oss << "\nPost Matrix (" << Post.size() << "x"
        << (Post.empty() ? 0 : Post[0].size()) << "):\n";
    for (size_t t = 0; t < Post.size(); ++t) {
      oss << "  T" << t << ": ";
      for (size_t p = 0; p < Post[t].size(); ++p) {
        oss << Post[t][p] << " ";
      }
      oss << "\n";
    }

    return oss.str();
  }

  [[nodiscard]] std::vector<size_t> get_enabled_transitions() const {
    std::vector<size_t> enabled;
    for (size_t t = 0; t < transitions.size(); ++t) {
      if (is_enabled(t)) {
        enabled.push_back(t);
      }
    }
    return enabled;
  }

  [[nodiscard]] std::vector<size_t> filter_by_priority(
      const std::vector<size_t>& enabled_transitions) const {
    if (enabled_transitions.empty()) {
      return {};
    }

    std::vector<size_t> scheduling_transitions;
    for (size_t t : enabled_transitions) {
      if (transitions[t].priority != INT_MAX) {
        scheduling_transitions.push_back(t);
      }
    }

    if (scheduling_transitions.empty()) {
      return enabled_transitions;
    }

    int highest_priority = INT_MIN;
    for (size_t t : scheduling_transitions) {
      if (transitions[t].priority > highest_priority) {
        highest_priority = transitions[t].priority;
      }
    }

    std::vector<size_t> filtered;
    for (size_t t : scheduling_transitions) {
      if (transitions[t].priority == highest_priority) {
        filtered.push_back(t);
      }
    }
    return filtered;
  }

  [[nodiscard]] std::vector<size_t> filter_by_core_and_priority(
      const std::vector<size_t>& enabled_transitions) const {
    if (enabled_transitions.empty()) {
      return {};
    }

    std::vector<size_t> non_scheduling_transitions;
    std::map<int, std::vector<size_t>> transitions_by_core;
    for (size_t t : enabled_transitions) {
      if (transitions[t].core < 0) {
        non_scheduling_transitions.push_back(t);
        continue;
      }
      transitions_by_core[transitions[t].core].push_back(t);
    }

    if (transitions_by_core.empty()) {
      return enabled_transitions;
    }

    std::vector<size_t> filtered = non_scheduling_transitions;

    for (auto& [core_id, core_transitions] : transitions_by_core) {
      int highest_priority = INT_MIN;
      for (size_t t : core_transitions) {
        if (transitions[t].priority > highest_priority) {
          highest_priority = transitions[t].priority;
        }
      }

      for (size_t t : core_transitions) {
        if (transitions[t].priority == highest_priority) {
          filtered.push_back(t);
        }
      }
    }

    return filtered;
  }

  [[nodiscard]] std::vector<size_t> get_transitions_by_core(int core_id) const {
    std::vector<size_t> result;
    for (size_t t = 0; t < transitions.size(); ++t) {
      if (transitions[t].core == core_id) {
        result.push_back(t);
      }
    }
    return result;
  }

  [[nodiscard]] std::vector<size_t> get_enabled_transitions_by_core(
      int core_id) const {
    std::vector<size_t> result;
    for (size_t t = 0; t < transitions.size(); ++t) {
      if (transitions[t].core == core_id && is_enabled(t)) {
        result.push_back(t);
      }
    }
    return result;
  }

  void rebuild_sparse_arcs() {
    pre_arcs.assign(transitions.size(), {});
    post_arcs.assign(transitions.size(), {});

    for (size_t p = 0; p < Pre.size(); ++p) {
      for (size_t t = 0; t < Pre[p].size(); ++t) {
        if (Pre[p][t] > 0) {
          pre_arcs[t].push_back({p, Pre[p][t]});
        }
      }
    }

    for (size_t t = 0; t < Post.size(); ++t) {
      for (size_t p = 0; p < Post[t].size(); ++p) {
        if (Post[t][p] > 0) {
          post_arcs[t].push_back({p, Post[t][p]});
        }
      }
    }
  }

  [[nodiscard]] bool verify_structure() const;

  std::vector<Place> places;
  std::vector<Transition> transitions;
  std::vector<std::vector<int>> Pre;
  std::vector<std::vector<int>> Post;
  std::vector<std::vector<std::pair<size_t, int>>> pre_arcs;
  std::vector<std::vector<std::pair<size_t, int>>> post_arcs;
  Marking M0;

  std::map<std::string, std::pair<size_t, size_t>> node_start_end_map;
  std::unordered_map<std::string, std::vector<size_t>> node_pn_map;
  std::vector<size_t> cpus_place;
  std::unordered_map<std::string, size_t> locks_place;
  int node_index = 0;
};

}  // namespace petri

#endif