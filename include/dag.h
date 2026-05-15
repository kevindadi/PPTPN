#ifndef DAG_CONFIG
#define DAG_CONFIG

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
using namespace std;

// TDG_RAP Vertex结构
struct DAGVertex {
  std::string name, label, shape;
  int tokens = 0;
};

struct DAGEdge {
  std::string label;
  std::string style;
};

enum TaskType {
  NORMAL,
  PERIOD,
  APERIOD,
  INTERRUPT,
};

inline std::unordered_map<TaskType, std::string> TaskTypeToString = {
    {TaskType::NORMAL, "NORMAL"},
    {TaskType::PERIOD, "PERIOD"},
    {TaskType::APERIOD, "APERIOD"},
    {TaskType::INTERRUPT, "INTERRUPT"}};

// 非周期任务:普通任务
struct APeriodicTask {
  string name;
  int core = 0;
  int priority = 100;
  // 任务的执行时间
  vector<pair<int, int>> time;
  bool is_lock = false;
  vector<string> lock;
  TaskType task_type = TaskType::NORMAL;
};

// 周期任务,偶发任务,中断任务
struct PeriodicTask {
  string name;
  int core = 0;
  int priority = 100;
  vector<pair<int, int>> time;
  bool is_lock = false;
  vector<string> lock;
  TaskType task_type = TaskType::PERIOD;
  std::pair<int, int> period_time = {0, 0};
};

// Task category for logical grouping
enum class TaskCategory {
  EXECUTABLE,  // Tasks that execute (PeriodicTask, APeriodicTask)
  CONTROL,     // Control flow nodes (ForkTask, JoinTask)
  MARKER       // Empty/marker nodes
};

// FORK node: represents a branching point in the task graph
struct ForkTask {
  string name;
  std::pair<int, int> time = std::make_pair(0, 0);
  static constexpr TaskCategory category() { return TaskCategory::CONTROL; }
  ForkTask() = default;
  ForkTask(const string& n) : name(n) {}
};

// JOIN node: represents a synchronization point in the task graph
struct JoinTask {
  string name;
  std::pair<int, int> time = std::make_pair(0, 0);
  static constexpr TaskCategory category() { return TaskCategory::CONTROL; }
  JoinTask() = default;
  JoinTask(const string& n) : name(n) {}
};

// Backward compatibility typedefs
using DistTask = ForkTask;
using SyncTask = JoinTask;

struct EmptyTask {
  string name;
};

using NodeType =
    variant<PeriodicTask, APeriodicTask, DistTask, SyncTask, EmptyTask>;

#endif