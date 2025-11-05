#include "state_class_graph.h"
#include <algorithm>
#include <queue>
#include <map>
#include <set>
#include <fstream>
#include <iomanip>

namespace state_class {

StateClassReachabilityGraph::StateClassReachabilityGraph(const matrix_ptpn::MatrixPTPN& ptpn)
    : ptpn_(ptpn), next_state_id_(0) {
}

size_t StateClassReachabilityGraph::build(size_t max_states) {
    stats_ = Statistics();
    state_to_vertex_.clear();
    
    StateClass init_state = create_initial_state_class();
    StateClass canonical_init = canonicalize(init_state);
    
    std::queue<StateClass> queue;
    std::set<StateClass> seen;
    
    queue.push(init_state);
    seen.insert(canonical_init);
    
    StateClassVertex init_vertex = find_or_add_vertex(init_state);
    initial_vertex_ = init_vertex;
    stats_.total_states++;
    
    while (!queue.empty() && stats_.total_states < max_states) {
        StateClass state = queue.front();
        queue.pop();
        
        StateClassVertex current_vertex = find_or_add_vertex(state);
        
        // 1. Time advance（得到可以到达的时间封闭区间）
        auto [z1_up, z2_up] = time_advance(state);
        
        if (z1_up.is_empty()) {
            stats_.pruned_states_count++;
            continue;  // Z1_up 为空，无法继续
        }
        
        // 2. 从当前 DBM 中找出所有"可触发"的变迁集合
        std::vector<size_t> enabled;
        for (size_t t = 0; t < ptpn_.num_transitions(); ++t) {
            if (matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, t)) {
                enabled.push_back(t);
            }
        }
        stats_.enabled_transitions_count += enabled.size();
        
        // 对每个 enabled 变迁 t，进一步判断是否被挂起
        for (size_t t : enabled) {
            // 检查是否被挂起
            if (is_suspended(t, enabled)) {
                continue;  // 被挂起，不能触发
            }
            
            // 3. 计算可触发的时间窗口（交集 Z1_up 或 Z2_up 与 [alpha(t), beta(t)]）
            const auto& transition = ptpn_.get_transition(t);
            bool has_intersection = false;
            
            if (transition.suspendable) {
                // 可挂起变迁：检查 Z2
                has_intersection = check_dbm_time_intersection(z2_up, t);
            } else {
                // 不可挂起变迁：检查 Z1
                has_intersection = check_dbm_time_intersection(z1_up, t);
            }
            
            if (!has_intersection) {
                stats_.pruned_states_count++;
                continue;  // 交集为空
            }
            
            // 4. 选择 firing time（通常取最小合法时间）
            double tau = transition.suspendable ? 
                         compute_firing_time(z2_up, t) : 
                         compute_firing_time(z1_up, t);
            
            // 5. fire 并构造后继状态
            StateClass succ = fire_transition(state, t, tau);
            
            if (succ.marking.empty()) {
                stats_.pruned_states_count++;
                continue;  // 无效状态
            }
            
            // 6. 重新计算挂起/恢复并 canonicalize
            recompute_suspension(succ);
            StateClass canonical = canonicalize(succ);
            
            if (seen.find(canonical) == seen.end()) {
                // 新状态，添加到图和队列
                StateClassVertex succ_vertex = find_or_add_vertex(succ);
                TransitionEdge edge(static_cast<int>(t), tau);
                boost::add_edge(current_vertex, succ_vertex, edge, graph_);
                
                seen.insert(canonical);
                queue.push(succ);
                stats_.total_states++;
                stats_.total_transitions++;
            } else {
                // 已存在的状态，只添加边
                // 找到已存在的规范化状态对应的顶点
                StateClassVertex succ_vertex = find_or_add_vertex(succ);
                TransitionEdge edge(static_cast<int>(t), tau);
                boost::add_edge(current_vertex, succ_vertex, edge, graph_);
                stats_.total_transitions++;
            }
        }
    }
    
    return stats_.total_states;
}

StateClass StateClassReachabilityGraph::create_initial_state_class() {
    StateClass initial;
    initial.marking = ptpn_.get_marking();
    initial.state_id = next_state_id_++;
    initial.cumulative_time = 0.0;

    size_t num_transitions = ptpn_.num_transitions();
    
    initial.Z1.resize(num_transitions + 1);  // +1 为参考时钟
    initial.Z2.resize(num_transitions + 1);

    update_dbm_constraints(initial);
    
    return initial;
}

void StateClassReachabilityGraph::explore_successors(const StateClass& current_state,
                                                     std::set<StateClass>& visited) {
}

bool StateClassReachabilityGraph::is_transition_enabled(const StateClass& state, 
                                                          size_t trans_idx) const {
    return matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, trans_idx);
}

std::pair<int, int> StateClassReachabilityGraph::get_transition_time_bounds(
    const StateClass& state, size_t trans_idx) const {
    
    const auto& transition = ptpn_.get_transition(trans_idx);
    int earliest = transition.time_interval.earliest;
    int latest = transition.time_interval.latest;
    
    // 考虑 DBM 约束
    // 这里简化处理：直接使用变迁的时间区间
    // 实际应用中需要结合 Z1 和 Z2 的约束
    
    return {earliest, latest};
}

std::pair<DBM, DBM> StateClassReachabilityGraph::time_advance(const StateClass& state) const {
    // 时间推进：Z1 和 Z2 可以同步增长
    // 对于不可挂起变迁，时间推进意味着时钟可以无限增长（相对于参考时钟）
    // 对于可挂起变迁，时间推进不受限制
    
    DBM z1_up = state.Z1;
    DBM z2_up = state.Z2;
    
    // 获取不变量约束
    DBM invariants = get_invariants_for(state.marking);
    
    // 时间推进：放宽所有上界约束（相对于参考时钟）
    // x_i - x_0 <= INF 保持不变
    // 实际上，时间推进允许所有时钟同步增长
    // 我们需要确保 Z1 与不变量一致
    
    // 计算 Z1 与不变量的交集
    if (invariants.size() > 0 && z1_up.size() == invariants.size()) {
        z1_up = z1_up.intersection(invariants);
    }
    
    z1_up.minimize();
    z2_up.minimize();
    
    return {z1_up, z2_up};
}

bool StateClassReachabilityGraph::is_suspended(size_t trans_idx, 
                                               const std::vector<size_t>& enabled) const {
    const auto& transition = ptpn_.get_transition(trans_idx);
    
    // 只有可挂起变迁才能被挂起
    if (!transition.suspendable) {
        return false;
    }
    
    // 检查同一核心上是否有更高优先级的不可挂起变迁使能
    int trans_core = transition.core;
    int trans_priority = transition.priority;
    
    for (size_t other_t : enabled) {
        if (other_t == trans_idx) continue;
        
        const auto& other_trans = ptpn_.get_transition(other_t);
        
        // 同一核心，不可挂起，且优先级更高（数值更小）
        if (other_trans.core == trans_core && 
            !other_trans.suspendable && 
            other_trans.priority < trans_priority) {
            return true;  // 被挂起
        }
    }
    
    return false;
}

bool StateClassReachabilityGraph::check_dbm_time_intersection(const DBM& z1, 
                                                              size_t trans_idx) const {
    const auto& transition = ptpn_.get_transition(trans_idx);
    
    // 检查 Z1 中的时钟约束是否与 [alpha(t), beta(t)] 有交集
    size_t clock_idx = trans_idx + 1;  // +1 因为索引 0 是参考时钟
    
    if (clock_idx >= z1.size()) {
        return false;
    }
    
    int alpha = transition.time_interval.earliest;
    int beta = transition.time_interval.latest == matrix_ptpn::INF ? 
               INF_TIME : transition.time_interval.latest;
    
    // 从 DBM 中获取 x_clock - x_0 的约束
    // z1[clock_idx][0] 表示 x_clock - x_0 <= ?
    // z1[0][clock_idx] 表示 x_0 - x_clock <= ?，即 x_clock - x_0 >= -?
    
    int dbm_upper = z1.get_constraint(clock_idx, 0);  // x_clock - x_0 <= dbm_upper
    int dbm_lower = -z1.get_constraint(0, clock_idx);  // x_clock - x_0 >= dbm_lower
    
    // 检查交集：[alpha, beta] 与 [dbm_lower, dbm_upper] 是否有交集
    int intersect_lower = std::max(alpha, dbm_lower);
    int intersect_upper = std::min(beta, dbm_upper == INF_TIME ? beta : dbm_upper);
    
    if (beta == INF_TIME) {
        // beta 为无穷，只要 intersect_lower <= dbm_upper 或 dbm_upper 为无穷
        return intersect_lower <= dbm_upper || dbm_upper == INF_TIME;
    }
    
    return intersect_lower <= intersect_upper;
}

double StateClassReachabilityGraph::compute_firing_time(const DBM& z1_up, 
                                                        size_t trans_idx) const {
    const auto& transition = ptpn_.get_transition(trans_idx);
    
    size_t clock_idx = trans_idx + 1;
    if (clock_idx >= z1_up.size()) {
        return static_cast<double>(transition.time_interval.earliest);
    }
    
    int alpha = transition.time_interval.earliest;
    int beta = transition.time_interval.latest == matrix_ptpn::INF ? 
               INF_TIME : transition.time_interval.latest;
    
    // 从 DBM 中获取下界
    int dbm_lower = -z1_up.get_constraint(0, clock_idx);  // x_clock - x_0 >= dbm_lower
    
    // 取满足下界的最大时间（alpha 和 dbm_lower 的最大值）
    int firing_time_int = std::max(alpha, dbm_lower);
    
    // 确保不超过上界
    if (beta != INF_TIME && firing_time_int > beta) {
        firing_time_int = beta;
    }
    
    return static_cast<double>(firing_time_int);
}

StateClass StateClassReachabilityGraph::canonicalize(const StateClass& state) const {
    StateClass canonical = state;
    
    // 规范化 DBM：最小化
    canonical.Z1.minimize();
    canonical.Z2.minimize();
    
    return canonical;
}

void StateClassReachabilityGraph::recompute_suspension(StateClass& state) const {
    // 重新计算使能变迁
    std::vector<size_t> enabled;
    for (size_t t = 0; t < ptpn_.num_transitions(); ++t) {
        if (matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, t)) {
            enabled.push_back(t);
        }
    }
    
    StateClass temp_state = state;
    const_cast<StateClassReachabilityGraph*>(this)->update_dbm_constraints(temp_state);
    state = temp_state;
}

DBM StateClassReachabilityGraph::get_invariants_for(const std::vector<int>& marking) const {
    size_t num_transitions = ptpn_.num_transitions();
    DBM invariants(num_transitions + 1);
    
    return invariants;      
}

StateClass StateClassReachabilityGraph::fire_transition(const StateClass& state, 
                                                         size_t trans_idx, 
                                                         double firing_time) {
    StateClass new_state = state;
    
    new_state.marking = matrix_ptpn::MatrixPTPN::fire(state.marking, ptpn_, trans_idx);
    new_state.cumulative_time = firing_time;
    
    const auto& transition = ptpn_.get_transition(trans_idx);
    size_t clock_idx = trans_idx + 1;  // +1 因为索引 0 是参考时钟
    
    if (transition.suspendable) {
        new_state.Z2.reset_clock(clock_idx);
    } else {
        new_state.Z1.reset_clock(clock_idx);
    }
    
    update_dbm_constraints(new_state);
    
    return new_state;
}

void StateClassReachabilityGraph::update_dbm_constraints(StateClass& state) {
    size_t num_transitions = ptpn_.num_transitions();
    
    std::vector<size_t> enabled;
    for (size_t t = 0; t < ptpn_.num_transitions(); ++t) {
        if (matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, t)) {
            enabled.push_back(t);
        }
    }
    
    for (size_t trans_idx : enabled) {
        const auto& transition = ptpn_.get_transition(trans_idx);
        size_t clock_idx = trans_idx + 1;  
        
        if (transition.suspendable) {
            if (transition.time_interval.earliest > 0) {
                state.Z2.set_constraint(0, clock_idx, -transition.time_interval.earliest);
            }
            if (transition.time_interval.latest != matrix_ptpn::INF) {
                state.Z2.set_constraint(clock_idx, 0, transition.time_interval.latest);
            }
        } else {
            if (transition.time_interval.earliest > 0) {
                state.Z1.set_constraint(0, clock_idx, -transition.time_interval.earliest);
            }
            if (transition.time_interval.latest != matrix_ptpn::INF) {
                state.Z1.set_constraint(clock_idx, 0, transition.time_interval.latest);
            }
        }
    }
    
    state.Z1.minimize();
    state.Z2.minimize();
}

bool StateClassReachabilityGraph::should_prune(const StateClass& state, 
                                               const std::set<StateClass>& visited) const {
    if (state.Z1.is_empty() || state.Z2.is_empty()) {
        return true;
    }
    
    return visited.find(state) != visited.end();
}

StateClassVertex StateClassReachabilityGraph::find_or_add_vertex(const StateClass& state) {
    auto it = state_to_vertex_.find(state);
    if (it != state_to_vertex_.end()) {
        return it->second;
    }
    
    StateClass new_state = state;
    new_state.state_id = next_state_id_++;
    StateClassVertex v = boost::add_vertex(new_state, graph_);
    state_to_vertex_[new_state] = v;
    return v;
}

bool StateClassReachabilityGraph::save_to_dot(const std::string& file_path) const {
    try {
        std::ofstream out(file_path);
        if (!out.is_open()) {
            return false;
        }
        
        out << "digraph StateClassGraph {\n";
        out << "  rankdir=LR;\n";
        out << "  node [shape=box];\n\n";
        
        typedef boost::graph_traits<StateClassGraph>::vertex_iterator StateClassVertexIterator;
        StateClassVertexIterator vi, vi_end;
        for (std::tie(vi, vi_end) = boost::vertices(graph_); vi != vi_end; ++vi) {
            const StateClass& state = boost::get(boost::vertex_name, graph_, *vi);
            out << "  s" << state.state_id << " [label=\"";
            out << "State " << state.state_id << "\\n";
            out << "M: [";
            for (size_t i = 0; i < state.marking.size(); ++i) {
                if (i > 0) out << ", ";
                out << state.marking[i];
            }
            out << "]\\n";
            out << "Time: " << state.cumulative_time;
            out << "\"];\n";
        }
        
        out << "\n";
        
        typedef boost::graph_traits<StateClassGraph>::edge_iterator StateClassEdgeIterator;
        StateClassEdgeIterator ei, ei_end;
        for (std::tie(ei, ei_end) = boost::edges(graph_); ei != ei_end; ++ei) {
            StateClassVertex src = boost::source(*ei, graph_);
            StateClassVertex tgt = boost::target(*ei, graph_);
            const TransitionEdge& edge = boost::get(boost::edge_name, graph_, *ei);
            const StateClass& src_state = boost::get(boost::vertex_name, graph_, src);
            const StateClass& tgt_state = boost::get(boost::vertex_name, graph_, tgt);
            
            out << "  s" << src_state.state_id << " -> s" << tgt_state.state_id;
            out << " [label=\"T" << edge.transition_id << "\\n@" << edge.firing_time << "\"];\n";
        }
        
        out << "}\n";
        out.close();
        
        return true;
    } catch (...) {
        return false;
    }
}

bool StateClassReachabilityGraph::save_to_json(const std::string& file_path) const {
    try {
        std::ofstream out(file_path);
        if (!out.is_open()) {
            return false;
        }
        
        out << "{\n";
        out << "  \"states\": [\n";
        
        typedef boost::graph_traits<StateClassGraph>::vertex_iterator StateClassVertexIterator;
        StateClassVertexIterator vi, vi_end;
        bool first_state = true;
        for (std::tie(vi, vi_end) = boost::vertices(graph_); vi != vi_end; ++vi) {
            const StateClass& state = boost::get(boost::vertex_name, graph_, *vi);
            if (!first_state) out << ",\n";
            first_state = false;
            
            out << "    {\n";
            out << "      \"id\": " << state.state_id << ",\n";
            out << "      \"marking\": [";
            for (size_t i = 0; i < state.marking.size(); ++i) {
                if (i > 0) out << ", ";
                out << state.marking[i];
            }
            out << "],\n";
            out << "      \"cumulative_time\": " << std::fixed << std::setprecision(2) 
                << state.cumulative_time << "\n";
            out << "    }";
        }
        
        out << "\n  ],\n";
        out << "  \"transitions\": [\n";
        
        typedef boost::graph_traits<StateClassGraph>::edge_iterator StateClassEdgeIterator;
        StateClassEdgeIterator ei, ei_end;
        bool first_trans = true;
        for (std::tie(ei, ei_end) = boost::edges(graph_); ei != ei_end; ++ei) {
            StateClassVertex src = boost::source(*ei, graph_);
            StateClassVertex tgt = boost::target(*ei, graph_);
            const TransitionEdge& edge = boost::get(boost::edge_name, graph_, *ei);
            const StateClass& src_state = boost::get(boost::vertex_name, graph_, src);
            const StateClass& tgt_state = boost::get(boost::vertex_name, graph_, tgt);
            
            if (!first_trans) out << ",\n";
            first_trans = false;
            
            out << "    {\n";
            out << "      \"source\": " << src_state.state_id << ",\n";
            out << "      \"target\": " << tgt_state.state_id << ",\n";
            out << "      \"transition_id\": " << edge.transition_id << ",\n";
            out << "      \"firing_time\": " << std::fixed << std::setprecision(2) 
                << edge.firing_time << "\n";
            out << "    }";
        }
        
        out << "\n  ],\n";
        out << "  \"statistics\": {\n";
        out << "    \"total_states\": " << stats_.total_states << ",\n";
        out << "    \"total_transitions\": " << stats_.total_transitions << ",\n";
        out << "    \"enabled_transitions_count\": " << stats_.enabled_transitions_count << ",\n";
        out << "    \"pruned_states_count\": " << stats_.pruned_states_count << "\n";
        out << "  }\n";
        out << "}\n";
        
        out.close();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace state_class

