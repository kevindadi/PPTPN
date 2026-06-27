#include "analysis/scheduling.h"

#include <map>

namespace state_class {

std::set<size_t> Scheduling::structural_enabled(
    const petri::PTPN& net, const petri::Marking& marking) {
  std::set<size_t> enabled;
  const size_t num_transitions = net.num_transitions();
  for (size_t t = 0; t < num_transitions; ++t) {
    if (petri::PTPN::is_enabled(marking, net, t)) {
      enabled.insert(t);
    }
  }
  return enabled;
}

std::set<size_t> Scheduling::filter_priority_per_core(
    const std::set<size_t>& struct_enabled, const petri::PTPN& net) {
  std::map<int, int> highest_priority_per_core;

  for (size_t t : struct_enabled) {
    const auto& trans = net.get_transition(t);
    if (trans.core < 0) {
      continue;  // control transitions do not compete for a core
    }

    auto it = highest_priority_per_core.find(trans.core);
    if (it == highest_priority_per_core.end()) {
      highest_priority_per_core[trans.core] = trans.priority;
    } else if (trans.priority > it->second) {
      it->second = trans.priority;
    }
  }

  std::set<size_t> active;
  for (size_t t : struct_enabled) {
    const auto& trans = net.get_transition(t);
    if (trans.core < 0) {
      active.insert(t);  // control transitions always pass the filter
      continue;
    }
    if (trans.priority == highest_priority_per_core[trans.core]) {
      active.insert(t);
    }
  }

  return active;
}

}  // namespace state_class
