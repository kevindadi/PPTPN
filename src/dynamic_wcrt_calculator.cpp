#include "wcrt_calculator.h"
#include "priority_state_class.h"
#include "priority_state_graph.h"
#include <boost/graph/graph_traits.hpp>
#include <boost/log/trivial.hpp>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace task_analysis {

// 首先声明所有模板特化
template<>
int DynamicWCRTCalculator<priority_scg::StateClassGraph, priority_scg::SCGVertex>::compute_execution_time_in_state_graph(
    const std::string& task_name, const std::string& start_node, const std::string& end_node) const;

// DynamicWCRTCalculator 模板特化实现

template<>
int DynamicWCRTCalculator<priority_scg::StateClassGraph, priority_scg::SCGVertex>::calculate_wcrt(const std::string& task_name) {
    BOOST_LOG_TRIVIAL(debug) << "[DYNAMIC WCRT] 开始计算任务 " << task_name << " 的WCRT";
    
    try {
        return compute_execution_time_in_state_graph(task_name, "entry", "exit");
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[DYNAMIC WCRT] 计算任务 " << task_name << " WCRT时出错: " << e.what();
        return -1;
    }
}

template<>
int DynamicWCRTCalculator<priority_scg::StateClassGraph, priority_scg::SCGVertex>::calculate_wcet(const std::string& task_name) {
    BOOST_LOG_TRIVIAL(debug) << "[DYNAMIC WCRT] 开始计算任务 " << task_name << " 的WCET";
    
    try {
        return compute_execution_time_in_state_graph(task_name, "ready", "exit");
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[DYNAMIC WCRT] 计算任务 " << task_name << " WCET时出错: " << e.what();
        return -1;
    }
}

template<>
TaskPathInfo<priority_scg::SCGVertex> DynamicWCRTCalculator<priority_scg::StateClassGraph, priority_scg::SCGVertex>::get_task_path_info(const std::string& task_name) {
    TaskPathInfo<priority_scg::SCGVertex> path_info(task_name);
    
    // 在Petri网中查找任务的关键节点
    boost::graph_traits<ptpn::PriorityTPNGraph>::vertex_iterator vi, vi_end;
    for (boost::tie(vi, vi_end) = boost::vertices(petri_net_); vi != vi_end; ++vi) {
        const auto &vertex = petri_net_[*vi];
        
        if (vertex.name == task_name + "entry") {
            path_info.entry_vertex = *vi;
        } else if (vertex.name == task_name + "ready") {
            path_info.ready_vertex = *vi;
        } else if (vertex.name == task_name + "exec") {
            path_info.exec_vertex = *vi;
        } else if (vertex.name == task_name + "exit") {
            path_info.exit_vertex = *vi;
        }
    }
    
    return path_info;
}

template<>
int DynamicWCRTCalculator<priority_scg::StateClassGraph, priority_scg::SCGVertex>::compute_execution_time_in_state_graph(
    const std::string& task_name,
    const std::string& start_node_type,
    const std::string& end_node_type) const {
    
    std::unordered_map<priority_scg::SCGVertex, int> vertex_times;
    std::queue<std::pair<priority_scg::SCGVertex, int>> bfs_queue;
    std::unordered_set<priority_scg::SCGVertex> visited;
    
    // 从初始状态开始BFS
    priority_scg::SCGVertex initial_vertex = 0;
    vertex_times[initial_vertex] = 0;
    bfs_queue.push({initial_vertex, 0});
    visited.insert(initial_vertex);
    
    int max_time = 0;
    
    while (!bfs_queue.empty()) {
        auto [current_vertex, current_time] = bfs_queue.front();
        bfs_queue.pop();
        
        const priority_scg::PriorityStateClass& state = *state_graph_[current_vertex].state;
        
        // 检查当前状态是否包含任务的起始和结束状态
        bool has_start_node = false;
        bool has_end_node = false;
        
        // 检查标记中是否包含任务的起始和结束库所
        for (const auto& [place, tokens] : state.get_marking()) {
            if (tokens > 0 && place < boost::num_vertices(petri_net_)) {
                const auto& vertex = petri_net_[place];
                if (vertex.name == task_name + start_node_type) {
                    has_start_node = true;
                } else if (vertex.name == task_name + end_node_type) {
                    has_end_node = true;
                }
            }
        }
        
        // 如果找到结束状态，更新最大时间
        if (has_end_node && has_start_node) {
            max_time = std::max(max_time, current_time);
        }
        
        // 继续遍历后继状态
        boost::graph_traits<priority_scg::StateClassGraph>::out_edge_iterator ei, ei_end;
        for (boost::tie(ei, ei_end) = boost::out_edges(current_vertex, state_graph_); ei != ei_end; ++ei) {
            priority_scg::SCGVertex target_vertex = boost::target(*ei, state_graph_);
            const auto& edge = state_graph_[*ei];
            
            // 计算到目标状态的时间（使用边的时间区间上界）
            int edge_time = edge.time_interval.upper;
            int new_time = current_time + edge_time;
            
            if (visited.find(target_vertex) == visited.end() || 
                vertex_times[target_vertex] < new_time) {
                vertex_times[target_vertex] = new_time;
                visited.insert(target_vertex);
                bfs_queue.push({target_vertex, new_time});
            }
        }
    }
    
    return max_time;
}

} // namespace task_analysis
