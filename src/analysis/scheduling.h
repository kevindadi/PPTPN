#ifndef ANALYSIS_SCHEDULING_H
#define ANALYSIS_SCHEDULING_H

#include <set>
#include <vector>

#include "petri/petri.h"

namespace state_class {

// Structural and priority enabling, the two set operations the state-class
// construction relies on. Priority filtering is done per core, matching the
// fixed-priority scheduling semantics of the formal model.
class Scheduling {
 public:
  // E_struct(M): transitions whose input places hold enough tokens.
  static std::set<size_t> structural_enabled(const petri::PTPN& net,
                                             const petri::Marking& marking);

  // E_pri(M): within every core group (identified by the transition's `core`
  // attribute, including the control core -1) keep only the structurally
  // enabled transitions with the highest priority on that core (ties allowed).
  static std::set<size_t> filter_priority_per_core(
      const std::set<size_t>& struct_enabled, const petri::PTPN& net);
};

}  // namespace state_class

#endif  // ANALYSIS_SCHEDULING_H
