#include "analysis/state.h"

#include <functional>
#include <limits>
#include <sstream>

namespace state_class {

namespace {
void hash_combine(size_t& seed, size_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
}  // namespace

bool ReachabilityState::operator==(const ReachabilityState& other) const {
  return marking == other.marking &&
         timing == other.timing &&
         scheduling == other.scheduling;
}

bool ReachabilityState::operator<(const ReachabilityState& other) const {
  if (marking < other.marking) return true;
  if (other.marking < marking) return false;

  if (timing < other.timing) return true;
  if (other.timing < timing) return false;

  return scheduling < other.scheduling;
}

size_t StateKeyHash::operator()(const StateKey& key) const {
  size_t seed = 0;
  std::hash<int> int_hash;
  std::hash<size_t> size_hash;

  for (int value : key.marking) {
    hash_combine(seed, int_hash(value));
  }
  for (int value : key.transition_to_clock) {
    hash_combine(seed, int_hash(value));
  }
  for (size_t value : key.clock_to_transition) {
    hash_combine(seed, size_hash(value));
  }
  for (int value : key.zone_matrix) {
    hash_combine(seed, int_hash(value));
  }
  for (size_t value : key.frozen_clocks) {
    hash_combine(seed, size_hash(value));
  }
  for (size_t value : key.scheduling.enabled) {
    hash_combine(seed, size_hash(value));
  }
  for (size_t value : key.scheduling.active) {
    hash_combine(seed, size_hash(value));
  }
  for (size_t value : key.scheduling.suspended) {
    hash_combine(seed, size_hash(value));
  }

  return seed;
}

ReachabilityState ReachabilityState::copy() const {
  ReachabilityState result;
  result.marking = marking;
  result.timing = timing;
  result.metadata = metadata;
  result.scheduling = scheduling;
  return result;
}

std::string ReachabilityState::to_string() const {
  std::ostringstream oss;
  oss << "ReachabilityState(id=" << metadata.state_id << ", time=" << metadata.cumulative_time << ")\n";
  oss << "  Marking: [";
  for (size_t i = 0; i < marking.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << marking[i];
  }
  oss << "]\n";

  oss << "  Enabled: {";
  for (auto it = scheduling.enabled.begin(); it != scheduling.enabled.end(); ++it) {
    if (it != scheduling.enabled.begin()) oss << ", ";
    oss << *it;
  }
  oss << "}\n";

  oss << "  Active: {";
  for (auto it = scheduling.active.begin(); it != scheduling.active.end(); ++it) {
    if (it != scheduling.active.begin()) oss << ", ";
    oss << *it;
  }
  oss << "}\n";

  oss << "  Suspended: {";
  for (auto it = scheduling.suspended.begin(); it != scheduling.suspended.end(); ++it) {
    if (it != scheduling.suspended.begin()) oss << ", ";
    oss << *it;
  }
  oss << "}\n";

  oss << "  Clocks:\n";
  for (size_t i = 0; i < timing.clocks.size(); ++i) {
    oss << "    T" << i << ": " << timing.clocks[i].to_string() << "\n";
  }

  if (timing.zone.size() > 0) {
    oss << "  Zone:\n" << timing.zone.to_string();
  }

  return oss.str();
}

void ReachabilityState::rebuild_zone_from_clocks() {
  timing.transition_to_clock.assign(timing.clocks.size(), -1);
  timing.clock_to_transition.clear();
  timing.clock_to_transition.push_back(std::numeric_limits<size_t>::max());

  timing.zone = DBM(1);

  for (size_t t : scheduling.enabled) {
    if (t >= timing.clocks.size()) {
      continue;
    }

    const size_t clock_idx = timing.zone.add_clock();
    timing.transition_to_clock[t] = static_cast<int>(clock_idx);
    timing.clock_to_transition.push_back(t);

    const auto& clock = timing.clocks[t];
    timing.zone.set_constraint(0, clock_idx, -clock.lower_bound);
    timing.zone.set_constraint(clock_idx, 0, clock.upper_bound);

    if (scheduling.active.count(t)) {
      timing.zone.unfreeze_clock(clock_idx);
    } else {
      timing.zone.freeze_clock(clock_idx);
    }
  }

  timing.zone.minimize();
}

void ReachabilityState::sync_clocks_from_zone() {
  if (timing.transition_to_clock.size() < timing.clocks.size()) {
    timing.transition_to_clock.resize(timing.clocks.size(), -1);
  }

  for (size_t t = 0; t < timing.clocks.size(); ++t) {
    if (!scheduling.enabled.count(t) || !has_zone_clock_for_transition(t)) {
      timing.clocks[t] = TransitionClock();
      continue;
    }

    const size_t clock_idx = static_cast<size_t>(timing.transition_to_clock[t]);
    timing.clocks[t].lower_bound = -timing.zone.get_constraint(0, clock_idx);
    timing.clocks[t].upper_bound = timing.zone.get_constraint(clock_idx, 0);

    if (scheduling.active.count(t)) {
      timing.clocks[t].state = ClockState::ACTIVE;
    } else if (scheduling.suspended.count(t)) {
      timing.clocks[t].state = ClockState::SUSPENDED;
    } else {
      timing.clocks[t].state = ClockState::UNACTIVE;
    }
  }
}

int ReachabilityState::clock_index_for_transition(size_t transition_id) const {
  if (transition_id >= timing.transition_to_clock.size()) {
    return -1;
  }
  return timing.transition_to_clock[transition_id];
}

size_t ReachabilityState::transition_for_clock(size_t clock_idx) const {
  if (clock_idx >= timing.clock_to_transition.size()) {
    return std::numeric_limits<size_t>::max();
  }
  return timing.clock_to_transition[clock_idx];
}

bool ReachabilityState::has_zone_clock_for_transition(size_t transition_id) const {
  return clock_index_for_transition(transition_id) > 0;
}

}  // namespace state_class
