#ifndef PPTPN_INCLUDE_PRIORITY_STATE_CLASS_H
#define PPTPN_INCLUDE_PRIORITY_STATE_CLASS_H

#include "priority_time_petri_net.h"
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/graph_utility.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace boost;
using namespace boost::multiprecision;
using namespace ptpn;

namespace priority_scg
{
    struct TimeInterval
    {
        int lower; // 下界
        int upper; // 上界，如果是无穷大则使用INT_MAX表示

        TimeInterval(int lower = 0, int upper = INT_MAX) : lower(lower), upper(upper) {}

        TimeInterval intersect(const TimeInterval &other) const
        {
            return TimeInterval(std::max(lower, other.lower), std::min(upper, other.upper));
        }

        bool is_valid() const { return lower <= upper; }

        bool operator==(const TimeInterval &other) const
        {
            return lower == other.lower && upper == other.upper;
        }

        std::string to_string() const
        {
            return "[" + std::to_string(lower) + ", " + std::to_string(upper) + "]";
        }
    };

    // 代表变迁的时间约束
    struct TransitionTimeConstraint
    {
        ptpn_v_desc transition;     // 变迁标识符
        TimeInterval time_interval; // 时间区间
        int priority;               // 优先级
        int cpu;                    // cpu

        TransitionTimeConstraint(ptpn_v_desc t, TimeInterval interval, int prio, int c)
            : transition(t), time_interval(interval), priority(prio), cpu(c) {}

        bool operator==(const TransitionTimeConstraint &other) const
        {
            return transition == other.transition &&
                   time_interval == other.time_interval &&
                   priority == other.priority &&
                   cpu == other.cpu;
        }
    };

    // 标记（Marking）表示Petri网中库所的token分布
    using Marking = std::map<ptpn_v_desc, int>; // 库所 -> token数量

    // 优先级时间 Petri 网的状态类
    class PriorityStateClass
    {
    public:
        struct PriorityFilterResult
        {
            std::vector<ptpn_v_desc> enabled_transitions;   // 保留的高优先级变迁
            std::vector<ptpn_v_desc> suspended_transitions; // 被挂起的低优先级变迁
        };

        PriorityStateClass() = default;
        PriorityStateClass(const Marking &m, const std::vector<TransitionTimeConstraint> &ttc)
            : marking(m), time_constraints(ttc) {}

        // 状态类的等价性判断
        bool operator==(const PriorityStateClass &other) const;

        // 获取状态类的字符串表示（用于输出和调试）
        std::string to_string() const;

        // 获取标记的字符串表示（排除标记中所有小于等于零的值）
        std::string marking_to_string() const;

        // 获取当前标记中的所有可启用变迁
        std::vector<ptpn_v_desc> get_enabled_transitions(const PriorityTPNGraph &graph) const;

        // 考虑优先级规则过滤可启用的变迁
        PriorityFilterResult filter_by_priority(
            const std::vector<ptpn_v_desc> &enabled_transitions,
            const PriorityTPNGraph &graph) const;

        std::map<ptpn_v_desc, TimeInterval> suspended_transitions_clocks; // 挂起变迁的时钟信息

        // 获取可调度变迁（基于时间约束能够触发的变迁）
        std::vector<ptpn_v_desc> get_schedulable_transitions(
            const std::vector<ptpn_v_desc> &enabled_transitions,
            const PriorityTPNGraph &graph,
            TimeInterval &common_interval) const;

        // 计算后继状态类
        std::shared_ptr<PriorityStateClass> compute_successor(
            const PriorityTPNGraph &graph,
            ptpn_v_desc fired_transition) const;

        // 标记变迁为挂起状态
        void mark_suspended(ptpn_v_desc transition, const TimeInterval &clock_time);

        TimeInterval get_suspended_clock(ptpn_v_desc transition) const;
        // 检查变迁是否被挂起
        bool is_suspended(ptpn_v_desc transition) const;

        // 获取时间约束
        const std::vector<TransitionTimeConstraint> &get_time_constraints() const;

        Marking marking;
        std::vector<TransitionTimeConstraint> time_constraints; // 时间约束

    private:
        std::set<ptpn_v_desc> suspended_transitions; // 挂起的变迁集合
    };

    // 状态类图的顶点属性
    struct SCGVertexProperties
    {
        std::string id;
        std::shared_ptr<PriorityStateClass> state;
        std::string label;
    };

    // 状态类图的边属性
    struct SCGEdgeProperties
    {
        ptpn_v_desc transition;
        std::string xlabel;
        TimeInterval time_interval;
    };

    // 状态类图的属性
    struct SCGProperties
    {
        std::string name;
    };

    // 状态类图定义
    typedef boost::adjacency_list<
        boost::vecS, boost::vecS, boost::directedS,
        SCGVertexProperties, SCGEdgeProperties, SCGProperties>
        StateClassGraph;

    typedef boost::graph_traits<StateClassGraph>::vertex_descriptor SCGVertex;
    typedef boost::graph_traits<StateClassGraph>::edge_descriptor SCGEdge;

    // 优先级时间 Petri 网的状态类图分析类
    class PriorityStateClassAnalyzer
    {
    public:
        PriorityStateClassAnalyzer(const PriorityTPNGraph &petri_net);

        // 生成状态类图
        void generate_state_class_graph();

        void export_to_dot(const std::string &filename);

        // 检查是否有死锁状态
        bool has_deadlock_states() const;

        // 获取死锁状态
        std::vector<SCGVertex> get_deadlock_states() const;

        // 计算最大执行时间
        TimeInterval calculate_max_execution_time() const;

        // 检查可达性
        bool is_marking_reachable(const Marking &target_marking) const;

        // 计算WCET
        int calculate_wcet(ptpn_v_desc start_place, ptpn_v_desc end_place) const;

        // 获取所有从开始库所到结束库所的路径
        std::vector<std::vector<SCGVertex>> get_paths_between_places(
            ptpn_v_desc start_place, ptpn_v_desc end_place) const;

        // 获取状态类图
        const StateClassGraph &get_graph() const { return graph; }

    private:
        PriorityTPNGraph petri_net; // 原始的优先级时间 Petri 网
        StateClassGraph graph;      // 状态类图

        // 计算初始状态类
        std::shared_ptr<PriorityStateClass> compute_initial_state();

        // 添加状态类到图中
        SCGVertex add_state(const std::shared_ptr<PriorityStateClass> &state);

        // 添加状态转换（边）到图中
        SCGEdge add_edge(SCGVertex source, SCGVertex target,
                         ptpn_v_desc transition, const TimeInterval &interval);

        // 生成状态标签
        std::string generate_state_label(const std::shared_ptr<PriorityStateClass> &state);

        // 判断变迁是否可启用
        bool is_transition_enabled(ptpn_v_desc transition, const Marking &marking);

        // 更新标记（触发变迁后）
        Marking update_marking(const Marking &current_marking, ptpn_v_desc transition);

        // 更新时间约束（触发变迁后）
        std::vector<TransitionTimeConstraint> update_time_constraints(
            const std::vector<TransitionTimeConstraint> &current_constraints,
            ptpn_v_desc fired_transition,
            const TimeInterval &firing_interval,
            const std::vector<ptpn_v_desc> &new_enabled_transitions);

        // 计算路径的执行时间
        int calculate_path_execution_time(const std::vector<SCGVertex> &path) const;

        // 检查顶点是否包含指定的库所
        bool vertex_contains_place(SCGVertex v, ptpn_v_desc place) const;
    };

} // namespace priority_scg

#endif // PPTPN_INCLUDE_PRIORITY_STATE_CLASS_H