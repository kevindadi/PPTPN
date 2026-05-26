#include "analysis/state.h"

#include <algorithm>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <functional>
#include <cmath>

namespace state_class {

namespace {
void hash_combine(size_t& seed, size_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
}

bool StateClass::operator==(const StateClass& other) const {
  if (marking != other.marking) return false;
  if (!(Z1 == other.Z1)) return false;
  if (!(Z2 == other.Z2)) return false;
  return true;
}

bool StateClass::operator<(const StateClass& other) const {
  if (marking < other.marking) return true;
  if (other.marking < marking) return false;

  if (Z1 < other.Z1) return true;
  if (other.Z1 < Z1) return false;

  return Z2 < other.Z2;
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
  for (int value : key.Z1.raw_matrix()) {
    hash_combine(seed, int_hash(value));
  }
  for (int value : key.Z2.raw_matrix()) {
    hash_combine(seed, int_hash(value));
  }

  return seed;
}

StateClass StateClass::copy() const {
  StateClass result;
  result.marking = marking;
  result.Z1 = Z1;
  result.Z2 = Z2;
  result.state_id = state_id;
  result.cumulative_time = cumulative_time;
  result.enabled = enabled;
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
  oss << "  Z1 (non-suspendable):\n" << Z1.to_string();
  oss << "  Z2 (suspendable):\n" << Z2.to_string();
  return oss.str();
}

}  // namespace state_class