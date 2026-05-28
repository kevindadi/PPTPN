#include "analysis/state.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>

namespace state_class {

namespace {
void hash_combine(size_t& seed, size_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
}  // namespace

bool StateClass::operator==(const StateClass& other) const {
  if (marking != other.marking) return false;
  if (clocks != other.clocks) return false;
  if (enabled != other.enabled) return false;
  if (suspended != other.suspended) return false;
  return true;
}

bool StateClass::operator<(const StateClass& other) const {
  if (marking < other.marking) return true;
  if (other.marking < marking) return false;

  if (clocks < other.clocks) return true;
  if (other.clocks < clocks) return false;

  if (enabled < other.enabled) return true;
  if (other.enabled < enabled) return false;

  return suspended < other.suspended;
}

size_t StateKeyHash::operator()(const StateKey& key) const {
  size_t seed = 0;
  std::hash<int> int_hash;
  std::hash<size_t> size_hash;

  for (int value : key.marking) {
    hash_combine(seed, int_hash(value));
  }
  for (size_t value : key.enabled) {
    hash_combine(seed, size_hash(value));
  }
  for (size_t value : key.suspended) {
    hash_combine(seed, size_hash(value));
  }
  for (const auto& tc : key.clocks) {
    hash_combine(seed, int_hash(tc.lower_bound));
    hash_combine(seed, int_hash(tc.upper_bound));
    hash_combine(seed, static_cast<size_t>(tc.state));
  }

  return seed;
}

StateClass StateClass::copy() const {
  StateClass result;
  result.marking = marking;
  result.clocks = clocks;
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

  return oss.str();
}

}  // namespace state_class