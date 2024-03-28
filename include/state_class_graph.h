//
// Created by 张凯文 on 2024/3/22.
//

#ifndef PPTPN_INCLUDE_STATE_CLASS_GRAPH_H
#define PPTPN_INCLUDE_STATE_CLASS_GRAPH_H

#include <algorithm>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/graph_utility.hpp>
#include <boost/graph/graphviz.hpp>
#include <string>
#include <unordered_map>
#include <utility>

struct TPetriNetTransition {
  bool is_handle = false;
  int runtime = 0;
  int priority = 0;
  std::pair<int, int> const_time = {0, 0};
  // 该变迁分配的处理器资源
  int c = 0;
  TPetriNetTransition() = default;
  TPetriNetTransition(int runtime, int priority, std::pair<int, int> const_time,
                      int core)
      : runtime(runtime), priority(priority), const_time(std::move(const_time)),
        c(core) {
    is_handle = false;
  };
  TPetriNetTransition(bool is_handle, int runtime, int priority,
                      std::pair<int, int> const_time)
      : is_handle(is_handle), runtime(runtime), priority(priority),
        const_time(std::move(const_time)){};
};

struct TPetriNetElement {
  std::string name, label, shape;
  int token = 0;
  bool enabled = false;
  TPetriNetTransition pnt;

  TPetriNetElement() = default;

  TPetriNetElement(const std::string &name, int token)
      : name(name), token(token) {
    label = name;
    shape = "circle";
    enabled = false;
    pnt = {};
  }

  TPetriNetElement(const std::string &name, bool enable,
                   TPetriNetTransition pnt)
      : name(name), enabled(enable), pnt(std::move(pnt)) {
    label = name;
    shape = "box";
    token = 0;
  }
};

struct TPetriNetEdge {
  std::string label;
  std::pair<int, int>
      weight;   // min_weight and max_weight represent the firing time interval
  int priority; // low-level propity remove;
};

struct PTPNTransition {
  bool is_handle = false;
  bool is_random = false;
  int runtime = 0;
  int priority = 0;
  std::pair<int, int> const_time = {0, 0};
  // 该变迁分配的处理器资源
  int c = 0;
  PTPNTransition() = default;
  PTPNTransition(int priority, std::pair<int, int> time, int c)
      : priority(priority), const_time(std::move(time)), c(c) {
    is_handle = false;
    is_random = false;
    runtime = 0;
  };
  PTPNTransition(bool is_handle, int priority, std::pair<int, int> time, int c)
      : is_handle(is_handle), priority(priority), const_time(std::move(time)),
        c(c) {
    is_random = false;
    runtime = 0;
  };
  PTPNTransition(int priority, std::pair<int, int> time, int c, bool is_random)
      : priority(priority), const_time(std::move(time)), c(c),
        is_random(is_random) {
    is_handle = false;
    runtime = 0;
  };
  PTPNTransition(bool is_handle, int priority, std::pair<int, int> time, int c,
                 bool is_random)
      : is_handle(is_handle), priority(priority), const_time(std::move(time)),
        c(c), is_random(is_random) {
    runtime = 0;
  };
};

// 可发生变迁, 从中筛选可调度变迁
struct SchedT {
  std::size_t t;
  std::pair<int, int> time;

  // Define the less-than operator for SchedT
  bool operator<(const SchedT &other) const {
    // Compare the 't' member first
    if (t < other.t)
      return true;
    if (other.t < t)
      return false;

    // If 't' is equal, compare based on 'time'
    return time < other.time;
  }
};

// 可挂起变迁的等待时间
struct T_wait {
  std::size_t t;
  int time;

  // Define the less-than operator for SchedT
  bool operator<(const T_wait &other) const {
    // Compare the 't' member first
    if (t < other.t)
      return true;
    if (other.t < t)
      return false;

    // If 't' is equal, compare based on 'time'
    return time < other.time;
  }
  // 需要在T_wait中定义operator==以便于比较
  bool operator==(const T_wait &other) const {
    return t == other.t && time == other.time;
  }

  T_wait() = default;
  T_wait(std::size_t t, int time) : t(t), time(time) {}
};

// 状态类图的节点
struct SCGVertex {
  std::string id;
  std::string label;
};

// 状态类图中的编
struct SCGEdge {
  std::string label;
  std::pair<int, int> time;
};

struct Marking {
  std::set<std::size_t> indexes;
  std::set<std::string> labels;

  bool operator==(const Marking &other) const {
    if (indexes.size() != other.indexes.size()) {
      return false;
    }
    if (indexes == other.indexes) {
      return true;
    }
    return false;
  }
  bool operator<(const Marking &other) const { return indexes < other.indexes; }
};
//
class StateClass {
public:
  // 当前标识
  Marking mark;
  // 使能变迁和可挂起变迁的等待时间
  std::set<T_wait> all_t;

public:
  StateClass() = default;
  StateClass(Marking mark, std::set<T_wait> all_t)
      : mark(std::move(mark)), all_t(std::move(all_t)) {}
  void print_current_mark();
  void print_current_state();
  std::string to_scg_vertex();
  bool operator==(const StateClass &other) const {
    return mark == other.mark && all_t == other.all_t;
  }

  bool operator<(const StateClass &other) const {
    // Compare the mark member first
    if (mark < other.mark)
      return true;
    if (other.mark < mark)
      return false;

    // If the mark members are equal, compare the all_t member
    return all_t < other.all_t;
  };
};

namespace std {
// 为Marking定义哈希函数
template <> struct hash<Marking> {
  std::size_t operator()(const Marking &m) const {
    std::size_t hash_val = 0;
    for (const auto &index : m.indexes) {
      hash_val ^= std::hash<std::size_t>()(index);
    }
    return hash_val;
  }
};

template <> struct hash<T_wait> {
  std::size_t operator()(const T_wait &t) const {
    return std::hash<std::size_t>()(t.t) ^ std::hash<int>()(t.time);
  }
};
} // namespace std

// 为StateClass定义哈希函数
struct StateClassHasher {
  std::size_t operator()(const StateClass &k) const {
    // 计算mark的哈希值
    std::size_t mark_hash = std::hash<Marking>()(k.mark);

    // 计算all_t的哈希值
    std::size_t all_t_hash = 0;
    for (const auto &t : k.all_t) {
      all_t_hash ^= std::hash<T_wait>()(t);
    }

    return mark_hash ^ all_t_hash;
  }
};

// 为StateClass定义等价比较函数
struct StateClassEqual {
  bool operator()(const StateClass &a, const StateClass &b) const {
    return a == b;
  }
};

typedef boost::property<boost::graph_name_t, std::string> graph_scg;
typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS,
                              SCGVertex, SCGEdge, graph_scg>
    SCG;
typedef boost::graph_traits<SCG>::vertex_descriptor ScgVertexD;
typedef boost::graph_traits<SCG>::edge_descriptor ScgEdgeD;
typedef std::unordered_map<StateClass, ScgVertexD, StateClassHasher,
                           StateClassEqual>
    ScgVertexMap;
typedef std::vector<ScgEdgeD> Path;

class WCETBFSVisitor : public boost::default_bfs_visitor {
public:
  WCETBFSVisitor(const std::string &end_name, const std::string &exit_name)
      : _endName(end_name), _exitName(exit_name) {}

  std::string _endName;
  std::string _exitName;
};

class StateClassGraph {
public:
  SCG scg;
  ScgVertexMap scg_vertex_map;
  ScgVertexD add_scg_vertex(StateClass sc);
  void write_to_dot(const std::string &scg_path);

  void dfs_all_path(ScgVertexD start, ScgVertexD end,
                    std::vector<Path> &all_path, Path &current_path,
                    std::vector<bool> &visited, std::string &exit_flag);
  void dfs_all_path(ScgVertexD start, std::string &end,
                    std::vector<Path> &all_path, Path &current_path,
                    std::vector<bool> &visited, std::string &exit_flag);
  // calculate wcet
  std::pair<int, std::vector<Path>>
  calculate_wcet(ScgVertexD &start, ScgVertexD &end, std::string &exit_flag);
  int only_calculate_wcet(ScgVertexD start, ScgVertexD end);
  std::pair<std::set<ScgVertexD>, std::set<ScgVertexD>>
  find_task_vertex(std::string task_name);
  int task_wcet();
  // Check deadlock
  bool check_deadlock();
};
#endif // PPTPN_INCLUDE_STATE_CLASS_GRAPH_H
