#include "canonicalization.h"
#include "state.h"

#include <algorithm>
#include <iterator>

namespace state_class {

namespace {

bool has_dbm_identity(const ReachabilityState& state) {
  return state.timing.zone.size() > 0 || !state.timing.transition_to_clock.empty() ||
         !state.timing.clock_to_transition.empty();
}

bool can_intersect_zones(const ReachabilityState& a, const ReachabilityState& b) {
  return has_dbm_identity(a) && has_dbm_identity(b) && a.timing.zone.size() > 0 &&
         a.timing.zone.size() == b.timing.zone.size() &&
         a.timing.transition_to_clock == b.timing.transition_to_clock &&
         a.timing.clock_to_transition == b.timing.clock_to_transition;
}

void sync_zone_activity_with_sets(ReachabilityState& state) {
  if (state.timing.zone.size() == 0) {
    return;
  }

  for (size_t t : state.scheduling.enabled) {
    if (!state.has_zone_clock_for_transition(t)) {
      continue;
    }

    const size_t clock_idx =
        static_cast<size_t>(state.clock_index_for_transition(t));
    if (state.scheduling.active.count(t)) {
      state.timing.zone.unfreeze_clock(clock_idx);
    } else {
      state.timing.zone.freeze_clock(clock_idx);
    }
  }
}

}  // namespace

ReachabilityState canonicalize(const ReachabilityState& a, const ReachabilityState& b,
                        CanonicalizationMode mode) {
  ReachabilityState result;

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
      result.scheduling.enabled.clear();
      std::set_union(a.scheduling.enabled.begin(), a.scheduling.enabled.end(),
                     b.scheduling.enabled.begin(), b.scheduling.enabled.end(),
                     std::inserter(result.scheduling.enabled, result.scheduling.enabled.end()));

      result.scheduling.active.clear();
      std::set_intersection(a.scheduling.active.begin(), a.scheduling.active.end(),
                            b.scheduling.active.begin(), b.scheduling.active.end(),
                            std::inserter(result.scheduling.active, result.scheduling.active.end()));

      result.scheduling.suspended.clear();
      std::set_intersection(a.scheduling.suspended.begin(), a.scheduling.suspended.end(),
                            b.scheduling.suspended.begin(), b.scheduling.suspended.end(),
                            std::inserter(result.scheduling.suspended, result.scheduling.suspended.end()));

      if (mode == CanonicalizationMode::INTERSECTION &&
          can_intersect_zones(a, b)) {
        result.timing.transition_to_clock = a.timing.transition_to_clock;
        result.timing.clock_to_transition = a.timing.clock_to_transition;
        result.timing.zone = a.timing.zone.intersection(b.timing.zone);
        sync_zone_activity_with_sets(result);
        result.sync_clocks_from_zone();
        break;
      }

      const size_t num_clocks = std::min(a.timing.clocks.size(), b.timing.clocks.size());
      result.timing.clocks.resize(num_clocks);

      for (size_t i = 0; i < num_clocks; ++i) {
        const TransitionClock& ca = a.timing.clocks[i];
        const TransitionClock& cb = b.timing.clocks[i];
        TransitionClock cr;

        cr.lower_bound = std::max(ca.lower_bound, cb.lower_bound);
        cr.upper_bound = std::min(ca.upper_bound, cb.upper_bound);

        if (mode == CanonicalizationMode::MAX_LOWER_BOUND) {
          cr.state = (ca.state < cb.state) ? ca.state : cb.state;
        } else {
          cr.state = (ca.state == cb.state) ? ca.state : ClockState::UNACTIVE;
        }

        result.timing.clocks[i] = cr;
      }

      result.rebuild_zone_from_clocks();
      break;
    }
  }

  result.metadata.state_id = std::max(a.metadata.state_id, b.metadata.state_id);
  result.metadata.cumulative_time = std::max(a.metadata.cumulative_time, b.metadata.cumulative_time);
  return result;
}

bool are_equivalent(const ReachabilityState& a, const ReachabilityState& b,
                    CanonicalizationMode mode) {
  if (a.marking != b.marking) {
    return false;
  }

  if (a.scheduling.enabled != b.scheduling.enabled || a.scheduling.active != b.scheduling.active ||
      a.scheduling.suspended != b.scheduling.suspended) {
    return false;
  }

  switch (mode) {
    case CanonicalizationMode::EQUALITY: {
      const bool has_dbm_identity =
          a.timing.zone.size() > 0 || b.timing.zone.size() > 0 ||
          !a.timing.transition_to_clock.empty() || !b.timing.transition_to_clock.empty() ||
          !a.timing.clock_to_transition.empty() || !b.timing.clock_to_transition.empty();
      if (has_dbm_identity) {
        return a.timing.transition_to_clock == b.timing.transition_to_clock &&
               a.timing.clock_to_transition == b.timing.clock_to_transition &&
               a.timing.zone == b.timing.zone;
      }
      return a.timing.clocks == b.timing.clocks;
    }

    case CanonicalizationMode::MAX_LOWER_BOUND:
    case CanonicalizationMode::INTERSECTION:
      if (a.timing.clocks.size() != b.timing.clocks.size()) {
        return false;
      }
      for (size_t i = 0; i < a.timing.clocks.size(); ++i) {
        if (a.timing.clocks[i].lower_bound != b.timing.clocks[i].lower_bound ||
            a.timing.clocks[i].upper_bound != b.timing.clocks[i].upper_bound) {
          return false;
        }
      }
      return true;
  }

  return false;
}

}  // namespace state_class
