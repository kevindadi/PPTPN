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
    
    std::pair<DBM, DBM> time_advance(const StateClass& state) const;
    
    bool is_suspended(size_t trans_idx, const std::vector<size_t>& enabled) const;
    
    bool check_dbm_time_intersection(const DBM& z1, size_t trans_idx) const;
    
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
    
    std::map<StateClass, StateClassVertex> state_to_vertex_;
    size_t next_state_id_;
};

} // namespace state_class

#endif // STATE_CLASS_GRAPH_H

