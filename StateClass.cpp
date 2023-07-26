//
// Created by Kevin on 2023/7/28.
//
#include "StateClass.h"
#include <iostream>

void StateClass::print_current_mark(){
  for(const auto& l : mark.labels) {
    std::cout << l << " ";
  }
  std::cout << std::endl;
}

bool StateClass::operator==(const StateClass &other) {
  if (mark == other.mark) {
    std::set<std::size_t> h_diff, H_diff;
    std::set_symmetric_difference(t_sched.begin(), t_sched.end(),
                                  other.t_sched.begin(), other.t_sched.end(),
                                  std::inserter(h_diff, h_diff.begin()));
    std::set_symmetric_difference(handle_t_sched.begin(), handle_t_sched.end(),
                                  other.handle_t_sched.begin(), other.handle_t_sched.end(),
                                  std::inserter(H_diff, H_diff.begin()));

    if (H_diff.empty() && h_diff.empty()) {
      return true;
    } else {
      return false;
    }
  }else {
    return false;
  }
}

bool StateClass::operator<(const StateClass& other) const {
  // 比较 mark 和 t_sched 的大小关系
  if (mark.indexes < other.mark.indexes) {
    return true;
  } else if (other.mark.indexes < mark.indexes) {
    return false;
  } else {
    return t_sched < other.t_sched;
  }
}

