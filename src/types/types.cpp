#include "types/types.h"

namespace {

SchedulePolicy parse_policy_string(const std::string& policy) {
  if (policy == "fixed") {
    return SchedulePolicy::FIXED;
  }
  if (policy == "rm") {
    return SchedulePolicy::RM;
  }
  if (policy == "dm") {
    return SchedulePolicy::DM;
  }
  if (policy == "edf") {
    return SchedulePolicy::EDF;
  }
  if (policy == "llf") {
    return SchedulePolicy::LLF;
  }
  if (policy == "fifo") {
    return SchedulePolicy::FIFO;
  }
  if (policy == "pip") {
    return SchedulePolicy::PIP;
  }
  if (policy == "pcp") {
    return SchedulePolicy::PCP;
  }
  if (policy == "srp") {
    return SchedulePolicy::SRP;
  }
  return SchedulePolicy::UNKNOWN;
}

std::string policy_to_string(SchedulePolicy policy) {
  switch (policy) {
    case SchedulePolicy::FIXED:
      return "fixed";
    case SchedulePolicy::RM:
      return "rm";
    case SchedulePolicy::DM:
      return "dm";
    case SchedulePolicy::EDF:
      return "edf";
    case SchedulePolicy::LLF:
      return "llf";
    case SchedulePolicy::FIFO:
      return "fifo";
    case SchedulePolicy::PIP:
      return "pip";
    case SchedulePolicy::PCP:
      return "pcp";
    case SchedulePolicy::SRP:
      return "srp";
    default:
      return "unknown";
  }
}

}  // namespace

SchedulePolicy parse_schedule_policy(const std::string& policy) {
  return parse_policy_string(policy);
}

std::string schedule_policy_to_string(SchedulePolicy policy) {
  return policy_to_string(policy);
}