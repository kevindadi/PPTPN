#ifndef CLAP
#define CLAP

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/graph_utility.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <set>
#include <vector>
#include "dag.h"

using namespace boost;
namespace logging = boost::log;

using namespace boost;

typedef property<graph_name_t, std::string> TDG_RAP_P;
typedef adjacency_list<vecS, vecS, directedS,
                       DAGVertex, DAGEdge, TDG_RAP_P>
    TDG_RAP;

// 任务类型, 后续扩展
enum TaskType {
  Task, InterTask,
};
// 节点类型的枚举，区别于结构体枚举，仅为后续区分, TASK包含周期任务和一般任务
enum VertexType {
  TASK, INTERRUPT, SYNC, DIST, EMPTY
};
// 边的枚举, 不同节点类型
enum EdgeType {

};
// TDG结构体，包含DAG图中所有信息
class TDG
{
public:
    TDG() = default;
    TDG(string);
    // ~TDGRAP();
    TDG_RAP tdg;
  boost::dynamic_properties tdg_dp;

public:
    // TDG-RAP文件路径
    string tdg_file;
    // 所有任务的集合
    vector<NodeType> all_task;
    // 所有任务的优先级
    std::unordered_map<string, int> task_priority;
    // 周期任务的开始任务和结束任务, 以及时间周期
    vector<std::tuple<string, string, int>> period_task;

    // 每个节点的名字和类型映射
    std::unordered_map<string, VertexType> vertexes_type;
    // 每个节点的名字和其属性的映射
    std::unordered_map<string, NodeType> nodes_type;
    // 任务使用的锁集合
    set<string> lock_set;
    // 任务分配核心数
    int core;

public:
    void parse_tdg();

    // 解析 vertex 的 label 属性
    NodeType parse_vertex_label(const string& label);
    // 解析 time 数组中的每个时间区间
    static vector<int> parse_time_vec(string times);

    std::unordered_map<int, vector<string>> classify_priority();
};

#endif // PPTPN_GCONFIG_H