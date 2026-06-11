#include "state.h"

#include <functional>
#include <sstream>

namespace scheduling {

namespace {
void hash_combine(size_t& seed, size_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
}  // namespace

bool StateClass::operator==(const StateClass& other) const {
  return marking == other.marking &&
         zone == other.zone &&
         enabled == other.enabled &&
         active == other.active &&
         suspended == other.suspended;
}

bool StateClass::operator<(const StateClass& other) const {
  if (marking < other.marking) return true;
  if (other.marking < marking) return false;

  if (zone < other.zone) return true;
  if (other.zone < zone) return false;

  if (enabled < other.enabled) return true;
  if (other.enabled < enabled) return false;

  if (active < other.active) return true;
  if (other.active < active) return false;

  return suspended < other.suspended;
}

StateClass StateClass::copy() const {
  StateClass result;
  result.marking = marking;
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
    oss << "T" << *it;
  }
  oss << "}\n";

  oss << "  Active: {";
  for (auto it = active.begin(); it != active.end(); ++it) {
    if (it != active.begin()) oss << ", ";
    oss << "T" << *it;
  }
  oss << "}\n";

  if (!suspended.empty()) {
    oss << "  Suspended: {";
    for (auto it = suspended.begin(); it != suspended.end(); ++it) {
      if (it != suspended.begin()) oss << ", ";
      oss << "T" << *it;
    }
    oss << "}\n";
  }

  if (zone.size() > 0) {
    oss << "  Zone:\n" << zone.to_string();
  }

  return oss.str();
}

int StateClass::clock_index_for_transition(size_t tid) const {
  if (tid >= transition_to_clock.size()) {
    return -1;
  }
  return transition_to_clock[tid];
}

size_t StateClass::transition_for_clock(size_t idx) const {
  if (idx >= clock_to_transition.size()) {
    return std::numeric_limits<size_t>::max();
  }
  return clock_to_transition[idx];
}

bool StateClass::has_clock_for_transition(size_t tid) const {
  int idx = clock_index_for_transition(tid);
  return idx > 0 && static_cast<size_t>(idx) < zone.size();
}

bool StateKey::operator==(const StateKey& other) const {
  return marking == other.marking &&
         zone_matrix == other.zone_matrix &&
         frozen_clocks == other.frozen_clocks &&
         enabled == other.enabled &&
         active == other.active &&
         suspended == other.suspended;
}

size_t StateKeyHash::operator()(const StateKey& key) const {
  size_t seed = 0;
  std::hash<int> int_hash;
  std::hash<size_t> size_hash;

  for (int value : key.marking) {
    hash_combine(seed, int_hash(value));
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

bool are_equivalent(const StateClass& a, const StateClass& b) {
  return a.marking == b.marking &&
         a.zone == b.zone &&
         a.enabled == b.enabled &&
         a.active == b.active &&
         a.suspended == b.suspended;
}

}  // namespace scheduling
