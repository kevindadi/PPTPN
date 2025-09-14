#include "wcrt_calculator.h"
#include "priority_time_petri_net.h"
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace task_analysis {

// StaticWCRTCalculator 实现

int StaticWCRTCalculator::calculate_wcrt(const std::string& task_name) {
    BOOST_LOG_TRIVIAL(debug) << "[STATIC WCRT] 开始计算任务 " << task_name << " 的WCRT";
    
    try {
        TaskPathInfo path_info = get_task_path_info(task_name);
        
        if (!path_info.is_complete()) {
            BOOST_LOG_TRIVIAL(warning) << "[STATIC WCRT] 任务 " << task_name << " 的路径信息不完整";
            return -1;
        }
        
        // 计算从entry到exit的最长路径
        const int wcrt = compute_longest_path_time(
            path_info.entry_vertex,
            path_info.exit_vertex
        );
        
        BOOST_LOG_TRIVIAL(info) << "[STATIC WCRT] 任务 " << task_name << " 的WCRT: " << wcrt;
        return wcrt;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[STATIC WCRT] 计算任务 " << task_name << " WCRT时出错: " << e.what();
        return -1;
    }
}

int StaticWCRTCalculator::calculate_wcet(const std::string& task_name) {
    BOOST_LOG_TRIVIAL(debug) << "[STATIC WCRT] 开始计算任务 " << task_name << " 的WCET";
    
    try {
        TaskPathInfo path_info = get_task_path_info(task_name);
        
        if (!path_info.is_complete()) {
            BOOST_LOG_TRIVIAL(warning) << "[STATIC WCRT] 任务 " << task_name << " 的路径信息不完整";
            return -1;
        }
        
        // 计算从ready到exit的最长路径
        const int wcet = compute_longest_path_time(
            path_info.ready_vertex,
            path_info.exit_vertex
        );
        
        BOOST_LOG_TRIVIAL(info) << "[STATIC WCRT] 任务 " << task_name << " 的WCET: " << wcet;
        return wcet;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[STATIC WCRT] 计算任务 " << task_name << " WCET时出错: " << e.what();
        return -1;
    }
}

TaskPathInfo<ptpn::ptpn_v_desc> StaticWCRTCalculator::get_task_path_info(const std::string& task_name) {
    TaskPathInfo<ptpn::ptpn_v_desc> path_info(task_name);
    
    // 查找任务的各个关键节点
    path_info.entry_vertex = find_task_node(task_name, "entry");
    path_info.ready_vertex = find_task_node(task_name, "ready");
    path_info.exec_vertex = find_task_node(task_name, "exec");
    path_info.exit_vertex = find_task_node(task_name, "exit");
    
    BOOST_LOG_TRIVIAL(debug) << "[STATIC WCRT] 任务 " << task_name << " 路径信息: "
                             << "entry=" << (path_info.entry_vertex != ptpn::ptpn_v_desc{} ? "找到" : "未找到")
                             << ", ready=" << (path_info.ready_vertex != ptpn::ptpn_v_desc{} ? "找到" : "未找到")
                             << ", exec=" << (path_info.exec_vertex != ptpn::ptpn_v_desc{} ? "找到" : "未找到")
                             << ", exit=" << (path_info.exit_vertex != ptpn::ptpn_v_desc{} ? "找到" : "未找到");
    
    return path_info;
}

int StaticWCRTCalculator::compute_longest_path_time(ptpn::ptpn_v_desc source, ptpn::ptpn_v_desc target) const {
    BOOST_LOG_TRIVIAL(debug) << "[STATIC WCRT] 开始静态计算最长路径时间";
    
    // 静态分析的核心思想：
    // 1. 基于Petri网结构分析所有可能的执行路径
    // 2. 不考虑实际调度策略和资源竞争
    // 3. 使用变迁的const_time上界作为路径时间
    // 4. 返回理论上的最长路径时间（绝对上界）
    
    std::unordered_map<ptpn::ptpn_v_desc, int> max_times;
    std::unordered_set<ptpn::ptpn_v_desc> visited;
    std::vector<ptpn::ptpn_v_desc> topo_order;
    
    // 拓扑排序：找到从source到target的所有可能路径
    std::function<void(ptpn::ptpn_v_desc)> dfs = [&](ptpn::ptpn_v_desc v) {
        if (visited.find(v) != visited.end()) return;
        visited.insert(v);
        
        boost::graph_traits<ptpn::PriorityTPNGraph>::out_edge_iterator ei, ei_end;
        for (boost::tie(ei, ei_end) = boost::out_edges(v, petri_net_); ei != ei_end; ++ei) {
            ptpn::ptpn_v_desc target_vertex = boost::target(*ei, petri_net_);
            dfs(target_vertex);
        }
        topo_order.push_back(v);
    };
    
    dfs(source);
    std::reverse(topo_order.begin(), topo_order.end());
    
    // 初始化源顶点时间为0
    max_times[source] = 0;
    
    // 动态规划：计算每条路径的总时间
    for (ptpn::ptpn_v_desc v : topo_order) {
        if (max_times.find(v) == max_times.end()) continue;
        
        boost::graph_traits<ptpn::PriorityTPNGraph>::out_edge_iterator ei, ei_end;
        for (boost::tie(ei, ei_end) = boost::out_edges(v, petri_net_); ei != ei_end; ++ei) {
            ptpn::ptpn_v_desc target_vertex = boost::target(*ei, petri_net_);
            
            // 静态分析：使用变迁的const_time上界
            // 这给出了理论上的最坏情况时间，不考虑实际调度
            int edge_time = 0;
            if (petri_net_[v].is_transition()) {
                const auto &transition = petri_net_[v].as_transition();
                edge_time = transition.const_time.second; // 使用上界作为最坏情况
                BOOST_LOG_TRIVIAL(debug) << "[STATIC WCRT] 变迁 " << petri_net_[v].name 
                                        << " 时间约束: " << edge_time;
            }
            
            int new_time = max_times[v] + edge_time;
            if (max_times.find(target_vertex) == max_times.end() || max_times[target_vertex] < new_time) {
                max_times[target_vertex] = new_time;
            }
        }
    }
    
    int result = max_times.find(target) != max_times.end() ? max_times[target] : -1;
    BOOST_LOG_TRIVIAL(info) << "[STATIC WCRT] 静态计算完成，最长路径时间: " << result;
    BOOST_LOG_TRIVIAL(info) << "[STATIC WCRT] 注意：这是理论上的上界，不考虑实际调度策略";
    
    return result;
}

ptpn::ptpn_v_desc StaticWCRTCalculator::find_task_node(const std::string& task_name, const std::string& node_type) const {
    std::string full_name = task_name + node_type;
    
    boost::graph_traits<ptpn::PriorityTPNGraph>::vertex_iterator vi, vi_end;
    for (boost::tie(vi, vi_end) = boost::vertices(petri_net_); vi != vi_end; ++vi) {
        if (petri_net_[*vi].name == full_name) {
            return *vi;
        }
    }
    
    return boost::graph_traits<ptpn::PriorityTPNGraph>::null_vertex();
}

// WCRTCalculatorFactory 实现

std::unique_ptr<IWCRTCalculator> WCRTCalculatorFactory::create_static_calculator(
    const ptpn::PriorityTPNGraph& petri_net) {
    return std::make_unique<StaticWCRTCalculator>(petri_net);
}

std::string WCRTCalculatorFactory::compare_calculators(const ptpn::PriorityTPNGraph& petri_net, 
                                                      const std::string& task_name) {
    std::stringstream comparison;
    
    comparison << "=== 计算器比较分析 ===" << std::endl;
    comparison << "任务: " << task_name << std::endl;
    
    // 静态计算器
    auto static_calc = create_static_calculator(petri_net);
    int static_wcrt = static_calc->calculate_wcrt(task_name);
    int static_wcet = static_calc->calculate_wcet(task_name);
    
    comparison << "静态计算器结果:" << std::endl;
    comparison << "  WCRT: " << static_wcrt << std::endl;
    comparison << "  WCET: " << static_wcet << std::endl;
    
    comparison << "注意: 静态分析基于图论最长路径，可能找到不存在的路径" << std::endl;
    comparison << "建议: 使用语义分析器验证路径可达性" << std::endl;
    
    return comparison.str();
}

} // namespace task_analysis
