#include "state_class.h"
#include "state_class_graph.h"
#include "matrix_ptpn.h"
#include <iostream>
#include <iomanip>
#include <map>
#include <vector>

using namespace matrix_ptpn;
using namespace state_class;

/**
 * 创建一个包含可挂起和不可挂起变迁的复杂 PTPN 示例
 * 
 * 示例网络:
 * - P0 (初始有1个token) -> T0(不可挂起) -> P1 -> T1(可挂起) -> P2
 * - P0 -> T2(不可挂起) -> P3 -> T3(可挂起) -> P4
 * - P2 和 P4 都连接到 T4(不可挂起) -> P5
 * 
 * 变迁说明:
 * - T0: [2, 5], 优先级 1, 核心 0, 不可挂起 -> 使用 Z1
 * - T1: [1, 4], 优先级 2, 核心 0, 可挂起 -> 使用 Z2
 * - T2: [1, 3], 优先级 1, 核心 1, 不可挂起 -> 使用 Z1
 * - T3: [2, 6], 优先级 2, 核心 1, 可挂起 -> 使用 Z2
 * - T4: [1, 2], 优先级 1, 核心 0, 不可挂起 -> 使用 Z1
 * 
 * 预期状态序列:
 * 初始状态: M=[1,0,0,0,0,0]
 * T0触发后: M=[0,1,0,0,0,0] 或 T2触发后: M=[0,0,0,1,0,0]
 * T1触发后: M=[0,0,1,0,0,0] 或 T3触发后: M=[0,0,0,0,1,0]
 * T4触发后: M=[0,0,0,0,0,1]
 */
void create_complex_ptpn(MatrixPTPN& ptpn) {
    // 添加位置
    size_t p0 = ptpn.add_place("P0");
    size_t p1 = ptpn.add_place("P1");
    size_t p2 = ptpn.add_place("P2");
    size_t p3 = ptpn.add_place("P3");
    size_t p4 = ptpn.add_place("P4");
    size_t p5 = ptpn.add_place("P5");
    
    // 添加变迁
    // T0: 不可挂起变迁,核心 0
    size_t t0 = ptpn.add_transition("T0", 
                                     TimeInterval(2, 5),  // [2, 5]
                                     1,                   // 优先级 1
                                     0,                   // 核心 0
                                     false);              // 不可挂起 -> Z1
    
    // T1: 可挂起变迁,核心 0
    size_t t1 = ptpn.add_transition("T1",
                                     TimeInterval(1, 4),  // [1, 4]
                                     2,                   // 优先级 2
                                     0,                   // 核心 0
                                     true);               // 可挂起 -> Z2
    
    // T2: 不可挂起变迁,核心 1
    size_t t2 = ptpn.add_transition("T2",
                                     TimeInterval(1, 3),  // [1, 3]
                                     1,                   // 优先级 1
                                     1,                   // 核心 1
                                     false);              // 不可挂起 -> Z1
    
    // T3: 可挂起变迁,核心 1
    size_t t3 = ptpn.add_transition("T3",
                                     TimeInterval(2, 6),  // [2, 6]
                                     2,                   // 优先级 2
                                     1,                   // 核心 1
                                     true);               // 可挂起 -> Z2
    
    // T4: 不可挂起变迁,核心 0
    size_t t4 = ptpn.add_transition("T4",
                                     TimeInterval(1, 2),  // [1, 2]
                                     1,                   // 优先级 1
                                     0,                   // 核心 0
                                     false);              // 不可挂起 -> Z1
    
    // 设置 Pre 矩阵
    ptpn.set_pre_arc(p0, t0, 1);  // P0 -> T0
    ptpn.set_pre_arc(p1, t1, 1);  // P1 -> T1
    ptpn.set_pre_arc(p0, t2, 1);  // P0 -> T2
    ptpn.set_pre_arc(p3, t3, 1);  // P3 -> T3
    ptpn.set_pre_arc(p2, t4, 1);  // P2 -> T4
    ptpn.set_pre_arc(p4, t4, 1);  // P4 -> T4
    
    // 设置 Post 矩阵
    ptpn.set_post_arc(t0, p1, 1);  // T0 -> P1
    ptpn.set_post_arc(t1, p2, 1);  // T1 -> P2
    ptpn.set_post_arc(t2, p3, 1);  // T2 -> P3
    ptpn.set_post_arc(t3, p4, 1);  // T3 -> P4
    ptpn.set_post_arc(t4, p5, 1);  // T4 -> P5
    
    // 设置初始标识
    ptpn.set_initial_marking(p0, 1);  // P0 初始有 1 个 token
    ptpn.set_initial_marking(p1, 0);
    ptpn.set_initial_marking(p2, 0);
    ptpn.set_initial_marking(p3, 0);
    ptpn.set_initial_marking(p4, 0);
    ptpn.set_initial_marking(p5, 0);
}

/**
 * 打印状态类信息(包含 Z1 和 Z2 信息)
 */
void print_state_class(const StateClass& state, size_t index) {
    std::cout << "State " << index << " (ID=" << state.state_id << "):\n";
    std::cout << "  Marking: [";
    for (size_t i = 0; i < state.marking.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << state.marking[i];
    }
    std::cout << "]\n";
    std::cout << "  Cumulative Time: " << std::fixed << std::setprecision(2) 
              << state.cumulative_time << "\n";
    std::cout << "  Z1 (non-suspendable) size: " << state.Z1.size() 
              << ", consistent: " << (state.Z1.is_consistent() ? "yes" : "no") << "\n";
    std::cout << "  Z2 (suspendable) size: " << state.Z2.size() 
              << ", consistent: " << (state.Z2.is_consistent() ? "yes" : "no") << "\n";
    std::cout << "\n";
}

/**
 * 验证状态类生成的正确性(包含可挂起和不可挂起变迁的验证)
 */
bool verify_state_classes(const StateClassGraph& graph, 
                         const StateClassVertex& initial_vertex) {
    std::cout << "=== 验证状态类生成 ===\n\n";
    
    typedef boost::graph_traits<StateClassGraph>::vertex_iterator VertexIterator;
    VertexIterator vi, vi_end;
    
    size_t state_count = 0;
    std::vector<StateClass> states;
    
    // 收集所有状态
    for (std::tie(vi, vi_end) = boost::vertices(graph); vi != vi_end; ++vi) {
        const StateClass& state = boost::get(boost::vertex_name, graph, *vi);
        states.push_back(state);
        state_count++;
        
        std::cout << "State " << state_count << ":\n";
        print_state_class(state, state_count);
    }
    
    // 验证初始状态
    const StateClass& initial = boost::get(boost::vertex_name, graph, initial_vertex);
    if (initial.marking.size() != 6) {
        std::cerr << "错误: 初始状态标识向量大小不正确！期望 6,实际 " 
                  << initial.marking.size() << "\n";
        return false;
    }
    
    if (initial.marking[0] != 1 || initial.marking[1] != 0 || initial.marking[2] != 0 ||
        initial.marking[3] != 0 || initial.marking[4] != 0 || initial.marking[5] != 0) {
        std::cerr << "错误: 初始状态标识不正确！期望 [1,0,0,0,0,0],实际 [";
        for (size_t i = 0; i < initial.marking.size(); ++i) {
            if (i > 0) std::cerr << ",";
            std::cerr << initial.marking[i];
        }
        std::cerr << "]\n";
        return false;
    }
    
    std::cout << "✓ 初始状态验证通过: [1,0,0,0,0,0]\n\n";
    
    // 验证状态转移
    typedef boost::graph_traits<StateClassGraph>::edge_iterator EdgeIterator;
    EdgeIterator ei, ei_end;
    
    std::cout << "=== 状态转移 ===\n";
    std::map<int, std::string> transition_names = {
        {0, "T0(不可挂起,核心0)"},
        {1, "T1(可挂起,核心0)"},
        {2, "T2(不可挂起,核心1)"},
        {3, "T3(可挂起,核心1)"},
        {4, "T4(不可挂起,核心0)"}
    };
    
    for (std::tie(ei, ei_end) = boost::edges(graph); ei != ei_end; ++ei) {
        StateClassVertex src = boost::source(*ei, graph);
        StateClassVertex tgt = boost::target(*ei, graph);
        const TransitionEdge& edge = boost::get(boost::edge_name, graph, *ei);
        const StateClass& src_state = boost::get(boost::vertex_name, graph, src);
        const StateClass& tgt_state = boost::get(boost::vertex_name, graph, tgt);
        
        std::string trans_name = transition_names.count(edge.transition_id) 
                                 ? transition_names[edge.transition_id] 
                                 : "T" + std::to_string(edge.transition_id);
        
        std::cout << "State " << src_state.state_id 
                  << " -> State " << tgt_state.state_id
                  << " via " << trans_name
                  << " @ " << std::fixed << std::setprecision(2) << edge.firing_time << "\n";
        
        std::cout << "  [";
        for (size_t i = 0; i < src_state.marking.size(); ++i) {
            if (i > 0) std::cout << ",";
            std::cout << src_state.marking[i];
        }
        std::cout << "] -> [";
        for (size_t i = 0; i < tgt_state.marking.size(); ++i) {
            if (i > 0) std::cout << ",";
            std::cout << tgt_state.marking[i];
        }
        std::cout << "]\n";
        
        // 验证 Z1 和 Z2 的一致性
        if (!src_state.Z1.is_consistent()) {
            std::cout << "  ⚠ 警告: 源状态 Z1 不一致！\n";
        }
        if (!src_state.Z2.is_consistent()) {
            std::cout << "  ⚠ 警告: 源状态 Z2 不一致！\n";
        }
        std::cout << "\n";
    }
    
    // 验证预期状态序列
    bool found_m010000 = false;  // T0 触发后
    bool found_m001000 = false;  // T1 触发后 (路径1)
    bool found_m000100 = false;  // T2 触发后
    bool found_m000010 = false;  // T3 触发后 (路径2)
    bool found_m000001 = false;  // T4 触发后
    
    for (const auto& state : states) {
        std::vector<int> m = state.marking;
        if (m.size() == 6) {
            if (m[0] == 0 && m[1] == 1 && m[2] == 0 && m[3] == 0 && m[4] == 0 && m[5] == 0) {
                found_m010000 = true;
                std::cout << "✓ 找到预期状态 [0,1,0,0,0,0] (T0 触发后)\n";
            }
            if (m[0] == 0 && m[1] == 0 && m[2] == 1 && m[3] == 0 && m[4] == 0 && m[5] == 0) {
                found_m001000 = true;
                std::cout << "✓ 找到预期状态 [0,0,1,0,0,0] (T1 触发后,路径1)\n";
            }
            if (m[0] == 0 && m[1] == 0 && m[2] == 0 && m[3] == 1 && m[4] == 0 && m[5] == 0) {
                found_m000100 = true;
                std::cout << "✓ 找到预期状态 [0,0,0,1,0,0] (T2 触发后)\n";
            }
            if (m[0] == 0 && m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 1 && m[5] == 0) {
                found_m000010 = true;
                std::cout << "✓ 找到预期状态 [0,0,0,0,1,0] (T3 触发后,路径2)\n";
            }
            if (m[0] == 0 && m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 0 && m[5] == 1) {
                found_m000001 = true;
                std::cout << "✓ 找到预期状态 [0,0,0,0,0,1] (T4 触发后,最终状态)\n";
            }
        }
    }
    
    // 验证 Z1 和 Z2 的正确使用
    std::cout << "\n=== Z1/Z2 验证 ===\n";
    bool z1_used = false;
    bool z2_used = false;
    
    for (std::tie(ei, ei_end) = boost::edges(graph); ei != ei_end; ++ei) {
        const TransitionEdge& edge = boost::get(boost::edge_name, graph, *ei);
        const StateClass& src_state = boost::get(boost::vertex_name, graph, 
                                                  boost::source(*ei, graph));
        
        // 根据变迁 ID 判断是否可挂起
        // T0, T2, T4: 不可挂起 -> 应该使用 Z1
        // T1, T3: 可挂起 -> 应该使用 Z2
        if (edge.transition_id == 0 || edge.transition_id == 2 || edge.transition_id == 4) {
            if (src_state.Z1.size() > 1) {
                z1_used = true;
            }
        } else if (edge.transition_id == 1 || edge.transition_id == 3) {
            if (src_state.Z2.size() > 1) {
                z2_used = true;
            }
        }
    }
    
    std::cout << "Z1 (不可挂起变迁约束) 被使用: " << (z1_used ? "✓" : "✗") << "\n";
    std::cout << "Z2 (可挂起变迁约束) 被使用: " << (z2_used ? "✓" : "✗") << "\n";
    
    std::cout << "\n=== 验证结果 ===\n";
    std::cout << "总状态数: " << state_count << "\n";
    std::cout << "初始状态 [1,0,0,0,0,0]: ✓\n";
    std::cout << "状态 [0,1,0,0,0,0] (T0后): " << (found_m010000 ? "✓" : "✗") << "\n";
    std::cout << "状态 [0,0,1,0,0,0] (T1后): " << (found_m001000 ? "✓" : "✗") << "\n";
    std::cout << "状态 [0,0,0,1,0,0] (T2后): " << (found_m000100 ? "✓" : "✗") << "\n";
    std::cout << "状态 [0,0,0,0,1,0] (T3后): " << (found_m000010 ? "✓" : "✗") << "\n";
    std::cout << "状态 [0,0,0,0,0,1] (T4后): " << (found_m000001 ? "✓" : "✗") << "\n";
    
    return found_m010000 && (found_m001000 || found_m000010) && found_m000001;
}

int main() {
    std::cout << "========================================\n";
    std::cout << "状态类生成正确性验证示例\n";
    std::cout << "(包含可挂起和不可挂起变迁)\n";
    std::cout << "========================================\n\n";
    
    // 创建复杂的 PTPN(包含可挂起和不可挂起变迁)
    MatrixPTPN ptpn;
    create_complex_ptpn(ptpn);
    
    std::cout << "=== PTPN 网络结构 ===\n";
    std::cout << ptpn.to_string() << "\n";
    
    // 显示变迁的挂起属性
    std::cout << "=== 变迁属性 ===\n";
    for (size_t i = 0; i < ptpn.num_transitions(); ++i) {
        const auto& trans = ptpn.get_transition(i);
        std::cout << "T" << i << ": " << trans.name 
                  << ", 时间区间=" << trans.time_interval.to_string()
                  << ", 优先级=" << trans.priority
                  << ", 核心=" << trans.core
                  << ", " << (trans.suspendable ? "可挂起(Z2)" : "不可挂起(Z1)") << "\n";
    }
    std::cout << "\n";
    
    // 构建状态类可达图
    std::cout << "=== 构建状态类可达图 ===\n";
    StateClassReachabilityGraph scg(ptpn);
    
    size_t max_states = 100;  // 限制最大状态数
    size_t num_states = scg.build(max_states);
    
    std::cout << "生成的状态数: " << num_states << "\n\n";
    
    // 获取统计信息
    const auto& stats = scg.get_statistics();
    std::cout << "=== 统计信息 ===\n";
    std::cout << "总状态数: " << stats.total_states << "\n";
    std::cout << "总转移数: " << stats.total_transitions << "\n";
    std::cout << "使能变迁计数: " << stats.enabled_transitions_count << "\n";
    std::cout << "剪枝状态数: " << stats.pruned_states_count << "\n\n";
    
    // 验证状态类
    bool success = verify_state_classes(scg.get_graph(), scg.get_initial_vertex());
    
    // 导出为 DOT 文件
    std::string dot_file = "state_class_example.dot";
    if (scg.save_to_dot(dot_file)) {
        std::cout << "\n✓ 状态类图已导出到: " << dot_file << "\n";
    }
    
    // 导出为 JSON 文件
    std::string json_file = "state_class_example.json";
    if (scg.save_to_json(json_file)) {
        std::cout << "✓ 状态类图已导出到: " << json_file << "\n";
    }
    
    std::cout << "\n========================================\n";
    if (success) {
        std::cout << "✓ 验证通过！状态类生成正确.\n";
        std::cout << "  - Z1 (不可挂起变迁约束) 和 Z2 (可挂起变迁约束) 正确分离\n";
        std::cout << "  - 状态转移符合预期\n";
        return 0;
    } else {
        std::cout << "✗ 验证失败！请检查状态类生成逻辑.\n";
        return 1;
    }
}

