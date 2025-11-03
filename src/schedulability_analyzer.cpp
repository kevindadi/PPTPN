#include "schedulability_analyzer.h"
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace task_analysis {

// WCRTSchedulabilityAnalyzer 实现

bool WCRTSchedulabilityAnalyzer::analyze_schedulability(const std::string& task_name, int deadline) {
    BOOST_LOG_TRIVIAL(debug) << "[SCHEDULABILITY] 开始分析任务 " << task_name << " 的可调度性 (deadline=" << deadline << ")";
    
    try {
        TaskAnalysisResult result = this->perform_task_analysis(task_name, deadline);
        this->update_statistics(result);
        
        bool schedulable = result.schedulable;
        BOOST_LOG_TRIVIAL(info) << "[SCHEDULABILITY] 任务 " << task_name << " 可调度性: " 
                                << (schedulable ? "可调度" : "不可调度");
        
        return schedulable;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[SCHEDULABILITY] 分析任务 " << task_name << " 可调度性时出错: " << e.what();
        return false;
    }
}

std::vector<TaskAnalysisResult> WCRTSchedulabilityAnalyzer::analyze_all_tasks(int deadline) {
    std::vector<TaskAnalysisResult> results;
    
    BOOST_LOG_TRIVIAL(info) << "[SCHEDULABILITY] 开始分析所有任务的可调度性 (deadline=" << deadline << ")";
    
    try {
        // 这里需要从某个地方获取所有任务名称
        // 为了简化,我们假设可以从WCRT计算器中获取任务列表
        // 实际实现中可能需要从Petri网中提取任务信息
        
        // 示例:假设有一些预定义的任务名称
        const std::vector<std::string> task_names = {"task1", "task2", "task3"}; // 这里需要实际实现
        
        for (const auto& task_name : task_names) {
            TaskAnalysisResult result = this->perform_task_analysis(task_name, deadline);
            results.push_back(result);
            this->update_statistics(result);
        }
        
        BOOST_LOG_TRIVIAL(info) << "[SCHEDULABILITY] 完成所有任务可调度性分析,共分析 " << results.size() << " 个任务";
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[SCHEDULABILITY] 分析所有任务可调度性时出错: " << e.what();
    }
    
    return results;
}

AnalysisStatistics WCRTSchedulabilityAnalyzer::get_statistics() const {
    return statistics_;
}

TaskAnalysisResult WCRTSchedulabilityAnalyzer::perform_task_analysis(const std::string& task_name, int deadline) {
    TaskAnalysisResult result(task_name);
    
    try {
        // 计算WCRT和WCET
        if (wcrt_calculator_) {
            result.wcrt = wcrt_calculator_->calculate_wcrt(task_name);
            result.wcet = wcrt_calculator_->calculate_wcet(task_name);
        }
        
        // 检查死锁
        if (deadlock_detector_) {
            result.has_deadlock = deadlock_detector_->has_deadlock(task_name);
        }
        
        // 判断可调度性
        result.schedulable = SchedulabilityAnalysisUtils::check_schedulability_constraint(result.wcrt, deadline) 
                           && !result.has_deadlock;
        
        // 构建分析信息
        std::stringstream info;
        info << "任务 " << task_name << " 可调度性分析结果:\n";
        info << "  WCRT: " << result.wcrt << "\n";
        info << "  WCET: " << result.wcet << "\n";
        info << "  Deadline: " << deadline << "\n";
        info << "  包含死锁: " << (result.has_deadlock ? "是" : "否") << "\n";
        info << "  可调度性: " << result.get_schedulability_string();
        
        result.analysis_info = info.str();
        
    } catch (const std::exception& e) {
        result.schedulable = false;
        result.analysis_info = "分析失败: " + std::string(e.what());
    }
    
    return result;
}

void WCRTSchedulabilityAnalyzer::update_statistics(const TaskAnalysisResult& result) {
    statistics_.update_statistics(result);
}

// RateMonotonicAnalyzer 实现

bool RateMonotonicAnalyzer::analyze_schedulability(const std::string& task_name, int deadline) {
    BOOST_LOG_TRIVIAL(debug) << "[RATE MONOTONIC] 开始使用Rate Monotonic分析任务 " << task_name << " 的可调度性";
    
    try {
        TaskAnalysisResult result(task_name);
        std::vector<std::string> higher_priority_tasks;
        result.wcrt = calculate_response_time(task_name, higher_priority_tasks);
        if (wcrt_calculator_) {
            result.wcet = wcrt_calculator_->calculate_wcet(task_name);
        }
        
        if (deadlock_detector_) {
            result.has_deadlock = deadlock_detector_->has_deadlock(task_name);
        }
        
        result.schedulable = SchedulabilityAnalysisUtils::check_schedulability_constraint(result.wcrt, deadline)
                           && !result.has_deadlock;
        
        std::stringstream info;
        info << "任务 " << task_name << " Rate Monotonic可调度性分析结果:\n";
        info << "  WCRT: " << result.wcrt << "\n";
        info << "  WCET: " << result.wcet << "\n";
        info << "  Deadline: " << deadline << "\n";
        info << "  包含死锁: " << (result.has_deadlock ? "是" : "否") << "\n";
        info << "  可调度性: " << result.get_schedulability_string();
        
        result.analysis_info = info.str();
        
        statistics_.update_statistics(result);
        
        return result.schedulable;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[RATE MONOTONIC] 分析任务 " << task_name << " 可调度性时出错: " << e.what();
        return false;
    }
}

std::vector<TaskAnalysisResult> RateMonotonicAnalyzer::analyze_all_tasks(int deadline) {
    std::vector<TaskAnalysisResult> results;
    
    BOOST_LOG_TRIVIAL(info) << "[RATE MONOTONIC] 开始使用Rate Monotonic分析所有任务";
    
    std::vector<std::string> task_names = {"task1", "task2", "task3"};
    std::vector<std::string> higher_priority_tasks;
    
    for (const auto& task_name : task_names) {
        TaskAnalysisResult result(task_name);
        
        try {
            result.wcrt = calculate_response_time(task_name, higher_priority_tasks);
            
            if (wcrt_calculator_) {
                result.wcet = wcrt_calculator_->calculate_wcet(task_name);
            }
            
            if (deadlock_detector_) {
                result.has_deadlock = deadlock_detector_->has_deadlock(task_name);
            }
            
            result.schedulable = SchedulabilityAnalysisUtils::check_schedulability_constraint(result.wcrt, deadline)
                               && !result.has_deadlock;
            
            std::stringstream info;
            info << "任务 " << task_name << " Rate Monotonic分析结果:\n";
            info << "  WCRT: " << result.wcrt << "\n";
            info << "  WCET: " << result.wcet << "\n";
            info << "  Deadline: " << deadline << "\n";
            info << "  包含死锁: " << (result.has_deadlock ? "是" : "否") << "\n";
            info << "  可调度性: " << result.get_schedulability_string();
            
            result.analysis_info = info.str();
            
            results.push_back(result);
            statistics_.update_statistics(result);
            
            higher_priority_tasks.push_back(task_name);
            
        } catch (const std::exception& e) {
            result.schedulable = false;
            result.analysis_info = "分析失败: " + std::string(e.what());
            results.push_back(result);
        }
    }
    
    return results;
}

AnalysisStatistics RateMonotonicAnalyzer::get_statistics() const {
    return statistics_;
}

int RateMonotonicAnalyzer::calculate_response_time(const std::string& task_name,
                                                  const std::vector<std::string>& higher_priority_tasks) const {
    if (wcrt_calculator_) {
        return wcrt_calculator_->calculate_wcrt(task_name);
    }
    
    return -1;
}

int RateMonotonicAnalyzer::get_task_priority(const std::string& task_name) const {
    return 100;
}

std::unique_ptr<ISchedulabilityAnalyzer> SchedulabilityAnalyzerFactory::create_wcrt_analyzer(
    std::unique_ptr<IWCRTCalculator> wcrt_calculator,
    std::unique_ptr<IDeadlockDetector> deadlock_detector) {
    return std::make_unique<WCRTSchedulabilityAnalyzer>(std::move(wcrt_calculator), std::move(deadlock_detector));
}

std::unique_ptr<ISchedulabilityAnalyzer> SchedulabilityAnalyzerFactory::create_rate_monotonic_analyzer(
    std::unique_ptr<IWCRTCalculator> wcrt_calculator,
    std::unique_ptr<IDeadlockDetector> deadlock_detector) {
    return std::make_unique<RateMonotonicAnalyzer>(std::move(wcrt_calculator), std::move(deadlock_detector));
}

std::unique_ptr<ISchedulabilityAnalyzer> SchedulabilityAnalyzerFactory::create_analyzer(
    AnalyzerType type,
    std::unique_ptr<IWCRTCalculator> wcrt_calculator,
    std::unique_ptr<IDeadlockDetector> deadlock_detector) {
    
    switch (type) {
        case AnalyzerType::WCRT_BASED:
            return create_wcrt_analyzer(std::move(wcrt_calculator), std::move(deadlock_detector));
        case AnalyzerType::RATE_MONOTONIC:
            return create_rate_monotonic_analyzer(std::move(wcrt_calculator), std::move(deadlock_detector));
        default:
            return nullptr;
    }
}

std::string SchedulabilityAnalysisUtils::generate_schedulability_report(const std::vector<TaskAnalysisResult>& results,
                                                                       const AnalysisStatistics& statistics) {
    std::stringstream report;
    
    report << "可调度性分析报告:\n";
    report << "  总任务数: " << statistics.total_tasks << "\n";
    report << "  可调度任务数: " << statistics.schedulable_tasks << "\n";
    report << "  可调度率: " << std::fixed << std::setprecision(2) 
           << statistics.get_schedulability_ratio() * 100 << "%\n";
    report << "  包含死锁的任务数: " << statistics.deadlock_tasks << "\n";
    report << "  最大WCRT: " << statistics.max_wcrt << "\n";
    report << "  最大WCET: " << statistics.max_wcet << "\n";
    report << "  整体可调度性: " << (statistics.is_overall_schedulable() ? "可调度" : "不可调度") << "\n\n";
    
    report << "详细结果:\n";
    for (const auto& result : results) {
        report << result.analysis_info << "\n\n";
    }
    
    return report.str();
}

bool SchedulabilityAnalysisUtils::check_schedulability_constraint(int wcrt, int deadline) {
    return wcrt > 0 && deadline > 0 && wcrt <= deadline;
}

double SchedulabilityAnalysisUtils::calculate_schedulability_margin(int wcrt, int deadline) {
    if (deadline <= 0 || wcrt < 0) {
        return 0.0;
    }
    
    if (wcrt > deadline) {
        return 0.0;
    }
    
    return static_cast<double>(deadline - wcrt) / deadline;
}

int SchedulabilityAnalysisUtils::assess_schedulability_risk(const std::vector<TaskAnalysisResult>& results) {
    if (results.empty()) {
        return 1;
    }
    
    int risk_level = 1;
    int unschedulable_count = 0;
    int deadlock_count = 0;
    
    for (const auto& result : results) {
        if (!result.schedulable) {
            unschedulable_count++;
        }
        if (result.has_deadlock) {
            deadlock_count++;
        }
    }
    
    double unschedulable_ratio = static_cast<double>(unschedulable_count) / results.size();
    double deadlock_ratio = static_cast<double>(deadlock_count) / results.size();
    
    if (unschedulable_ratio > 0.5) {
        risk_level += 3;
    } else if (unschedulable_ratio > 0.2) {
        risk_level += 2;
    } else if (unschedulable_ratio > 0.0) {
        risk_level += 1;
    }
    
    if (deadlock_ratio > 0.3) {
        risk_level += 2;
    } else if (deadlock_ratio > 0.0) {
        risk_level += 1;
    }
    
    return std::min(risk_level, 5);
}

} // namespace task_analysis
