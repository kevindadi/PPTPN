#ifndef CONFIG
#define CONFIG
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
    int weight = 0;
};

// 非周期任务：偶发任务和普通任务
struct APeriodicTask
{
    string name;
    int core = 0;
    int priority = 100;
    // 任务的执行时间
    pair<int, int> time = {0, 0};
    bool is_lock = false;
    string lock;
    bool is_period = false;
    // 如果是偶发任务，period为偶发周期
    int period = 1000;
};

struct PeriodicTask
{
    string name;
    int core = 0;
    int priority = 100;
    pair<int, int> time = {0, 0};
    bool is_lock = false;
    string lock;
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

using NodeType = variant<PeriodicTask, APeriodicTask, DistTask, SyncTask>;

#endif