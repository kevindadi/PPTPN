#include "priority_state_graph.h"
#include "priority_time_petri_net.h"
#include <iostream>

using namespace priority_scg;
using namespace ptpn;

int main() {
    // 创建一个简单的优先级时间Petri网
    PriorityTPNGraph petri_net;
    
    // 添加库所
    auto entry = add_place(petri_net, "task1entry", 1, 1);
    auto ready = add_place(petri_net, "task1ready", 0, 1);
    auto exit = add_place(petri_net, "task1exit", 0, 1);
    
    // 添加变迁
    auto get_core = add_transition(petri_net, "task1get_core", 10, 0, {0, 0}, false, {0, 0});
    auto exec = add_transition(petri_net, "task1exec", 10, 0, {5, 10}, false, {0, 0});
    
    // 添加边
    add_edge(entry, get_core, petri_net);
    add_edge(get_core, ready, petri_net);
    add_edge(exec, exit, petri_net);
    
    // 创建状态类图
    PriorityStateClassGraph state_graph(petri_net);
    
    // 生成状态类图（限制状态数以避免无限状态空间）
    state_graph.generate_state_class_graph_with_limit(50);
    
    std::cout << "状态类图生成完成" << std::endl;
    
    // 分析所有任务
    auto results = state_graph.analyze_all_tasks();
    state_graph.print_analysis_report(results);
    
    // 检查可调度性
    int deadline = 15;
    for (const auto& result : results) {
        bool schedulable = state_graph.check_task_schedulability(result.task_name, deadline);
        std::cout << "任务 " << result.task_name << " 在deadline=" << deadline 
                  << " 下" << (schedulable ? "可调度" : "不可调度") << std::endl;
    }
    
    // 死锁分析
    auto deadlock_result = state_graph.analyze_deadlocks();
    std::cout << "死锁检测结果: " << (deadlock_result.has_deadlock ? "存在死锁" : "无死锁") << std::endl;
    
    return 0;
}
