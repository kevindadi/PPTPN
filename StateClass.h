//
// Created by Kevin on 2023/7/28.
//

#ifndef PPTPN_STATECLASS_H
#define PPTPN_STATECLASS_H
#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <boost/graph/graphviz.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>

struct TPetriNetTransition
{
  bool is_handle = false;
  int runtime = 0;
  int priority = 0;
  std::pair<int, int> const_time = {0, 0};
  // 该变迁分配的处理器资源
  int c = 0;
  TPetriNetTransition() = default;
  TPetriNetTransition(int runtime, int priority, std::pair<int, int> const_time, int core) :
    runtime(runtime), priority(priority), const_time(std::move(const_time)), c(core) {
    is_handle = false;
  };
  TPetriNetTransition(bool is_handle, int runtime, int priority, std::pair<int, int> const_time) :
     is_handle(is_handle), runtime(runtime), priority(priority), const_time(std::move(const_time)) {
                                                                                                                                                               };
};

struct TPetriNetElement
{
  std::string name, label, shape;
  int token = 0;
  bool enabled = false;
  TPetriNetTransition pnt;

  TPetriNetElement() = default;

  TPetriNetElement(const std::string& name, int token) : name(name), token(token)
  {
    label = name;
    shape = "circle";
    enabled = false;
    pnt = {};
  }

  TPetriNetElement(const std::string& name, bool enable, TPetriNetTransition pnt)
      : name(name), enabled(enable), pnt(std::move(pnt))
  {
    label = name;
    shape = "box";
    token = 0;
  }
};

struct TPetriNetEdge
{
  std::string label;
  std::pair<int, int> weight; // min_weight and max_weight represent the firing time interval
  int priority;                // low-level propity remove;
};

struct Marking {
  std::set<std::size_t> indexes;
  std::set<std::string> labels;

  bool operator==(const Marking& other) {
    std::set<std::size_t> diff;
    std::set_symmetric_difference(indexes.begin(), indexes.end(),
                                  other.indexes.begin(), other.indexes.end(),
                                  std::inserter(diff, diff.begin()));
    if (diff.empty()) {
      return true;
    }else {
      return false;
    }
  }

  // Define the less-than operator for Marking
  bool operator<(const Marking& other) const {
    return indexes < other.indexes;
  }
};

struct SchedT {
  std::size_t t;
  std::pair<int, int> time;

  // Define the less-than operator for SchedT
  bool operator<(const SchedT& other) const {
    // Compare the 't' member first
    if (t < other.t)
      return true;
    if (other.t < t)
      return false;

    // If 't' is equal, compare based on 'time'
    return time < other.time;
  }
};

struct T_wait {
  std::size_t t;
  int time;

  // Define the less-than operator for SchedT
  bool operator<(const T_wait& other) const {
    // Compare the 't' member first
    if (t < other.t)
      return true;
    if (other.t < t)
      return false;

    // If 't' is equal, compare based on 'time'
    return time < other.time;
  }
};

struct StateVertex {

};

struct StateEdge {

};

class StateClass {
 public:
  typedef boost::property<boost::graph_name_t, std::string> graph_p;
  typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS,
                                StateVertex, StateEdge, graph_p> SCG;
  typedef boost::graph_traits<SCG>::vertex_descriptor vertex_tpn;
 public:
  // 当前标识
  Marking mark;
  // 可调度变迁集
  std::set<std::size_t> t_sched;
  // 可挂起变迁
  std::set<std::size_t> handle_t_sched;
  // 变迁的已等待时间
  std::unordered_map<std::size_t, int> t_time;
  //
  std::set<T_wait> all_t;

public:
  StateClass() = default;
  StateClass(Marking mark, const std::set<std::size_t>& h_t,
             const std::set<std::size_t>& H_t,
             const std::unordered_map<std::size_t, int>& t_time) : mark(std::move(mark)),
                                                                   t_sched(h_t),
                                                                   handle_t_sched(H_t),
                                                                   t_time(t_time ) {}
  StateClass(Marking mark, std::set<T_wait> all_t) : mark(std::move(mark)), all_t(std::move(all_t)) {}
  void print_current_mark();

  bool operator==(const StateClass& other);
  bool operator<(const StateClass& other) const {
    // Compare the mark member first
    if (mark < other.mark)
      return true;
    if (other.mark < mark)
      return false;

    // If the mark members are equal, compare the all_t member
    return all_t < other.all_t;
  };
};




#endif // PPTPN_STATECLASS_H
