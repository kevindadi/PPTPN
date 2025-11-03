#ifndef TASK_ANALYSIS_MANAGER_H
#define TASK_ANALYSIS_MANAGER_H

#include "task_analysis_types.h"
#include "wcrt_calculator.h"
#include "deadlock_detector.h"
#include "schedulability_analyzer.h"
#include <memory>
#include <vector>
#include <string>
#include <chrono>

namespace task_analysis {

/**
 * @brief 任务分析管理器
 * 
 * 统一管理WCRT、WCET、可调度性和死锁分析
 */
class TaskAnalysisManager {
private:
    // 核心组件
    std::unique_ptr<IWCRTCalculator> wcrt_calculator_;
    std::unique_ptr<IDeadlockDetector> deadlock_detector_;
    std::unique_ptr<ISchedulabilityAnalyzer> schedulability_analyzer_;
    
    // 配置
    AnalysisConfig config_;
    
    // 统计信息
    AnalysisStatistics statistics_;
    
public:
    /**
     * @brief 构造函数
     * @param config 分析配置
     */
    explicit TaskAnalysisManager(const AnalysisConfig& config = AnalysisConfig{});
    
    /**
     * @brief 析构函数
     */
    ~TaskAnalysisManager() = default;
    
    // 禁用拷贝构造和赋值
    TaskAnalysisManager(const TaskAnalysisManager&) = delete;
    TaskAnalysisManager& operator=(const TaskAnalysisManager&) = delete;
    
    /**
     * @brief 设置WCRT计算器
     * @param calculator WCRT计算器
     */
    void set_wcrt_calculator(std::unique_ptr<IWCRTCalculator> calculator);
    
    /**
     * @brief 设置死锁检测器
     * @param detector 死锁检测器
     */
    void set_deadlock_detector(std::unique_ptr<IDeadlockDetector> detector);
    
    /**
     * @brief 设置可调度性分析器
     * @param analyzer 可调度性分析器
     */
    void set_schedulability_analyzer(std::unique_ptr<ISchedulabilityAnalyzer> analyzer);
    
    /**
     * @brief 更新分析配置
     * @param config 新的分析配置
     */
    void update_config(const AnalysisConfig& config);
    
    /**
     * @brief 获取当前配置
     * @return 当前配置
     */
    const AnalysisConfig& get_config() const;
    
    /**
     * @brief 执行完整的任务分析
     * @return 分析结果列表
     */
    std::vector<TaskAnalysisResult> perform_complete_analysis();
    
    /**
     * @brief 分析单个任务
     * @param task_name 任务名称
     * @return 任务分析结果
     */
    TaskAnalysisResult analyze_single_task(const std::string& task_name);
    
    /**
     * @brief 执行死锁分析
     * @return 死锁分析结果
     */
    DeadlockAnalysisResult perform_deadlock_analysis();
    
    /**
     * @brief 执行可调度性分析
     * @return 可调度性分析结果列表
     */
    std::vector<TaskAnalysisResult> perform_schedulability_analysis();
    
    /**
     * @brief 获取分析统计信息
     * @return 统计信息
     */
    const AnalysisStatistics& get_statistics() const;
    
    /**
     * @brief 重置统计信息
     */
    void reset_statistics();
    
    /**
     * @brief 生成分析报告
     * @return 格式化的分析报告
     */
    std::string generate_analysis_report() const;
    
    /**
     * @brief 保存分析结果到文件
     * @param filename 文件名
     * @param format 文件格式("json", "xml", "csv")
     * @return 是否保存成功
     */
    bool save_results_to_file(const std::string& filename, const std::string& format = "json") const;
    
private:
    /**
     * @brief 验证组件完整性
     * @return 是否所有必要组件都已设置
     */
    bool validate_components() const;
    
    /**
     * @brief 更新分析时间统计
     * @param start_time 开始时间
     * @param end_time 结束时间
     */
    void update_analysis_time(const std::chrono::high_resolution_clock::time_point& start_time,
                             const std::chrono::high_resolution_clock::time_point& end_time);
    
    /**
     * @brief 生成JSON格式的报告
     * @return JSON格式的报告
     */
    std::string generate_json_report() const;
    
    /**
     * @brief 生成XML格式的报告
     * @return XML格式的报告
     */
    std::string generate_xml_report() const;
    
    /**
     * @brief 生成CSV格式的报告
     * @return CSV格式的报告
     */
    std::string generate_csv_report() const;
};

/**
 * @brief 任务分析管理器工厂类  
 */
class TaskAnalysisManagerFactory {
public:
    /**
     * @brief 创建基于静态分析的简单管理器
     * @param petri_net Petri网图引用
     * @param config 分析配置
     * @return 分析管理器智能指针
     */
    static std::unique_ptr<TaskAnalysisManager> create_simple_manager(
        const ptpn::PriorityTPNGraph& petri_net,
        const AnalysisConfig& config = AnalysisConfig{});
    
    /**
     * @brief 创建基于状态类图的高级管理器
     * @param petri_net Petri网图引用
     * @param state_graph 状态类图引用
     * @param config 分析配置
     * @return 分析管理器智能指针
     */
    template<typename StateGraphType>
    static std::unique_ptr<TaskAnalysisManager> create_advanced_manager(
        const ptpn::PriorityTPNGraph& petri_net,
        const StateGraphType& state_graph,
        const AnalysisConfig& config = AnalysisConfig{});
    
    /**
     * @brief 创建自定义配置的管理器
     * @param petri_net Petri网图引用
     * @param state_graph 状态类图引用(可选)
     * @param config 分析配置
     * @return 分析管理器智能指针
     */
    template<typename StateGraphType>
    static std::unique_ptr<TaskAnalysisManager> create_custom_manager(
        const ptpn::PriorityTPNGraph& petri_net,
        const StateGraphType* state_graph,
        const AnalysisConfig& config);
};

/**
 * @brief 分析结果导出器
 * 
 * 提供多种格式的分析结果导出功能
 */
class AnalysisResultExporter {
public:
    /**
     * @brief 导出格式枚举
     */
    enum class ExportFormat {
        JSON,
        XML,
        CSV,
        DOT,
        HTML
    };
    
    /**
     * @brief 导出分析结果
     * @param results 分析结果列表
     * @param filename 文件名
     * @param format 导出格式
     * @return 是否导出成功
     */
    static bool export_results(const std::vector<TaskAnalysisResult>& results,
                              const std::string& filename,
                              ExportFormat format);
    
    /**
     * @brief 导出死锁分析结果
     * @param result 死锁分析结果
     * @param filename 文件名
     * @param format 导出格式
     * @return 是否导出成功
     */
    static bool export_deadlock_result(const DeadlockAnalysisResult& result,
                                      const std::string& filename,
                                      ExportFormat format);
    
    /**
     * @brief 导出统计信息
     * @param statistics 统计信息
     * @param filename 文件名
     * @param format 导出格式
     * @return 是否导出成功
     */
    static bool export_statistics(const AnalysisStatistics& statistics,
                                 const std::string& filename,
                                 ExportFormat format);
    
private:
    /**
     * @brief 生成JSON格式数据
     * @param results 分析结果列表
     * @return JSON字符串
     */
    static std::string generate_json_data(const std::vector<TaskAnalysisResult>& results);
    
    /**
     * @brief 生成XML格式数据
     * @param results 分析结果列表
     * @return XML字符串
     */
    static std::string generate_xml_data(const std::vector<TaskAnalysisResult>& results);
    
    /**
     * @brief 生成CSV格式数据
     * @param results 分析结果列表
     * @return CSV字符串
     */
    static std::string generate_csv_data(const std::vector<TaskAnalysisResult>& results);
};

} // namespace task_analysis

#endif // TASK_ANALYSIS_MANAGER_H
