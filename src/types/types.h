#ifndef TYPES_H
#define TYPES_H

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

enum class TaskType { NORMAL, PERIOD, APERIOD, INTERRUPT };

// 实时系统调度策略
enum class SchedulePolicy {
  FIXED,   // 固定优先级
  RM,      // Rate Monotonic - 周期越短优先级越高
  DM,      // Deadline Monotonic - 截止时间越短优先级越高
  EDF,     // Earliest Deadline First - 截止时间最早优先
  LLF,     // Least Laxity First - 松弛时间最小优先
  FIFO,    // 先来先服务
  PIP,     // Priority Inheritance Protocol - 优先级继承协议
  PCP,     // Priority Ceiling Protocol - 优先级天花板协议
  SRP,     // Stack Resource Policy - 栈资源策略
  UNKNOWN  // 未知策略
};

SchedulePolicy parse_schedule_policy(const std::string& policy);
std::string schedule_policy_to_string(SchedulePolicy policy);

struct TaskNode {
  std::string name;
  int core = 0;
  int priority = 100;
  std::vector<std::pair<int, int>> time;
  bool is_lock = false;
  std::vector<std::string> lock;
  TaskType task_type = TaskType::NORMAL;
};

struct ForkTask {
  std::string name;
  std::pair<int, int> time = std::make_pair(0, 0);
  ForkTask() = default;
  ForkTask(const std::string& n) : name(n) {}
};

struct JoinTask {
  std::string name;
  std::pair<int, int> time = std::make_pair(0, 0);
  JoinTask() = default;
  JoinTask(const std::string& n) : name(n) {}
};

struct EmptyTask {
  std::string name;
};

using NodeType = std::variant<TaskNode, ForkTask, JoinTask, EmptyTask>;

enum class TDGVertexType { TASK, FORK, JOIN, EMPTY };

struct TaskConfig {
  int core;
  int priority;
  std::vector<std::pair<int, int>> times;
  std::vector<std::string> locks;
};

struct StartBinding {
  std::string task;
  int tokens = 1;
};

struct PeriodicBinding {
  std::string task;
  int period = 0;
};

#endif  // TYPES_H
