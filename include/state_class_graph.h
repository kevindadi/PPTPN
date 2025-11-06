#ifndef STATE_CLASS_GRAPH_H
#define STATE_CLASS_GRAPH_H

#include "state_class.h"
#include "matrix_ptpn.h"
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <vector>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace state_class {

typedef boost::adjacency_list<
    boost::vecS,         
    boost::vecS,           
    boost::directedS,      
    boost::property<boost::vertex_name_t, StateClass>,      
    boost::property<boost::edge_name_t, TransitionEdge>     
> StateClassGraph;

typedef boost::graph_traits<StateClassGraph>::vertex_descriptor StateClassVertex;
typedef boost::graph_traits<StateClassGraph>::edge_descriptor StateClassEdge;


class StateClassReachabilityGraph {
public:
    explicit StateClassReachabilityGraph(const matrix_ptpn::MatrixPTPN& ptpn);
    
    void set_pruning_enabled(bool enabled) { pruning_enabled_ = enabled; }
    [[nodiscard]] bool is_pruning_enabled() const { return pruning_enabled_; }
    
    size_t build(size_t max_states = std::numeric_limits<size_t>::max());
    
    [[nodiscard]] const StateClassGraph& get_graph() const { return graph_; }
    [[nodiscard]] StateClassGraph& get_graph() { return graph_; }
    
    [[nodiscard]] StateClassVertex get_initial_vertex() const { return initial_vertex_; }
    
    struct Statistics {
        size_t total_states;
        size_t total_transitions;
        size_t enabled_transitions_count;
        size_t pruned_states_count;
        
        Statistics() : total_states(0), total_transitions(0), 
                      enabled_transitions_count(0), pruned_states_count(0) {}
    };
    
    [[nodiscard]] const Statistics& get_statistics() const { return stats_; }
    
    bool save_to_dot(const std::string& file_path) const;  
    bool save_to_json(const std::string& file_path) const;

private:
    const matrix_ptpn::MatrixPTPN& ptpn_;  
    StateClassGraph graph_;                 
    StateClassVertex initial_vertex_;       
    Statistics stats_;                      
    
    StateClass create_initial_state_class();
    
    void explore_successors(const StateClass& current_state,
                           std::set<StateClass>& visited);
    
    bool is_transition_enabled(const StateClass& state, size_t trans_idx) const;
    
    std::pair<int, int> get_transition_time_bounds(const StateClass& state, 
                                                    size_t trans_idx) const;
    
    // 新算法方法
    std::vector<size_t> select_per_core(const std::set<size_t>& enabled) const;
    
    void apply_preemption(const std::vector<size_t>& chosen, StateClass& state) const;
    
    bool maximal_time_elapse(StateClass& state, double& dt) const;
    
    std::tuple<bool, StateClass, double> fire_with_dbm(size_t trans_idx, 
                                                       const StateClass& from_state);
    
    void compute_enabled_and_clocks(StateClass& state);
    
    // 旧方法（保留用于兼容）
    std::pair<DBM, DBM> time_advance(const StateClass& state) const;
    
    bool is_suspended(size_t trans_idx, const std::vector<size_t>& enabled) const;
    
    bool check_dbm_time_intersection(const DBM& z1, size_t trans_idx) const;
    
    /**
     * 限制DBM以反映变迁的触发时间窗口
     * @param z DBM
     * @param trans_idx 变迁索引
     * @return 限制后的DBM
     */
    DBM restrict_for_firing(const DBM& z, size_t trans_idx) const;
    
    double compute_firing_time(const DBM& z1_up, size_t trans_idx) const;
    
    StateClass canonicalize(const StateClass& state) const;
    
    void recompute_suspension(StateClass& state) const;
    
    DBM get_invariants_for(const std::vector<int>& marking) const;
    
    StateClass fire_transition(const StateClass& state, size_t trans_idx, 
                               double firing_time);
    
    void update_dbm_constraints(StateClass& state);
    
    bool should_prune(const StateClass& state, 
                     const std::set<StateClass>& visited) const;
    
    StateClassVertex find_or_add_vertex(const StateClass& state);
    
    static std::string format_marking(const std::vector<int>& marking); 
    std::string format_transitions(const std::set<size_t>& trans_indices, bool detailed = true) const;
    
    // 辅助函数：格式化库所信息（带名称）
    std::string format_places(const std::vector<int>& marking) const;
    
    // 辅助函数：详细输出状态类信息
    void log_state_class_details(const StateClass& state, const std::string& prefix = "") const;
    
    std::map<StateClass, StateClassVertex> state_to_vertex_;
    size_t next_state_id_;
    bool pruning_enabled_ = false;  // 是否启用剪枝，默认false
};

} // namespace state_class

#endif // STATE_CLASS_GRAPH_H

