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

bool StateClass::operator==(const StateClass& other) const {
  return marking == other.marking &&
         clocks == other.clocks &&
         zone == other.zone &&
         transition_to_clock == other.transition_to_clock &&
         clock_to_transition == other.clock_to_transition &&
         enabled == other.enabled &&
         active == other.active &&
         suspended == other.suspended;
}

bool StateClass::operator<(const StateClass& other) const {
  if (marking < other.marking) return true;
  if (other.marking < marking) return false;

  if (transition_to_clock < other.transition_to_clock) return true;
  if (other.transition_to_clock < transition_to_clock) return false;

  if (clock_to_transition < other.clock_to_transition) return true;
  if (other.clock_to_transition < clock_to_transition) return false;

  if (zone < other.zone) return true;
  if (other.zone < zone) return false;

  if (clocks < other.clocks) return true;
  if (other.clocks < clocks) return false;

  if (enabled < other.enabled) return true;
  if (other.enabled < enabled) return false;

  if (active < other.active) return true;
  if (other.active < active) return false;

  return suspended < other.suspended;
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
  for (size_t value : key.enabled) {
    hash_combine(seed, size_hash(value));
  }
  for (size_t value : key.active) {
    hash_combine(seed, size_hash(value));
  }
  for (size_t value : key.suspended) {
    hash_combine(seed, size_hash(value));
  }

  return seed;
}

StateClass StateClass::copy() const {
  StateClass result;
  result.marking = marking;
  result.clocks = clocks;
  result.zone = zone;
  result.transition_to_clock = transition_to_clock;
  result.clock_to_transition = clock_to_transition;
  result.state_id = state_id;
  result.cumulative_time = cumulative_time;
  result.enabled = enabled;
  result.active = active;
  result.suspended = suspended;
  return result;
}

std::string StateClass::to_string() const {
  std::ostringstream oss;
  oss << "StateClass(id=" << state_id << ", time=" << cumulative_time << ")\n";
  oss << "  Marking: [";
  for (size_t i = 0; i < marking.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << marking[i];
  }
  oss << "]\n";

  oss << "  Enabled: {";
  for (auto it = enabled.begin(); it != enabled.end(); ++it) {
    if (it != enabled.begin()) oss << ", ";
    oss << *it;
  }
  oss << "}\n";

  oss << "  Active: {";
  for (auto it = active.begin(); it != active.end(); ++it) {
    if (it != active.begin()) oss << ", ";
    oss << *it;
  }
  oss << "}\n";

  oss << "  Suspended: {";
  for (auto it = suspended.begin(); it != suspended.end(); ++it) {
    if (it != suspended.begin()) oss << ", ";
    oss << *it;
  }
  oss << "}\n";

  oss << "  Clocks:\n";
  for (size_t i = 0; i < clocks.size(); ++i) {
    oss << "    T" << i << ": " << clocks[i].to_string() << "\n";
  }

  if (zone.size() > 0) {
    oss << "  Zone:\n" << zone.to_string();
  }

  return oss.str();
}

void StateClass::rebuild_zone_from_clocks() {
  transition_to_clock.assign(clocks.size(), -1);
  clock_to_transition.clear();
  clock_to_transition.push_back(std::numeric_limits<size_t>::max());

  zone = DBM(1);

  for (size_t t : enabled) {
    if (t >= clocks.size()) {
      continue;
    }

    const size_t clock_idx = zone.add_clock();
    transition_to_clock[t] = static_cast<int>(clock_idx);
    clock_to_transition.push_back(t);

    const auto& clock = clocks[t];
    zone.set_constraint(0, clock_idx, -clock.lower_bound);
    zone.set_constraint(clock_idx, 0, clock.upper_bound);

    if (active.count(t)) {
      zone.unfreeze_clock(clock_idx);
    } else {
      zone.freeze_clock(clock_idx);
    }
  }

  zone.minimize();
}

void StateClass::sync_clocks_from_zone() {
  if (transition_to_clock.size() < clocks.size()) {
    transition_to_clock.resize(clocks.size(), -1);
  }

  for (size_t t = 0; t < clocks.size(); ++t) {
    if (!enabled.count(t) || !has_zone_clock_for_transition(t)) {
      clocks[t] = TransitionClock();
      continue;
    }

    const size_t clock_idx = static_cast<size_t>(transition_to_clock[t]);
    clocks[t].lower_bound = -zone.get_constraint(0, clock_idx);
    clocks[t].upper_bound = zone.get_constraint(clock_idx, 0);

    if (active.count(t)) {
      clocks[t].state = ClockState::ACTIVE;
    } else if (suspended.count(t)) {
      clocks[t].state = ClockState::SUSPENDED;
    } else {
      clocks[t].state = ClockState::UNACTIVE;
    }
  }
}

int StateClass::clock_index_for_transition(size_t transition_id) const {
  if (transition_id >= transition_to_clock.size()) {
    return -1;
  }
  return transition_to_clock[transition_id];
}

size_t StateClass::transition_for_clock(size_t clock_idx) const {
  if (clock_idx >= clock_to_transition.size()) {
    return std::numeric_limits<size_t>::max();
  }
  return clock_to_transition[clock_idx];
}

bool StateClass::has_zone_clock_for_transition(size_t transition_id) const {
  return clock_index_for_transition(transition_id) > 0;
}

}  // namespace state_class
