#ifndef TYPES_H
#define TYPES_H

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

enum class TaskType { NORMAL, PERIOD, APERIOD, INTERRUPT };

struct APeriodicTask {
  std::string name;
  int core = 0;
  int priority = 100;
  std::vector<std::pair<int, int>> time;
  bool is_lock = false;
  std::vector<std::string> lock;
  TaskType task_type = TaskType::NORMAL;
};

struct PeriodicTask {
  std::string name;
  int core = 0;
  int priority = 100;
  std::vector<std::pair<int, int>> time;
  bool is_lock = false;
  std::vector<std::string> lock;
  TaskType task_type = TaskType::PERIOD;
  std::pair<int, int> period_time = {0, 0};
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

using NodeType = std::variant<PeriodicTask, APeriodicTask,
                              ForkTask, JoinTask, EmptyTask>;

enum class TDGVertexType { TASK, FORK, JOIN, EMPTY };

struct TaskConfig {
  int core;
  int priority;
  std::vector<std::pair<int, int>> times;
  std::vector<std::string> locks;
};

#endif  // TYPES_H