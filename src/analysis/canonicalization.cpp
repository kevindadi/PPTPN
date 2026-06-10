#include "canonicalization.h"
#include "state.h"

#include <algorithm>
#include <iterator>

namespace state_class {

StateClass canonicalize(const StateClass& a, const StateClass& b,
                        CanonicalizationMode mode) {
  StateClass result;

  if (a.marking != b.marking) {
    return result;
  }

  result = a.copy();
  result.marking = a.marking;

  switch (mode) {
    case CanonicalizationMode::EQUALITY:
      break;

    case CanonicalizationMode::MAX_LOWER_BOUND:
    case CanonicalizationMode::INTERSECTION: {
      const size_t num_clocks = std::min(a.clocks.size(), b.clocks.size());
      result.clocks.resize(num_clocks);

      for (size_t i = 0; i < num_clocks; ++i) {
        const TransitionClock& ca = a.clocks[i];
        const TransitionClock& cb = b.clocks[i];
        TransitionClock cr;

        cr.lower_bound = std::max(ca.lower_bound, cb.lower_bound);
        cr.upper_bound = std::min(ca.upper_bound, cb.upper_bound);

        if (mode == CanonicalizationMode::MAX_LOWER_BOUND) {
          cr.state = (ca.state < cb.state) ? ca.state : cb.state;
        } else {
          cr.state = (ca.state == cb.state) ? ca.state : ClockState::UNACTIVE;
        }

        result.clocks[i] = cr;
      }

      result.enabled.clear();
      std::set_union(a.enabled.begin(), a.enabled.end(),
                     b.enabled.begin(), b.enabled.end(),
                     std::inserter(result.enabled, result.enabled.end()));

      result.active.clear();
      std::set_intersection(a.active.begin(), a.active.end(),
                            b.active.begin(), b.active.end(),
                            std::inserter(result.active, result.active.end()));

      result.suspended.clear();
      std::set_intersection(a.suspended.begin(), a.suspended.end(),
                            b.suspended.begin(), b.suspended.end(),
                            std::inserter(result.suspended, result.suspended.end()));

      result.rebuild_zone_from_clocks();
      break;
    }
  }

  result.state_id = std::max(a.state_id, b.state_id);
  result.cumulative_time = std::max(a.cumulative_time, b.cumulative_time);
  return result;
}

bool are_equivalent(const StateClass& a, const StateClass& b,
                    CanonicalizationMode mode) {
  if (a.marking != b.marking) {
    return false;
  }

  if (a.enabled != b.enabled || a.active != b.active ||
      a.suspended != b.suspended) {
    return false;
  }

  switch (mode) {
    case CanonicalizationMode::EQUALITY: {
      const bool has_dbm_identity =
          a.zone.size() > 0 || b.zone.size() > 0 ||
          !a.transition_to_clock.empty() || !b.transition_to_clock.empty() ||
          !a.clock_to_transition.empty() || !b.clock_to_transition.empty();
      if (has_dbm_identity) {
        return a.transition_to_clock == b.transition_to_clock &&
               a.clock_to_transition == b.clock_to_transition &&
               a.zone == b.zone;
      }
      return a.clocks == b.clocks;
    }

    case CanonicalizationMode::MAX_LOWER_BOUND:
    case CanonicalizationMode::INTERSECTION:
      if (a.clocks.size() != b.clocks.size()) {
        return false;
      }
      for (size_t i = 0; i < a.clocks.size(); ++i) {
        if (a.clocks[i].lower_bound != b.clocks[i].lower_bound ||
            a.clocks[i].upper_bound != b.clocks[i].upper_bound) {
          return false;
        }
      }
      return true;
  }

  return false;
}

}  // namespace state_class
