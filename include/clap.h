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
#include <include/config.h>

using namespace boost;
namespace logging = boost::log;

using namespace boost;

typedef property<graph_name_t, std::string> TDG_RAP_P;
typedef adjacency_list<vecS, vecS, directedS,
                       DAGVertex, DAGEdge, TDG_RAP_P>
    TDG_RAP;

class TDG
{
public:
    TDG() = default;
    TDG(string);
    // ~TDGRAP();

public:
    boost::dynamic_properties tdg_dp;
    // TDG-RAP文件路径
    string tdg_file;
    // 所有任务的集合
    std::vector<NodeType> all_task;
    // 周期任务的开始任务和结束任务
    vector<pair<string, string>> period_task;

    // 保存每个任务的转换为Petri网的开始和结束库所

    // 任务使用的锁集合
    set<string> lock_set;
    // 任务分配核心数
    int core;

public:
    void prase_tdg();
};

#endif // PPTPN_GCONFIG_H
