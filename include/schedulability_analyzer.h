#ifndef SCHEDULABILITY_ANALYZER_H
#define SCHEDULABILITY_ANALYZER_H

#include "task_analysis_types.h"
#include "wcrt_calculator.h"
#include "deadlock_detector.h"
#include <vector>
#include <memory>

namespace task_analysis {

/**
 * @brief 可调度性分析器抽象基类
 * 
 * 定义了可调度性分析的标准接口
 */
class ISchedulabilityAnalyzer {
public:
    virtual ~ISchedulabilityAnalyzer() = default;
    
    /**
     * @brief 分析任务的可调度性
     * @param task_name 任务名称
     * @param deadline 截止时间
     * @return 是否可调度
     */
    virtual bool analyze_schedulability(const std::string& task_name, int deadline) = 0;
    
    /**
     * @brief 分析所有任务的可调度性
     * @param deadline 截止时间
     * @return 可调度性分析结果列表
     */
    virtual std::vector<TaskAnalysisResult> analyze_all_tasks(int deadline) = 0;
    
    /**
     * @brief 获取可调度性统计信息
     * @return 统计信息
     */
    virtual AnalysisStatistics get_statistics() const = 0;
};

/**
 * @brief 基于WCRT的可调度性分析器
 * 
 * 使用WCRT与deadline比较来判断可调度性
 */
class WCRTSchedulabilityAnalyzer : public ISchedulabilityAnalyzer {
private:
    std::unique_ptr<IWCRTCalculator> wcrt_calculator_;
    std::unique_ptr<IDeadlockDetector> deadlock_detector_;
    AnalysisStatistics statistics_;
    
public:
    /**
     * @brief 构造函数
     * @param wcrt_calculator WCRT计算器
     * @param deadlock_detector 死锁检测器
     */
    WCRTSchedulabilityAnalyzer(std::unique_ptr<IWCRTCalculator> wcrt_calculator,
                              std::unique_ptr<IDeadlockDetector> deadlock_detector)
        : wcrt_calculator_(std::move(wcrt_calculator))
        , deadlock_detector_(std::move(deadlock_detector)) {}
    
    /**
     * @brief 分析任务的可调度性
     * @param task_name 任务名称
     * @param deadline 截止时间
     * @return 是否可调度
     */
    bool analyze_schedulability(const std::string& task_name, int deadline) override;
    
    /**
     * @brief 分析所有任务的可调度性
     * @param deadline 截止时间
     * @return 可调度性分析结果列表
     */
    std::vector<TaskAnalysisResult> analyze_all_tasks(int deadline) override;
    
    /**
     * @brief 获取可调度性统计信息
     * @return 统计信息
     */
    AnalysisStatistics get_statistics() const override;
    
private:
    /**
     * @brief 执行单个任务的完整分析
     * @param task_name 任务名称
     * @param deadline 截止时间
     * @return 任务分析结果
     */
    TaskAnalysisResult perform_task_analysis(const std::string& task_name, int deadline);
    
    /**
     * @brief 更新统计信息
     * @param result 任务分析结果
     */
    void update_statistics(const TaskAnalysisResult& result);
};

/**
 * @brief 基于Rate Monotonic的可调度性分析器
 * 
 * 使用Rate Monotonic调度算法分析可调度性
 */
class RateMonotonicAnalyzer : public ISchedulabilityAnalyzer {
private:
    std::unique_ptr<IWCRTCalculator> wcrt_calculator_;
    std::unique_ptr<IDeadlockDetector> deadlock_detector_;
    AnalysisStatistics statistics_;
    
public:
    /**
     * @brief 构造函数
     * @param wcrt_calculator WCRT计算器
     * @param deadlock_detector 死锁检测器
     */
    RateMonotonicAnalyzer(std::unique_ptr<IWCRTCalculator> wcrt_calculator,
                         std::unique_ptr<IDeadlockDetector> deadlock_detector)
        : wcrt_calculator_(std::move(wcrt_calculator))
        , deadlock_detector_(std::move(deadlock_detector)) {}
    
    /**
     * @brief 分析任务的可调度性
     * @param task_name 任务名称
     * @param deadline 截止时间
     * @return 是否可调度
     */
    bool analyze_schedulability(const std::string& task_name, int deadline) override;
    
    /**
     * @brief 分析所有任务的可调度性
     * @param deadline 截止时间
     * @return 可调度性分析结果列表
     */
    std::vector<TaskAnalysisResult> analyze_all_tasks(int deadline) override;
    
    /**
     * @brief 获取可调度性统计信息
     * @return 统计信息
     */
    AnalysisStatistics get_statistics() const override;
    
private:
    /**
     * @brief 计算任务的响应时间
     * @param task_name 任务名称
     * @param higher_priority_tasks 更高优先级任务列表
     * @return 响应时间
     */
    int calculate_response_time(const std::string& task_name,
                               const std::vector<std::string>& higher_priority_tasks) const;
    
    /**
     * @brief 获取任务优先级
     * @param task_name 任务名称
     * @return 任务优先级
     */
    int get_task_priority(const std::string& task_name) const;
};

/**
 * @brief 可调度性分析器工厂类
 * 
 * 使用工厂模式创建不同类型的可调度性分析器
 */
class SchedulabilityAnalyzerFactory {
public:
    /**
     * @brief 分析器类型枚举
     */
    enum class AnalyzerType {
        WCRT_BASED,         ///< 基于WCRT的分析器
        RATE_MONOTONIC      ///< Rate Monotonic分析器
    };
    
    /**
     * @brief 创建基于WCRT的分析器
     * @param wcrt_calculator WCRT计算器
     * @param deadlock_detector 死锁检测器
     * @return 基于WCRT的分析器智能指针
     */
    static std::unique_ptr<ISchedulabilityAnalyzer> create_wcrt_analyzer(
        std::unique_ptr<IWCRTCalculator> wcrt_calculator,
        std::unique_ptr<IDeadlockDetector> deadlock_detector);
    
    /**
     * @brief 创建Rate Monotonic分析器
     * @param wcrt_calculator WCRT计算器
     * @param deadlock_detector 死锁检测器
     * @return Rate Monotonic分析器智能指针
     */
    static std::unique_ptr<ISchedulabilityAnalyzer> create_rate_monotonic_analyzer(
        std::unique_ptr<IWCRTCalculator> wcrt_calculator,
        std::unique_ptr<IDeadlockDetector> deadlock_detector);
    
    /**
     * @brief 根据类型创建分析器
     * @param type 分析器类型
     * @param wcrt_calculator WCRT计算器
     * @param deadlock_detector 死锁检测器
     * @return 分析器智能指针
     */
    static std::unique_ptr<ISchedulabilityAnalyzer> create_analyzer(
        AnalyzerType type,
        std::unique_ptr<IWCRTCalculator> wcrt_calculator,
        std::unique_ptr<IDeadlockDetector> deadlock_detector);
};

/**
 * @brief 可调度性分析工具类
 * 
 * 提供可调度性分析的辅助功能
 */
class SchedulabilityAnalysisUtils {
public:
    /**
     * @brief 生成可调度性报告
     * @param results 分析结果列表
     * @param statistics 统计信息
     * @return 格式化的可调度性报告
     */
    static std::string generate_schedulability_report(const std::vector<TaskAnalysisResult>& results,
                                                     const AnalysisStatistics& statistics);
    
    /**
     * @brief 检查可调度性约束
     * @param wcrt WCRT值
     * @param deadline 截止时间
     * @return 是否满足约束
     */
    static bool check_schedulability_constraint(int wcrt, int deadline);
    
    /**
     * @brief 计算可调度性余量
     * @param wcrt WCRT值
     * @param deadline 截止时间
     * @return 可调度性余量(0.0-1.0)
     */
    static double calculate_schedulability_margin(int wcrt, int deadline);
    
    /**
     * @brief 评估可调度性风险
     * @param results 分析结果列表
     * @return 风险等级(1-5)
     */
    static int assess_schedulability_risk(const std::vector<TaskAnalysisResult>& results);
};

} // namespace task_analysis

#endif // SCHEDULABILITY_ANALYZER_H
