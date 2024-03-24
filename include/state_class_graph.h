//
// Created by 张凯文 on 2024/3/22.
//

#ifndef PPTPN_INCLUDE_STATE_CLASS_GRAPH_H
#define PPTPN_INCLUDE_STATE_CLASS_GRAPH_H

#include <string>
#include <utility>

struct TPetriNetTransition
{
  bool is_handle = false;
  int runtime = 0;
  int priority = 0;
  std::pair<int, int> const_time = {0, 0};
  // 该变迁分配的处理器资源
  int c = 0;
  TPetriNetTransition() = default;
  TPetriNetTransition(int runtime, int priority, std::pair<int, int> const_time,
                      int core)
      : runtime(runtime), priority(priority), const_time(std::move(const_time)), c(core)
  {
    is_handle = false;
  };
  TPetriNetTransition(bool is_handle, int runtime, int priority,
                      std::pair<int, int> const_time)
      : is_handle(is_handle), runtime(runtime), priority(priority),
        const_time(std::move(const_time)){};
};

struct TPetriNetElement
{
  std::string name, label, shape;
  int token = 0;
  bool enabled = false;
  TPetriNetTransition pnt;

  TPetriNetElement() = default;

  TPetriNetElement(const std::string &name, int token)
      : name(name), token(token)
  {
    label = name;
    shape = "circle";
    enabled = false;
    pnt = {};
  }

  TPetriNetElement(const std::string &name, bool enable,
                   TPetriNetTransition pnt)
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
  std::pair<int, int>
      weight;   // min_weight and max_weight represent the firing time interval
  int priority; // low-level propity remove;
};

struct PTPNTransition
{
  bool is_handle = false;
  bool is_random = false;
  int runtime = 0;
  int priority = 0;
  std::pair<int, int> const_time = {0, 0};
  // 该变迁分配的处理器资源
  int c = 0;
  PTPNTransition() = default;
  PTPNTransition(int priority, std::pair<int, int> time, int c)
      : priority(priority), const_time(std::move(time)), c(c)
  {
    is_handle = false;
    is_random = false;
    runtime = 0;
  };
  PTPNTransition(bool is_handle, int priority, std::pair<int, int> time, int c)
      : is_handle(is_handle), priority(priority), const_time(std::move(time)), c(c)
  {
    is_random = false;
    runtime = 0;
  };
  PTPNTransition(int priority, std::pair<int, int> time, int c, bool is_random)
      : priority(priority), const_time(std::move(time)), c(c), is_random(is_random)
  {
    is_handle = false;
    runtime = 0;
  };
  PTPNTransition(bool is_handle, int priority, std::pair<int, int> time, int c,
                 bool is_random)
      : is_handle(is_handle), priority(priority), const_time(std::move(time)), c(c),
        is_random(is_random)
  {
    runtime = 0;
  };
};



#endif //PPTPN_INCLUDE_STATE_CLASS_GRAPH_H
