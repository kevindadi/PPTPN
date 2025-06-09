#ifndef PPTPN_INCLUDE_PRIORITY_STATE_GRAPH_H
#define PPTPN_INCLUDE_PRIORITY_STATE_GRAPH_H

#include "priority_state_class.h"
#include <unordered_map>
#include <memory>

namespace priority_scg
{
    struct SCGVertexProperties
    {
        std::string id;
        std::shared_ptr<PriorityStateClass> state;
        std::string label;
    };

    struct SCGEdgeProperties
    {
        ptpn_v_desc transition;
        std::string xlabel;
        TimeInterval time_interval;
    };

    struct SCGProperties
    {
        std::string name;
    };

    typedef adjacency_list<vecS, vecS, directedS,
        SCGVertexProperties, SCGEdgeProperties, SCGProperties>
        StateClassGraph;

    typedef graph_traits<StateClassGraph>::vertex_descriptor SCGVertex;
    typedef graph_traits<StateClassGraph>::edge_descriptor SCGEdge;

    class PriorityStateClassGraph
    {
    public:
        explicit PriorityStateClassGraph(const PriorityTPNGraph &petri_net);

        void generate_state_class_graph();
        std::shared_ptr<PriorityStateClass> get_initial_state_class();

        [[nodiscard]] const StateClassGraph &get_graph() const { return graph; }
        [[nodiscard]] std::size_t get_vertex_count() const;
        [[nodiscard]] std::size_t get_edge_count() const;

        bool save_to_dot(const std::string &filename) const;
        void print_graph_info() const;
        bool has_deadlock() const;

        // 获取可达性树的最大深度
        int get_max_depth() const;

    private:
        SCGVertex add_state(const PriorityStateClass &state);
        SCGEdge add_edge(SCGVertex from, SCGVertex to, ptpn_v_desc transition, const TimeInterval &interval);

        // 计算在当前标记下的使能变迁
        std::vector<ptpn_v_desc> compute_enabled_transitions(const PriorityStateClass &state);

        // 根据优先级过滤使能变迁
        std::vector<ptpn_v_desc> filter_by_priority(const std::vector<ptpn_v_desc> &enabled_transitions);

        // 计算变迁的时间区间
        TimeInterval compute_time_interval(const PriorityStateClass &state, ptpn_v_desc transition);
        TimeInterval compute_common_firing_interval(
            const std::map<ptpn_v_desc, TimeInterval> &transition_intervals);

        // 变迁触发后得到的后继状态
        PriorityStateClass fire_transition(const PriorityStateClass &state, ptpn_v_desc transition, const TimeInterval &common_interval);

        // 找到状态在图中对应的顶点描述符
        SCGVertex find_state_vertex(const PriorityStateClass &state) const;

        // 重置 Petri 网到指定的标记状态
        void reset_petri_net(const PriorityStateClass &state);

        PriorityTPNGraph petri_net;                                  // 原始的优先级时间 Petri 网
        StateClassGraph graph;                                       // 状态类图
        std::unordered_map<std::size_t, SCGVertex> state_vertex_map; // 状态哈希值到顶点的映射
    };

} // namespace priority_scg

#endif // PPTPN_INCLUDE_PRIORITY_STATE_GRAPH_H