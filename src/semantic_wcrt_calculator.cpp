#include "semantic_wcrt_calculator.h"
#include <boost/graph/graph_traits.hpp>
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <sstream>

namespace task_analysis {

// SemanticWCRTCalculator 实现

int SemanticWCRTCalculator::calculate_wcrt(const std::string& task_name) {
    BOOST_LOG_TRIVIAL(debug) << "[SEMANTIC WCRT] 开始语义计算任务 " << task_name << " 的WCRT";
    
    try {
        TaskPathInfo path_info = get_task_path_info(task_name);
        
        if (!path_info.is_complete()) {
            BOOST_LOG_TRIVIAL(warning) << "[SEMANTIC WCRT] 任务 " << task_name << " 的路径信息不完整";
            return -1;
        }
        
        // 计算从entry到exit的语义最长路径
        int wcrt = compute_semantic_longest_path_time(
            static_cast<ptpn::ptpn_v_desc>(path_info.entry_vertex),
            static_cast<ptpn::ptpn_v_desc>(path_info.exit_vertex)
        );
        
        BOOST_LOG_TRIVIAL(info) << "[SEMANTIC WCRT] 任务 " << task_name << " 的WCRT: " << wcrt;
        return wcrt;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[SEMANTIC WCRT] 计算任务 " << task_name << " WCRT时出错: " << e.what();
        return -1;
    }
}

int SemanticWCRTCalculator::calculate_wcet(const std::string& task_name) {
    BOOST_LOG_TRIVIAL(debug) << "[SEMANTIC WCRT] 开始语义计算任务 " << task_name << " 的WCET";
    
    try {
        TaskPathInfo path_info = get_task_path_info(task_name);
        
        if (!path_info.is_complete()) {
            BOOST_LOG_TRIVIAL(warning) << "[SEMANTIC WCRT] 任务 " << task_name << " 的路径信息不完整";
            return -1;
        }
        
        // 计算从ready到exit的语义最长路径
        int wcet = compute_semantic_longest_path_time(
            static_cast<ptpn::ptpn_v_desc>(path_info.ready_vertex),
            static_cast<ptpn::ptpn_v_desc>(path_info.exit_vertex)
        );
        
        BOOST_LOG_TRIVIAL(info) << "[SEMANTIC WCRT] 任务 " << task_name << " 的WCET: " << wcet;
        return wcet;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[SEMANTIC WCRT] 计算任务 " << task_name << " WCET时出错: " << e.what();
        return -1;
    }
}

TaskPathInfo<ptpn::ptpn_v_desc> SemanticWCRTCalculator::get_task_path_info(const std::string& task_name) {
    TaskPathInfo<ptpn::ptpn_v_desc> path_info(task_name);
    
    // 查找任务的各个关键节点
    path_info.entry_vertex = find_task_node(task_name, "entry");
    path_info.ready_vertex = find_task_node(task_name, "ready");
    path_info.exec_vertex = find_task_node(task_name, "exec");
    path_info.exit_vertex = find_task_node(task_name, "exit");
    
    return path_info;
}

int SemanticWCRTCalculator::compute_semantic_longest_path_time(ptpn::ptpn_v_desc source, ptpn::ptpn_v_desc target) const {
    BOOST_LOG_TRIVIAL(debug) << "[SEMANTIC WCRT] 开始语义最长路径计算";
    
    // 语义分析的核心思想：
    // 1. 考虑Petri网的发生语义
    // 2. 确保找到的路径在实际执行中是可达的
    // 3. 考虑token约束和变迁使能条件
    // 4. 使用BFS找到所有可达路径，然后选择最长的
    
    // 找到所有从source到target的可达路径
    auto reachable_paths = find_reachable_paths(source, target);
    
    if (reachable_paths.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "[SEMANTIC WCRT] 没有找到从source到target的可达路径";
        return -1;
    }
    
    // 计算每条路径的时间，找到最长的
    int max_time = 0;
    for (const auto& [path, time] : reachable_paths) {
        max_time = std::max(max_time, time);
        BOOST_LOG_TRIVIAL(debug) << "[SEMANTIC WCRT] 路径时间: " << time;
    }
    
    BOOST_LOG_TRIVIAL(info) << "[SEMANTIC WCRT] 语义最长路径时间: " << max_time;
    BOOST_LOG_TRIVIAL(info) << "[SEMANTIC WCRT] 找到 " << reachable_paths.size() << " 条可达路径";
    
    return max_time;
}

bool SemanticWCRTCalculator::is_transition_enabled(ptpn::ptpn_v_desc transition, const priority_scg::Marking& marking) const {
    // 检查变迁的所有输入库所是否都有足够的token
    boost::graph_traits<ptpn::PriorityTPNGraph>::in_edge_iterator ei, ei_end;
    for (boost::tie(ei, ei_end) = boost::in_edges(transition, petri_net_); ei != ei_end; ++ei) {
        ptpn::ptpn_v_desc source = boost::source(*ei, petri_net_);
        const auto& [label, weight] = petri_net_[*ei];
        
        if (petri_net_[source].is_place()) {
            auto it = marking.find(source);
            int available_tokens = (it != marking.end()) ? it->second : 0;
            
            if (available_tokens < weight) {
                return false;
            }
        }
    }
    
    return true;
}

priority_scg::Marking SemanticWCRTCalculator::fire_transition(ptpn::ptpn_v_desc transition, const priority_scg::Marking& marking) const {
    priority_scg::Marking new_marking = marking;
    
    // 移除输入库所的token
    boost::graph_traits<ptpn::PriorityTPNGraph>::in_edge_iterator ei, ei_end;
    for (boost::tie(ei, ei_end) = boost::in_edges(transition, petri_net_); ei != ei_end; ++ei) {
        ptpn::ptpn_v_desc source = boost::source(*ei, petri_net_);
        const auto& [label, weight] = petri_net_[*ei];
        
        if (petri_net_[source].is_place()) {
            auto it = new_marking.find(source);
            if (it != new_marking.end()) {
                it->second -= weight;
                if (it->second <= 0) {
                    new_marking.erase(it);
                }
            }
        }
    }
    
    // 添加输出库所的token
    boost::graph_traits<ptpn::PriorityTPNGraph>::out_edge_iterator eo, eo_end;
    for (boost::tie(eo, eo_end) = boost::out_edges(transition, petri_net_); eo != eo_end; ++eo) {
        ptpn::ptpn_v_desc target = boost::target(*eo, petri_net_);
        const auto& [label, weight] = petri_net_[*eo];
        
        if (petri_net_[target].is_place()) {
            auto it = new_marking.find(target);
            if (it != new_marking.end()) {
                it->second += weight;
            } else {
                new_marking[target] = weight;
            }
        }
    }
    
    return new_marking;
}

std::vector<std::pair<std::vector<ptpn::ptpn_v_desc>, int>> SemanticWCRTCalculator::find_reachable_paths(
    ptpn::ptpn_v_desc source, ptpn::ptpn_v_desc target) const {
    
    std::vector<std::pair<std::vector<ptpn::ptpn_v_desc>, int>> reachable_paths;
    
    // 使用BFS找到所有可达路径
    struct BFSState {
        priority_scg::Marking marking;
        std::vector<ptpn::ptpn_v_desc> path;
        int accumulated_time;
    };
    
    std::queue<BFSState> bfs_queue;
    std::unordered_set<std::string> visited_states; // 用于避免循环
    
    // 初始化BFS
    BFSState initial_state;
    initial_state.marking = get_initial_marking();
    initial_state.path = {source};
    initial_state.accumulated_time = 0;
    
    bfs_queue.push(initial_state);
    
    while (!bfs_queue.empty()) {
        BFSState current = bfs_queue.front();
        bfs_queue.pop();
        
        // 检查是否到达目标
        if (marking_contains_vertex(current.marking, target)) {
            reachable_paths.emplace_back(current.path, current.accumulated_time);
            continue;
        }
        
        // 生成状态标识符（用于避免循环）
        std::stringstream state_id;
        state_id << "path:";
        for (auto v : current.path) {
            state_id << v << ",";
        }
        state_id << "marking:";
        for (const auto& [place, tokens] : current.marking) {
            state_id << place << ":" << tokens << ",";
        }
        
        if (visited_states.find(state_id.str()) != visited_states.end()) {
            continue; // 避免循环
        }
        visited_states.insert(state_id.str());
        
        // 找到所有使能的变迁
        boost::graph_traits<ptpn::PriorityTPNGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = boost::vertices(petri_net_); vi != vi_end; ++vi) {
            if (petri_net_[*vi].is_transition() && is_transition_enabled(*vi, current.marking)) {
                // 触发变迁
                priority_scg::Marking new_marking = fire_transition(*vi, current.marking);
                
                // 计算新路径的时间
                int transition_time = 0;
                if (petri_net_[*vi].is_transition()) {
                    const auto& transition = petri_net_[*vi].as_transition();
                    transition_time = transition.const_time.second; // 使用上界
                }
                
                // 创建新状态
                BFSState new_state;
                new_state.marking = new_marking;
                new_state.path = current.path;
                new_state.path.push_back(*vi);
                new_state.accumulated_time = current.accumulated_time + transition_time;
                
                bfs_queue.push(new_state);
            }
        }
    }
    
    return reachable_paths;
}

int SemanticWCRTCalculator::calculate_path_time(const std::vector<ptpn::ptpn_v_desc>& path) const {
    int total_time = 0;
    
    for (ptpn::ptpn_v_desc vertex : path) {
        if (petri_net_[vertex].is_transition()) {
            const auto& transition = petri_net_[vertex].as_transition();
            total_time += transition.const_time.second; // 使用上界
        }
    }
    
    return total_time;
}

ptpn::ptpn_v_desc SemanticWCRTCalculator::find_task_node(const std::string& task_name, const std::string& node_type) const {
    std::string full_name = task_name + node_type;
    
    boost::graph_traits<ptpn::PriorityTPNGraph>::vertex_iterator vi, vi_end;
    for (boost::tie(vi, vi_end) = boost::vertices(petri_net_); vi != vi_end; ++vi) {
        if (petri_net_[*vi].name == full_name) {
            return *vi;
        }
    }
    
    return boost::graph_traits<ptpn::PriorityTPNGraph>::null_vertex();
}

priority_scg::Marking SemanticWCRTCalculator::get_initial_marking() const {
    priority_scg::Marking initial_marking;
    
    boost::graph_traits<ptpn::PriorityTPNGraph>::vertex_iterator vi, vi_end;
    for (boost::tie(vi, vi_end) = boost::vertices(petri_net_); vi != vi_end; ++vi) {
        if (petri_net_[*vi].is_place()) {
            const auto& [token, capacity] = petri_net_[*vi].as_place();
            if (token > 0) {
                initial_marking[*vi] = token;
            }
        }
    }
    
    return initial_marking;
}

bool SemanticWCRTCalculator::marking_contains_vertex(const priority_scg::Marking& marking, ptpn::ptpn_v_desc target) const {
    if (petri_net_[target].is_place()) {
        auto it = marking.find(target);
        return it != marking.end() && it->second > 0;
    }
    
    return false;
}

// EnhancedWCRTCalculatorFactory 实现

std::unique_ptr<IWCRTCalculator> EnhancedWCRTCalculatorFactory::create_semantic_calculator(
    const ptpn::PriorityTPNGraph& petri_net) {
    return std::make_unique<SemanticWCRTCalculator>(petri_net);
}

std::string EnhancedWCRTCalculatorFactory::compare_calculators(const ptpn::PriorityTPNGraph& petri_net, 
                                                              const std::string& task_name) {
    std::stringstream comparison;
    
    comparison << "=== 计算器比较分析 ===" << std::endl;
    comparison << "任务: " << task_name << std::endl;
    
    // 静态计算器
    auto static_calc = std::make_unique<StaticWCRTCalculator>(petri_net);
    int static_wcrt = static_calc->calculate_wcrt(task_name);
    int static_wcet = static_calc->calculate_wcet(task_name);
    
    // 语义计算器
    auto semantic_calc = EnhancedWCRTCalculatorFactory::create_semantic_calculator(petri_net);
    int semantic_wcrt = semantic_calc->calculate_wcrt(task_name);
    int semantic_wcet = semantic_calc->calculate_wcet(task_name);
    
    comparison << "静态计算器结果:" << std::endl;
    comparison << "  WCRT: " << static_wcrt << std::endl;
    comparison << "  WCET: " << static_wcet << std::endl;
    
    comparison << "语义计算器结果:" << std::endl;
    comparison << "  WCRT: " << semantic_wcrt << std::endl;
    comparison << "  WCET: " << semantic_wcet << std::endl;
    
    comparison << "差异分析:" << std::endl;
    if (static_wcrt != semantic_wcrt) {
        comparison << "  WCRT差异: " << (static_wcrt - semantic_wcrt) 
                  << " (静态分析" << (static_wcrt > semantic_wcrt ? "高估" : "低估") << ")" << std::endl;
    }
    if (static_wcet != semantic_wcet) {
        comparison << "  WCET差异: " << (static_wcet - semantic_wcet) 
                  << " (静态分析" << (static_wcet > semantic_wcet ? "高估" : "低估") << ")" << std::endl;
    }
    
    if (static_wcrt == semantic_wcrt && static_wcet == semantic_wcet) {
        comparison << "  两种方法结果一致，说明图论路径在实际执行中是可达的" << std::endl;
    } else {
        comparison << "  两种方法结果不一致，说明存在语义约束" << std::endl;
    }
    
    return comparison.str();
}

} // namespace task_analysis
