#ifndef DAG_CONFIG
#define DAG_CONFIG
#include <string>
#include <vector>
#include <variant>

using namespace std;

// TDG_RAP Vertex结构
struct DAGVertex
{
    std::string name, label, shape;
    int tokens = 0;
};

struct DAGEdge
{
    std::string label;
    std::string style;
};

// 非周期任务：偶发任务和普通任务
struct APeriodicTask
{
    string name;
    int core = 0;
    int priority = 100;
    // 任务的执行时间
    vector<pair<int, int>> time;
    bool is_lock = false;
    vector<string> lock;
    bool is_period = false;
    // 如果是偶发任务，period为偶发周期
    int period = 1000;
};

struct PeriodicTask
{
    string name;
    int core = 0;
    int priority = 100;
    vector<pair<int, int>> time;
    bool is_lock = false;
    vector<string> lock;
    // 周期任务，并且自身结束即周期结束
    bool is_period = false;
    int period;
};

struct DistTask
{
    string name;
    pair<int, int> time = {0, 0};
};

struct SyncTask
{
    string name;
    pair<int, int> time = {0, 0};
};

struct EmptyTask {
  string name;
};

using NodeType = variant<PeriodicTask, APeriodicTask, DistTask, SyncTask, EmptyTask>;

#endif