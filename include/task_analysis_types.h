#ifndef TASK_ANALYSIS_TYPES_H
#define TASK_ANALYSIS_TYPES_H

#include <string>
#include <vector>
#include <memory>

namespace task_analysis {

/**
 * @brief 任务分析结果结构体
 * 
 * 包含任务的WCRT、WCET、可调度性和死锁信息
 */
struct TaskAnalysisResult {
    std::string task_name;      ///< 任务名称
    int wcrt = 0;              ///< 最坏响应时间 (从entry到exit)
    int wcet = 0;              ///< 最坏执行时间 (从ready到exit)
    bool schedulable = true;    ///< 是否可调度
    bool has_deadlock = false;  ///< 是否包含死锁
    std::string analysis_info;  ///< 分析详情
    
    /**
     * @brief 构造函数
     * @param name 任务名称
     */
    explicit TaskAnalysisResult(const std::string& name = "") 
        : task_name(name) {}
    
    /**
     * @brief 检查分析是否成功
     * @return 分析是否成功
     */
    bool is_valid() const {
        return !task_name.empty() && wcrt >= 0 && wcet >= 0;
    }
    
    /**
     * @brief 获取可调度性状态字符串
     * @return 可调度性状态描述
     */
    std::string get_schedulability_string() const {
        return schedulable ? "可调度" : "不可调度";
    }
    
    /**
     * @brief 获取死锁状态字符串
     * @return 死锁状态描述
     */
    std::string get_deadlock_string() const {
        return has_deadlock ? "存在死锁" : "无死锁";
    }
};

/**
 * @brief 死锁分析结果结构体
 * 
 * 包含死锁检测的详细信息和原因分析
 */
struct DeadlockAnalysisResult {
    bool has_deadlock = false;                    ///< 是否存在死锁
    std::vector<std::string> deadlock_states;     ///< 死锁状态列表
    std::string deadlock_reason;                  ///< 死锁原因
    std::vector<std::string> deadlock_cycles;     ///< 死锁循环信息
    
    /**
     * @brief 构造函数
     */
    DeadlockAnalysisResult() = default;
    
    /**
     * @brief 添加死锁状态
     * @param state_id 状态ID
     */
    void add_deadlock_state(const std::string& state_id) {
        deadlock_states.push_back(state_id);
        has_deadlock = true;
    }
    
    /**
     * @brief 添加死锁循环信息
     * @param cycle_info 循环信息
     */
    void add_deadlock_cycle(const std::string& cycle_info) {
        deadlock_cycles.push_back(cycle_info);
    }
    
    /**
     * @brief 获取死锁状态数量
     * @return 死锁状态数量
     */
    size_t get_deadlock_count() const {
        return deadlock_states.size();
    }
    
    /**
     * @brief 获取死锁循环数量
     * @return 死锁循环数量
     */
    size_t get_cycle_count() const {
        return deadlock_cycles.size();
    }
};

/**
 * @brief 任务路径信息结构体
 * 
 * 包含任务在Petri网中的关键节点信息
 */
template<typename VertexType = size_t>
struct TaskPathInfo {
    std::string task_name;      ///< 任务名称
    VertexType entry_vertex = VertexType{};   ///< entry节点
    VertexType ready_vertex = VertexType{};   ///< ready节点
    VertexType exec_vertex = VertexType{};    ///< exec节点
    VertexType exit_vertex = VertexType{};    ///< exit节点
    
    /**
     * @brief 构造函数
     * @param name 任务名称
     */
    explicit TaskPathInfo(const std::string& name = "") 
        : task_name(name) {}
    
    /**
     * @brief 检查路径信息是否完整
     * @return 路径信息是否完整
     */
    bool is_complete() const {
        return !task_name.empty() && 
               entry_vertex != VertexType{} && 
               ready_vertex != VertexType{} && 
               exec_vertex != VertexType{} && 
               exit_vertex != VertexType{};
    }
};

/**
 * @brief 分析统计信息结构体
 * 
 * 包含整体分析的统计信息
 */
struct AnalysisStatistics {
    size_t total_tasks = 0;         ///< 总任务数
    size_t schedulable_tasks = 0;   ///< 可调度任务数
    size_t deadlock_tasks = 0;      ///< 包含死锁的任务数
    int max_wcrt = 0;              ///< 最大WCRT
    int max_wcet = 0;              ///< 最大WCET
    double analysis_time_ms = 0.0;  ///< 分析时间(毫秒)
    
    /**
     * @brief 更新统计信息
     * @param result 任务分析结果
     */
    void update_statistics(const TaskAnalysisResult& result) {
        total_tasks++;
        if (result.schedulable) {
            schedulable_tasks++;
        }
        if (result.has_deadlock) {
            deadlock_tasks++;
        }
        max_wcrt = std::max(max_wcrt, result.wcrt);
        max_wcet = std::max(max_wcet, result.wcet);
    }
    
    /**
     * @brief 获取可调度率
     * @return 可调度率(0.0-1.0)
     */
    double get_schedulability_ratio() const {
        return total_tasks > 0 ? static_cast<double>(schedulable_tasks) / total_tasks : 0.0;
    }
    
    /**
     * @brief 获取死锁率
     * @return 死锁率(0.0-1.0)
     */
    double get_deadlock_ratio() const {
        return total_tasks > 0 ? static_cast<double>(deadlock_tasks) / total_tasks : 0.0;
    }
    
    /**
     * @brief 检查整体可调度性
     * @return 整体是否可调度
     */
    bool is_overall_schedulable() const {
        return deadlock_tasks == 0;
    }
};

/**
 * @brief 分析配置结构体
 * 
 * 包含分析过程的配置参数
 */
struct AnalysisConfig {
    bool enable_wcrt_analysis = true;        ///< 是否启用WCRT分析
    bool enable_wcet_analysis = true;        ///< 是否启用WCET分析
    bool enable_schedulability_check = true; ///< 是否启用可调度性检查
    bool enable_deadlock_detection = true;   ///< 是否启用死锁检测
    int deadline = -1;                      ///< 截止时间约束(-1表示不检查)
    size_t max_analysis_depth = 1000;       ///< 最大分析深度
    bool verbose_output = false;            ///< 是否输出详细信息
    
    /**
     * @brief 构造函数
     */
    AnalysisConfig() = default;
    
    /**
     * @brief 检查是否应该进行可调度性检查
     * @return 是否应该进行可调度性检查
     */
    [[nodiscard]] bool should_check_schedulability() const {
        return enable_schedulability_check && deadline > 0;
    }
};

} // namespace task_analysis

#endif // TASK_ANALYSIS_TYPES_H
