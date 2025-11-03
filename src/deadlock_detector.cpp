#include "deadlock_detector.h"
#include "priority_state_graph.h"
#include "priority_time_petri_net.h"
#include <boost/graph/graph_traits.hpp>
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <sstream>

namespace task_analysis {

// 首先声明所有模板特化
template<>
bool StateGraphDeadlockDetector<priority_scg::StateClassGraph, priority_scg::SCGVertex, priority_scg::PriorityStateClass>::dfs_detect_cycles(
    priority_scg::SCGVertex start_vertex,
    std::unordered_set<priority_scg::SCGVertex>& visited,
    std::unordered_set<priority_scg::SCGVertex>& recursion_stack,
    std::vector<std::string>& deadlock_states) const;

template<>
bool StateGraphDeadlockDetector<priority_scg::StateClassGraph, priority_scg::SCGVertex, priority_scg::PriorityStateClass>::is_deadlock_state(priority_scg::SCGVertex vertex) const;

template<>
bool StateGraphDeadlockDetector<priority_scg::StateClassGraph, priority_scg::SCGVertex, priority_scg::PriorityStateClass>::check_task_deadlock_paths(const std::string& task_name) const;

template<>
std::vector<std::string> StateGraphDeadlockDetector<priority_scg::StateClassGraph, priority_scg::SCGVertex, priority_scg::PriorityStateClass>::build_deadlock_cycles(
    const std::vector<std::string>& deadlock_states) const;

// StateGraphDeadlockDetector 特化实现

template<>
DeadlockAnalysisResult StateGraphDeadlockDetector<priority_scg::StateClassGraph, priority_scg::SCGVertex, priority_scg::PriorityStateClass>::detect_deadlocks() {
    DeadlockAnalysisResult result;
    
    BOOST_LOG_TRIVIAL(info) << "[DEADLOCK DETECTOR] 开始死锁检测";
    
    try {
        std::unordered_set<priority_scg::SCGVertex> visited;
        std::unordered_set<priority_scg::SCGVertex> recursion_stack;
        std::vector<std::string> deadlock_states;
        
        // 从所有未访问的顶点开始DFS
        boost::graph_traits<priority_scg::StateClassGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = boost::vertices(state_graph_); vi != vi_end; ++vi) {
            if (visited.find(*vi) == visited.end()) {
                if (dfs_detect_cycles(*vi, visited, recursion_stack, deadlock_states)) {
                    result.has_deadlock = true;
                }
            }
        }
        
        // 分析死锁原因
        if (result.has_deadlock) {
            result.deadlock_states = deadlock_states;
            result.deadlock_cycles = build_deadlock_cycles(deadlock_states);
            
            if (result.deadlock_reason.empty()) {
                result.deadlock_reason = "发现循环依赖,共涉及 " + std::to_string(deadlock_states.size()) + " 个状态";
            }
        }
        
        BOOST_LOG_TRIVIAL(info) << "[DEADLOCK DETECTOR] 死锁检测完成: " 
                                << (result.has_deadlock ? "发现死锁" : "无死锁");
        
    } catch (const std::exception& e) {
        result.has_deadlock = true;
        result.deadlock_reason = "死锁分析失败: " + std::string(e.what());
        BOOST_LOG_TRIVIAL(error) << "[DEADLOCK DETECTOR] 死锁检测时出错: " << e.what();
    }
    
    return result;
}

template<>
bool StateGraphDeadlockDetector<priority_scg::StateClassGraph, priority_scg::SCGVertex, priority_scg::PriorityStateClass>::has_deadlock(const std::string& task_name) {
    return check_task_deadlock_paths(task_name);
}

template<>
bool StateGraphDeadlockDetector<priority_scg::StateClassGraph, priority_scg::SCGVertex, priority_scg::PriorityStateClass>::dfs_detect_cycles(
    priority_scg::SCGVertex start_vertex,
    std::unordered_set<priority_scg::SCGVertex>& visited,
    std::unordered_set<priority_scg::SCGVertex>& recursion_stack,
    std::vector<std::string>& deadlock_states) const {
    
    if (recursion_stack.find(start_vertex) != recursion_stack.end()) {
        // 发现循环依赖
        deadlock_states.push_back(state_graph_[start_vertex].id);
        return true;
    }
    
    if (visited.find(start_vertex) != visited.end()) {
        return false;
    }
    
    visited.insert(start_vertex);
    recursion_stack.insert(start_vertex);
    
    // 检查当前状态是否为死锁状态
    if (is_deadlock_state(start_vertex)) {
        deadlock_states.push_back(state_graph_[start_vertex].id);
        recursion_stack.erase(start_vertex);
        return true;
    }
    
    // 递归检查后继状态
    bool found_deadlock = false;
    boost::graph_traits<priority_scg::StateClassGraph>::out_edge_iterator ei, ei_end;
    for (boost::tie(ei, ei_end) = boost::out_edges(start_vertex, state_graph_); ei != ei_end; ++ei) {
        priority_scg::SCGVertex target = boost::target(*ei, state_graph_);
        if (dfs_detect_cycles(target, visited, recursion_stack, deadlock_states)) {
            found_deadlock = true;
        }
    }
    
    recursion_stack.erase(start_vertex);
    return found_deadlock;
}

template<>
bool StateGraphDeadlockDetector<priority_scg::StateClassGraph, priority_scg::SCGVertex, priority_scg::PriorityStateClass>::is_deadlock_state(priority_scg::SCGVertex vertex) const {
    const priority_scg::PriorityStateClass& state = *state_graph_[vertex].state;
    
    // 检查当前状态是否为死锁状态
    return state.get_enabled_runtimes().empty() && state.get_suspended_runtimes().empty();
}

template<>
bool StateGraphDeadlockDetector<priority_scg::StateClassGraph, priority_scg::SCGVertex, priority_scg::PriorityStateClass>::check_task_deadlock_paths(const std::string& task_name) const {
    // 使用BFS检查任务相关的死锁路径
    std::queue<priority_scg::SCGVertex> bfs_queue;
    std::unordered_set<priority_scg::SCGVertex> visited;
    
    priority_scg::SCGVertex initial_vertex = 0;
    bfs_queue.push(initial_vertex);
    visited.insert(initial_vertex);
    
    while (!bfs_queue.empty()) {
        priority_scg::SCGVertex current = bfs_queue.front();
        bfs_queue.pop();
        
        const priority_scg::PriorityStateClass& state = *state_graph_[current].state;
        
        // 检查当前状态是否与任务相关且为死锁状态
        bool task_related = false;
        for (const auto& [place, tokens] : state.get_marking()) {
            if (tokens > 0 && place < boost::num_vertices(petri_net_)) {
                const auto& vertex = petri_net_[place];
                if (vertex.name.find(task_name) != std::string::npos) {
                    task_related = true;
                    break;
                }
            }
        }
        
        if (task_related && is_deadlock_state(current)) {
            return true;
        }
        
        // 继续BFS
        boost::graph_traits<priority_scg::StateClassGraph>::out_edge_iterator ei, ei_end;
        for (boost::tie(ei, ei_end) = boost::out_edges(current, state_graph_); ei != ei_end; ++ei) {
            priority_scg::SCGVertex target = boost::target(*ei, state_graph_);
            if (visited.find(target) == visited.end()) {
                visited.insert(target);
                bfs_queue.push(target);
            }
        }
    }
    
    return false;
}

template<>
std::vector<std::string> StateGraphDeadlockDetector<priority_scg::StateClassGraph, priority_scg::SCGVertex, priority_scg::PriorityStateClass>::build_deadlock_cycles(
    const std::vector<std::string>& deadlock_states) const {
    
    std::vector<std::string> cycles;
    
    for (size_t i = 0; i < deadlock_states.size(); ++i) {
        std::string cycle_info = "死锁循环 " + std::to_string(i + 1) + ": " + deadlock_states[i];
        if (i < deadlock_states.size() - 1) {
            cycle_info += " -> " + deadlock_states[i + 1];
        } else {
            cycle_info += " -> " + deadlock_states[0] + " (循环)";
        }
        cycles.push_back(cycle_info);
    }
    
    return cycles;
}

// PetriNetDeadlockDetector 实现

DeadlockAnalysisResult PetriNetDeadlockDetector::detect_deadlocks() {
    DeadlockAnalysisResult result;
    
    BOOST_LOG_TRIVIAL(info) << "[PETRI NET DEADLOCK] 开始Petri网死锁检测";
    
    try {
        // 检测资源死锁
        auto resource_deadlocks = detect_resource_deadlocks();
        
        // 检测循环等待死锁
        auto circular_wait = detect_circular_wait();
        
        // 合并结果
        if (resource_deadlocks.has_deadlock || circular_wait.has_deadlock) {
            result.has_deadlock = true;
            result.deadlock_states.insert(result.deadlock_states.end(),
                                        resource_deadlocks.deadlock_states.begin(),
                                        resource_deadlocks.deadlock_states.end());
            result.deadlock_states.insert(result.deadlock_states.end(),
                                        circular_wait.deadlock_states.begin(),
                                        circular_wait.deadlock_states.end());
            
            if (!resource_deadlocks.deadlock_reason.empty()) {
                result.deadlock_reason += resource_deadlocks.deadlock_reason;
            }
            if (!circular_wait.deadlock_reason.empty()) {
                if (!result.deadlock_reason.empty()) {
                    result.deadlock_reason += "; ";
                }
                result.deadlock_reason += circular_wait.deadlock_reason;
            }
        }
        
    } catch (const std::exception& e) {
        result.has_deadlock = true;
        result.deadlock_reason = "Petri网死锁分析失败: " + std::string(e.what());
        BOOST_LOG_TRIVIAL(error) << "[PETRI NET DEADLOCK] 死锁检测时出错: " << e.what();
    }
    
    return result;
}

bool PetriNetDeadlockDetector::has_deadlock(const std::string& task_name) {
    // 检查任务相关的变迁是否可能死锁
    boost::graph_traits<ptpn::PriorityTPNGraph>::vertex_iterator vi, vi_end;
    for (boost::tie(vi, vi_end) = boost::vertices(petri_net_); vi != vi_end; ++vi) {
        const auto& vertex = petri_net_[*vi];
        
        if (vertex.name.find(task_name) != std::string::npos && vertex.is_transition()) {
            if (is_transition_deadlock_prone(*vi)) {
                return true;
            }
        }
    }
    
    return false;
}

DeadlockAnalysisResult PetriNetDeadlockDetector::detect_resource_deadlocks() const {
    DeadlockAnalysisResult result;
    
    // 简化的资源死锁检测逻辑
    // 这里可以根据具体的Petri网结构实现更复杂的检测
    
    return result;
}

DeadlockAnalysisResult PetriNetDeadlockDetector::detect_circular_wait() const {
    DeadlockAnalysisResult result;
    
    // 简化的循环等待检测逻辑
    // 这里可以根据具体的Petri网结构实现更复杂的检测
    
    return result;
}

bool PetriNetDeadlockDetector::is_transition_deadlock_prone(ptpn::ptpn_v_desc transition) const {
    // 检查变迁是否可能导致死锁
    // 这里实现具体的死锁倾向检测逻辑
    
    return false;
}

// DeadlockDetectorFactory 实现
// create_petri_net_detector 已在头文件中实现,无需重复定义

// DeadlockAnalysisUtils 实现

std::string DeadlockAnalysisUtils::analyze_deadlock_reason(const std::vector<std::string>& deadlock_states,
                                                          const ptpn::PriorityTPNGraph& petri_net) {
    std::stringstream reason;
    reason << "发现 " << deadlock_states.size() << " 个死锁状态: ";
    
    for (size_t i = 0; i < deadlock_states.size(); ++i) {
        if (i > 0) reason << ", ";
        reason << deadlock_states[i];
    }
    
    return reason.str();
}

std::string DeadlockAnalysisUtils::generate_deadlock_report(const DeadlockAnalysisResult& result) {
    std::stringstream report;
    
    report << "死锁分析报告:\n";
    report << "  存在死锁: " << (result.has_deadlock ? "是" : "否") << "\n";
    
    if (result.has_deadlock) {
        report << "  死锁原因: " << result.deadlock_reason << "\n";
        report << "  涉及状态数: " << result.deadlock_states.size() << "\n";
        
        for (const auto& cycle : result.deadlock_cycles) {
            report << "  " << cycle << "\n";
        }
    }
    
    return report.str();
}

int DeadlockAnalysisUtils::assess_deadlock_severity(const DeadlockAnalysisResult& result) {
    if (!result.has_deadlock) {
        return 1; // 无风险
    }
    
    int severity = 2; // 基础风险
    
    // 根据死锁状态数量调整严重程度
    if (result.deadlock_states.size() > 5) {
        severity += 2;
    } else if (result.deadlock_states.size() > 2) {
        severity += 1;
    }
    
    // 根据循环数量调整严重程度
    if (result.deadlock_cycles.size() > 3) {
        severity += 1;
    }
    
    return std::min(severity, 5); // 最大严重程度为5
}

} // namespace task_analysis