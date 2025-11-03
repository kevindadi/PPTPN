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
        void generate_state_class_graph_with_limit(size_t max_states = 100);
        std::shared_ptr<PriorityStateClass> get_initial_state_class();

        [[nodiscard]] const StateClassGraph &get_graph() const { return graph; }
        [[nodiscard]] std::size_t get_vertex_count() const;
        [[nodiscard]] std::size_t get_edge_count() const;

        [[nodiscard]] bool save_to_dot(const std::string &filename) const;
        [[nodiscard]] bool save_to_json(const std::string &filename) const;
        void print_graph_info() const;
        [[nodiscard]] bool has_deadlock() const;

        // 获取可达性树的最大深度
        [[nodiscard]] int get_max_depth() const;

        // 注意:WCRT/WCET分析和死锁检测功能已重构到独立的模块中
        // 请使用 task_analysis 命名空间中的相关类

    private:
        SCGVertex add_state(const PriorityStateClass &state);
        SCGEdge add_edge(SCGVertex from, SCGVertex to, ptpn_v_desc transition, const TimeInterval &interval);

        std::vector<ptpn_v_desc> compute_enabled_transitions(const PriorityStateClass &state);
        std::vector<ptpn_v_desc> filter_by_priority(const std::vector<ptpn_v_desc> &enabled_transitions);

        TimeInterval compute_time_interval(const PriorityStateClass &state, ptpn_v_desc transition);
        TimeInterval compute_common_firing_interval(
            const std::map<ptpn_v_desc, TimeInterval> &transition_intervals);
            
        PriorityStateClass fire_transition(const PriorityStateClass &state, ptpn_v_desc transition, const TimeInterval &common_interval);

        // 找到状态在图中对应的顶点描述符
        [[nodiscard]] SCGVertex find_state_vertex(const PriorityStateClass &state) const;

        // 重置 Petri 网到指定的标记状态
        void reset_petri_net(const PriorityStateClass &state);


        PriorityTPNGraph petri_net;                                 
        StateClassGraph graph;                                      
        // 使用状态对象作为key，避免哈希碰撞导致的状态丢失
        std::unordered_map<PriorityStateClass, SCGVertex, PriorityStateClassHash<int>> state_vertex_map;
        Marking initial_marking;                                    
    };

} // namespace priority_scg

#endif