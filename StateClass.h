//
// Created by Kevin on 2023/7/28.
//

#ifndef PPTPN_STATECLASS_H
#define PPTPN_STATECLASS_H
#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <unordered_map>

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
    runtime(runtime), priority(priority), const_time(const_time), c(core) {
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
      : name(name), enabled(enable), pnt(pnt)
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
};

struct SchedT {
  std::size_t t;
  std::pair<int, int> time;
};

class StateClass {
public:
  // 当前标识
  Marking mark;
  // 可调度变迁集
  std::set<std::size_t> t_sched;
  // 可挂起变迁
  std::set<std::size_t> handle_t_sched;
  // 变迁的已等待时间
  std::unordered_map<std::size_t, int> t_time;

public:
  bool operator<(const StateClass& other) const;

  StateClass() = default;
  StateClass(Marking mark, const std::set<std::size_t>& h_t,
             const std::set<std::size_t>& H_t,
             const std::unordered_map<std::size_t, int>& t_time) : mark(std::move(mark)),
                                                                   t_sched(h_t),
                                                                   handle_t_sched(H_t),
                                                                   t_time(t_time ) {}
  void print_current_mark();
  bool operator==(const StateClass& other);
};


#endif // PPTPN_STATECLASS_H
