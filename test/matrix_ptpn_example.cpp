#include "matrix_ptpn.h"
#include <iostream>

using namespace matrix_ptpn;

MatrixPTPN create_simple_example() {
    MatrixPTPN net;
    
    size_t p0 = net.add_place("Place0", INF);
    size_t p1 = net.add_place("Place1", INF);
    
    TimeInterval interval(1, 5);  // 时间区间 [1, 5]
    size_t t0 = net.add_transition("Transition0", interval, 10, 0, false);
    
    net.set_pre_arc(p0, t0, 1);
    
    net.set_post_arc(t0, p1, 1);
    
    net.set_initial_marking(p0, 1);
    
    return net;
}

MatrixPTPN create_producer_consumer_example() {
    MatrixPTPN net;
    
    size_t p_empty = net.add_place("Empty", INF);
    size_t p_full = net.add_place("Full", INF);
    size_t p_prod = net.add_place("ProducerReady", INF);
    size_t p_cons = net.add_place("ConsumerReady", INF);
    
    TimeInterval prod_interval(2, 4);
    size_t t_produce = net.add_transition("Produce", prod_interval, 5, 0, false);
    
    TimeInterval cons_interval(1, 3);
    size_t t_consume = net.add_transition("Consume", cons_interval, 10, 0, false);
    
    net.set_pre_arc(p_prod, t_produce, 1);
    net.set_pre_arc(p_empty, t_produce, 1);
    net.set_post_arc(t_produce, p_prod, 1);
    net.set_post_arc(t_produce, p_full, 1);
    
    net.set_pre_arc(p_cons, t_consume, 1);
    net.set_pre_arc(p_full, t_consume, 1);
    net.set_post_arc(t_consume, p_cons, 1);
    net.set_post_arc(t_consume, p_empty, 1);
    
    net.set_initial_marking(p_prod, 1);
    net.set_initial_marking(p_cons, 1);
    net.set_initial_marking(p_empty, 2);
    net.set_initial_marking(p_full, 0);
    
    return net;
}

void test_simple_example() {
    MatrixPTPN net = create_simple_example();
    
    std::cout << net.to_string() << "\n";
    
    if (net.is_enabled(0)) {
        std::cout << "变迁 T0 使能\n";
        
        Marking current = net.get_marking();
        std::cout << "当前标识: ";
        for (size_t i = 0; i < current.size(); ++i) {
            std::cout << "P" << i << "=" << current[i] << " ";
        }
        std::cout << "\n";
        
        net.fire_transition(0);
        
        Marking new_marking = net.get_marking();
        std::cout << "触发后标识: ";
        for (size_t i = 0; i < new_marking.size(); ++i) {
            std::cout << "P" << i << "=" << new_marking[i] << " ";
        }
        std::cout << "\n";
    } else {
        std::cout << "变迁 T0 未使能\n";
    }
}

void test_producer_consumer() {
    MatrixPTPN net = create_producer_consumer_example();
    
    std::cout << net.to_string() << "\n";
    
    std::vector<size_t> enabled = net.get_enabled_transitions();
    std::cout << "使能的变迁: ";
    for (size_t t : enabled) {
        std::cout << "T" << t << " ";
    }
    std::cout << "\n";
    
    std::vector<size_t> filtered = net.filter_by_priority(enabled);
    std::cout << "优先级过滤后: ";
    for (size_t t : filtered) {
        std::cout << "T" << t << " ";
    }
    std::cout << "\n";
    
    if (!filtered.empty()) {
        std::cout << "\n触发变迁 T" << filtered[0] << "\n";
        net.fire_transition(filtered[0]);
        
        Marking new_marking = net.get_marking();
        std::cout << "新标识: ";
        for (size_t i = 0; i < new_marking.size(); ++i) {
            std::cout << "P" << i << "=" << new_marking[i] << " ";
        }
        std::cout << "\n";
    }
}

MatrixPTPN create_multi_core_example() {
    MatrixPTPN net;
    
    size_t p_task1_ready = net.add_place("Task1Ready", INF);
    size_t p_task1_done = net.add_place("Task1Done", INF);
    size_t p_task2_ready = net.add_place("Task2Ready", INF);
    size_t p_task2_done = net.add_place("Task2Done", INF);
    size_t p_task3_ready = net.add_place("Task3Ready", INF);
    size_t p_task3_done = net.add_place("Task3Done", INF);
    
    TimeInterval t1_interval(1, 3);
    size_t t1 = net.add_transition("Task1", t1_interval, 5, 0, false);
    
    TimeInterval t2_interval(2, 4);
    size_t t2 = net.add_transition("Task2", t2_interval, 10, 0, false);
    
    TimeInterval t3_interval(1, 2);
    size_t t3 = net.add_transition("Task3", t3_interval, 5, 1, false);
    
    net.set_pre_arc(p_task1_ready, t1, 1);
    net.set_post_arc(t1, p_task1_done, 1);
    
    net.set_pre_arc(p_task2_ready, t2, 1);
    net.set_post_arc(t2, p_task2_done, 1);
    
    net.set_pre_arc(p_task3_ready, t3, 1);
    net.set_post_arc(t3, p_task3_done, 1);
    
    net.set_initial_marking(p_task1_ready, 1);
    net.set_initial_marking(p_task2_ready, 1);
    net.set_initial_marking(p_task3_ready, 1);
    
    return net;
}

void test_multi_core() {            
    MatrixPTPN net = create_multi_core_example();
    
    std::cout << net.to_string() << "\n";
    
    std::vector<size_t> enabled = net.get_enabled_transitions();
    std::cout << "所有使能的变迁: ";
    for (size_t t : enabled) {
        const auto& trans = net.get_transition(t);
        std::cout << "T" << t << "(" << trans.name << ", core=" << trans.core << ", priority=" << trans.priority << ") ";
    }
    std::cout << "\n";
    
    std::vector<size_t> filtered = net.filter_by_core_and_priority(enabled);
    std::cout << "按核心和优先级过滤后: ";
    for (size_t t : filtered) {
        const auto& trans = net.get_transition(t);
        std::cout << "T" << t << "(" << trans.name << ", core=" << trans.core << ", priority=" << trans.priority << ") ";
    }
    std::cout << "\n";
    
    std::cout << "\n各核心的使能变迁:\n";
    for (int core = 0; core <= 1; ++core) {
        std::vector<size_t> core_enabled = net.get_enabled_transitions_by_core(core);
        std::cout << "  核心 " << core << ": ";
        if (core_enabled.empty()) {
            std::cout << "无";
        } else {
            for (size_t t : core_enabled) {
                const auto& trans = net.get_transition(t);
                std::cout << "T" << t << "(" << trans.name << ", priority=" << trans.priority << ") ";
            }
        }
        std::cout << "\n";
    }
}

void test_time_interval() {
    std::cout << "\n=== 时间区间测试 ===\n";
    
    TimeInterval interval1(1, 5);
    std::cout << "区间1: " << interval1.to_string() << "\n";
    std::cout << "  包含时间 3: " << (interval1.contains(3) ? "是" : "否") << "\n";
    std::cout << "  包含时间 0: " << (interval1.contains(0) ? "是" : "否") << "\n";
    std::cout << "  包含时间 6: " << (interval1.contains(6) ? "是" : "否") << "\n";
    
    TimeInterval interval2(0, INF);
    std::cout << "区间2: " << interval2.to_string() << "\n";
    std::cout << "  包含时间 1000: " << (interval2.contains(1000) ? "是" : "否") << "\n";
}

int main() {
    try {
        test_simple_example();
        test_producer_consumer();
        test_multi_core();
        test_time_interval();
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}

