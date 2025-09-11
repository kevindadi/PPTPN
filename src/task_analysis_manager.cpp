#include "task_analysis_manager.h"
#include "priority_state_graph.h"
#include <boost/log/trivial.hpp>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace task_analysis {

// TaskAnalysisManager 实现

TaskAnalysisManager::TaskAnalysisManager(const AnalysisConfig& config)
    : config_(config) {
    BOOST_LOG_TRIVIAL(info) << "[ANALYSIS MANAGER] 创建任务分析管理器";
}

void TaskAnalysisManager::set_wcrt_calculator(std::unique_ptr<IWCRTCalculator> calculator) {
    wcrt_calculator_ = std::move(calculator);
    BOOST_LOG_TRIVIAL(debug) << "[ANALYSIS MANAGER] 设置WCRT计算器";
}

void TaskAnalysisManager::set_deadlock_detector(std::unique_ptr<IDeadlockDetector> detector) {
    deadlock_detector_ = std::move(detector);
    BOOST_LOG_TRIVIAL(debug) << "[ANALYSIS MANAGER] 设置死锁检测器";
}

void TaskAnalysisManager::set_schedulability_analyzer(std::unique_ptr<ISchedulabilityAnalyzer> analyzer) {
    schedulability_analyzer_ = std::move(analyzer);
    BOOST_LOG_TRIVIAL(debug) << "[ANALYSIS MANAGER] 设置可调度性分析器";
}

void TaskAnalysisManager::update_config(const AnalysisConfig& config) {
    config_ = config;
    BOOST_LOG_TRIVIAL(debug) << "[ANALYSIS MANAGER] 更新分析配置";
}

const AnalysisConfig& TaskAnalysisManager::get_config() const {
    return config_;
}

std::vector<TaskAnalysisResult> TaskAnalysisManager::perform_complete_analysis() {
    BOOST_LOG_TRIVIAL(info) << "[ANALYSIS MANAGER] 开始执行完整任务分析";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    if (!validate_components()) {
        BOOST_LOG_TRIVIAL(error) << "[ANALYSIS MANAGER] 组件验证失败，无法执行分析";
        return {};
    }
    
    std::vector<TaskAnalysisResult> results;
    
    try {
        // 执行可调度性分析（包含WCRT/WCET计算）
        if (config_.enable_schedulability_check && schedulability_analyzer_) {
            results = perform_schedulability_analysis();
        }
        
        // 执行死锁分析
        if (config_.enable_deadlock_detection && deadlock_detector_) {
            auto deadlock_result = perform_deadlock_analysis();
            
            // 将死锁信息合并到任务分析结果中
            for (auto& result : results) {
                result.has_deadlock = deadlock_detector_->has_deadlock(result.task_name);
            }
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        update_analysis_time(start_time, end_time);
        
        BOOST_LOG_TRIVIAL(info) << "[ANALYSIS MANAGER] 完整任务分析完成，共分析 " << results.size() << " 个任务";
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[ANALYSIS MANAGER] 执行完整分析时出错: " << e.what();
    }
    
    return results;
}

TaskAnalysisResult TaskAnalysisManager::analyze_single_task(const std::string& task_name) {
    BOOST_LOG_TRIVIAL(info) << "[ANALYSIS MANAGER] 开始分析单个任务: " << task_name;
    
    if (!validate_components()) {
        BOOST_LOG_TRIVIAL(error) << "[ANALYSIS MANAGER] 组件验证失败，无法执行分析";
        return TaskAnalysisResult(task_name);
    }
    
    TaskAnalysisResult result(task_name);
    
    try {
        // 计算WCRT和WCET
        if (config_.enable_wcrt_analysis && wcrt_calculator_) {
            result.wcrt = wcrt_calculator_->calculate_wcrt(task_name);
        }
        
        if (config_.enable_wcet_analysis && wcrt_calculator_) {
            result.wcet = wcrt_calculator_->calculate_wcet(task_name);
        }
        
        // 检查死锁
        if (config_.enable_deadlock_detection && deadlock_detector_) {
            result.has_deadlock = deadlock_detector_->has_deadlock(task_name);
        }
        
        // 检查可调度性
        if (config_.should_check_schedulability() && schedulability_analyzer_) {
            result.schedulable = schedulability_analyzer_->analyze_schedulability(task_name, config_.deadline);
        } else {
            result.schedulable = !result.has_deadlock;
        }
        
        // 构建分析信息
        std::stringstream info;
        info << "任务 " << task_name << " 分析结果:\n";
        info << "  WCRT: " << result.wcrt << "\n";
        info << "  WCET: " << result.wcet << "\n";
        info << "  包含死锁: " << (result.has_deadlock ? "是" : "否") << "\n";
        info << "  可调度性: " << result.get_schedulability_string();
        
        result.analysis_info = info.str();
        
        BOOST_LOG_TRIVIAL(info) << "[ANALYSIS MANAGER] 任务 " << task_name << " 分析完成";
        
    } catch (const std::exception& e) {
        result.schedulable = false;
        result.analysis_info = "分析失败: " + std::string(e.what());
        BOOST_LOG_TRIVIAL(error) << "[ANALYSIS MANAGER] 分析任务 " << task_name << " 时出错: " << e.what();
    }
    
    return result;
}

DeadlockAnalysisResult TaskAnalysisManager::perform_deadlock_analysis() {
    BOOST_LOG_TRIVIAL(info) << "[ANALYSIS MANAGER] 开始死锁分析";
    
    if (!deadlock_detector_) {
        BOOST_LOG_TRIVIAL(warning) << "[ANALYSIS MANAGER] 死锁检测器未设置，跳过死锁分析";
        return DeadlockAnalysisResult{};
    }
    
    try {
        auto result = deadlock_detector_->detect_deadlocks();
        BOOST_LOG_TRIVIAL(info) << "[ANALYSIS MANAGER] 死锁分析完成: " 
                                << (result.has_deadlock ? "发现死锁" : "无死锁");
        return result;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[ANALYSIS MANAGER] 死锁分析时出错: " << e.what();
        DeadlockAnalysisResult error_result;
        error_result.has_deadlock = true;
        error_result.deadlock_reason = "死锁分析失败: " + std::string(e.what());
        return error_result;
    }
}

std::vector<TaskAnalysisResult> TaskAnalysisManager::perform_schedulability_analysis() {
    BOOST_LOG_TRIVIAL(info) << "[ANALYSIS MANAGER] 开始可调度性分析";
    
    if (!schedulability_analyzer_) {
        BOOST_LOG_TRIVIAL(warning) << "[ANALYSIS MANAGER] 可调度性分析器未设置，跳过可调度性分析";
        return {};
    }
    
    try {
        int deadline = config_.should_check_schedulability() ? config_.deadline : -1;
        auto results = schedulability_analyzer_->analyze_all_tasks(deadline);
        BOOST_LOG_TRIVIAL(info) << "[ANALYSIS MANAGER] 可调度性分析完成，共分析 " << results.size() << " 个任务";
        return results;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[ANALYSIS MANAGER] 可调度性分析时出错: " << e.what();
        return {};
    }
}

const AnalysisStatistics& TaskAnalysisManager::get_statistics() const {
    return statistics_;
}

void TaskAnalysisManager::reset_statistics() {
    statistics_ = AnalysisStatistics{};
    BOOST_LOG_TRIVIAL(debug) << "[ANALYSIS MANAGER] 重置统计信息";
}

std::string TaskAnalysisManager::generate_analysis_report() const {
    std::stringstream report;
    
    report << "任务分析报告\n";
    report << "==================\n\n";
    
    report << "配置信息:\n";
    report << "  WCRT分析: " << (config_.enable_wcrt_analysis ? "启用" : "禁用") << "\n";
    report << "  WCET分析: " << (config_.enable_wcet_analysis ? "启用" : "禁用") << "\n";
    report << "  可调度性检查: " << (config_.enable_schedulability_check ? "启用" : "禁用") << "\n";
    report << "  死锁检测: " << (config_.enable_deadlock_detection ? "启用" : "禁用") << "\n";
    if (config_.should_check_schedulability()) {
        report << "  截止时间: " << config_.deadline << "\n";
    }
    report << "  最大分析深度: " << config_.max_analysis_depth << "\n\n";
    
    report << "统计信息:\n";
    report << "  总任务数: " << statistics_.total_tasks << "\n";
    report << "  可调度任务数: " << statistics_.schedulable_tasks << "\n";
    report << "  包含死锁的任务数: " << statistics_.deadlock_tasks << "\n";
    report << "  最大WCRT: " << statistics_.max_wcrt << "\n";
    report << "  最大WCET: " << statistics_.max_wcet << "\n";
    report << "  分析时间: " << std::fixed << std::setprecision(2) << statistics_.analysis_time_ms << " 毫秒\n";
    report << "  可调度率: " << std::fixed << std::setprecision(2) 
           << statistics_.get_schedulability_ratio() * 100 << "%\n";
    report << "  整体可调度性: " << (statistics_.is_overall_schedulable() ? "可调度" : "不可调度") << "\n";
    
    return report.str();
}

bool TaskAnalysisManager::save_results_to_file(const std::string& filename, const std::string& format) const {
    BOOST_LOG_TRIVIAL(info) << "[ANALYSIS MANAGER] 保存分析结果到文件: " << filename << " (格式: " << format << ")";
    
    try {
        std::string content;
        
        if (format == "json") {
            content = generate_json_report();
        } else if (format == "xml") {
            content = generate_xml_report();
        } else if (format == "csv") {
            content = generate_csv_report();
        } else {
            BOOST_LOG_TRIVIAL(error) << "[ANALYSIS MANAGER] 不支持的格式: " << format;
            return false;
        }
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            BOOST_LOG_TRIVIAL(error) << "[ANALYSIS MANAGER] 无法打开文件: " << filename;
            return false;
        }
        
        file << content;
        file.close();
        
        BOOST_LOG_TRIVIAL(info) << "[ANALYSIS MANAGER] 成功保存分析结果到文件: " << filename;
        return true;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[ANALYSIS MANAGER] 保存文件时出错: " << e.what();
        return false;
    }
}

bool TaskAnalysisManager::validate_components() const {
    bool valid = true;
    
    if (config_.enable_wcrt_analysis && !wcrt_calculator_) {
        BOOST_LOG_TRIVIAL(warning) << "[ANALYSIS MANAGER] WCRT计算器未设置但WCRT分析已启用";
        valid = false;
    }
    
    if (config_.enable_deadlock_detection && !deadlock_detector_) {
        BOOST_LOG_TRIVIAL(warning) << "[ANALYSIS MANAGER] 死锁检测器未设置但死锁检测已启用";
        valid = false;
    }
    
    if (config_.enable_schedulability_check && !schedulability_analyzer_) {
        BOOST_LOG_TRIVIAL(warning) << "[ANALYSIS MANAGER] 可调度性分析器未设置但可调度性检查已启用";
        valid = false;
    }
    
    return valid;
}

void TaskAnalysisManager::update_analysis_time(const std::chrono::high_resolution_clock::time_point& start_time,
                                              const std::chrono::high_resolution_clock::time_point& end_time) {
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    statistics_.analysis_time_ms = duration.count();
}

std::string TaskAnalysisManager::generate_json_report() const {
    std::stringstream json;
    
    json << "{\n";
    json << "  \"analysis_config\": {\n";
    json << "    \"enable_wcrt_analysis\": " << (config_.enable_wcrt_analysis ? "true" : "false") << ",\n";
    json << "    \"enable_wcet_analysis\": " << (config_.enable_wcet_analysis ? "true" : "false") << ",\n";
    json << "    \"enable_schedulability_check\": " << (config_.enable_schedulability_check ? "true" : "false") << ",\n";
    json << "    \"enable_deadlock_detection\": " << (config_.enable_deadlock_detection ? "true" : "false") << ",\n";
    json << "    \"deadline\": " << config_.deadline << ",\n";
    json << "    \"max_analysis_depth\": " << config_.max_analysis_depth << "\n";
    json << "  },\n";
    json << "  \"statistics\": {\n";
    json << "    \"total_tasks\": " << statistics_.total_tasks << ",\n";
    json << "    \"schedulable_tasks\": " << statistics_.schedulable_tasks << ",\n";
    json << "    \"deadlock_tasks\": " << statistics_.deadlock_tasks << ",\n";
    json << "    \"max_wcrt\": " << statistics_.max_wcrt << ",\n";
    json << "    \"max_wcet\": " << statistics_.max_wcet << ",\n";
    json << "    \"analysis_time_ms\": " << std::fixed << std::setprecision(2) << statistics_.analysis_time_ms << ",\n";
    json << "    \"schedulability_ratio\": " << std::fixed << std::setprecision(4) << statistics_.get_schedulability_ratio() << ",\n";
    json << "    \"is_overall_schedulable\": " << (statistics_.is_overall_schedulable() ? "true" : "false") << "\n";
    json << "  }\n";
    json << "}\n";
    
    return json.str();
}

std::string TaskAnalysisManager::generate_xml_report() const {
    std::stringstream xml;
    
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<analysis_report>\n";
    xml << "  <config>\n";
    xml << "    <wcrt_analysis>" << (config_.enable_wcrt_analysis ? "enabled" : "disabled") << "</wcrt_analysis>\n";
    xml << "    <wcet_analysis>" << (config_.enable_wcet_analysis ? "enabled" : "disabled") << "</wcet_analysis>\n";
    xml << "    <schedulability_check>" << (config_.enable_schedulability_check ? "enabled" : "disabled") << "</schedulability_check>\n";
    xml << "    <deadlock_detection>" << (config_.enable_deadlock_detection ? "enabled" : "disabled") << "</deadlock_detection>\n";
    xml << "    <deadline>" << config_.deadline << "</deadline>\n";
    xml << "    <max_analysis_depth>" << config_.max_analysis_depth << "</max_analysis_depth>\n";
    xml << "  </config>\n";
    xml << "  <statistics>\n";
    xml << "    <total_tasks>" << statistics_.total_tasks << "</total_tasks>\n";
    xml << "    <schedulable_tasks>" << statistics_.schedulable_tasks << "</schedulable_tasks>\n";
    xml << "    <deadlock_tasks>" << statistics_.deadlock_tasks << "</deadlock_tasks>\n";
    xml << "    <max_wcrt>" << statistics_.max_wcrt << "</max_wcrt>\n";
    xml << "    <max_wcet>" << statistics_.max_wcet << "</max_wcet>\n";
    xml << "    <analysis_time_ms>" << std::fixed << std::setprecision(2) << statistics_.analysis_time_ms << "</analysis_time_ms>\n";
    xml << "    <schedulability_ratio>" << std::fixed << std::setprecision(4) << statistics_.get_schedulability_ratio() << "</schedulability_ratio>\n";
    xml << "    <overall_schedulable>" << (statistics_.is_overall_schedulable() ? "true" : "false") << "</overall_schedulable>\n";
    xml << "  </statistics>\n";
    xml << "</analysis_report>\n";
    
    return xml.str();
}

std::string TaskAnalysisManager::generate_csv_report() const {
    std::stringstream csv;
    
    csv << "指标,值\n";
    csv << "总任务数," << statistics_.total_tasks << "\n";
    csv << "可调度任务数," << statistics_.schedulable_tasks << "\n";
    csv << "包含死锁的任务数," << statistics_.deadlock_tasks << "\n";
    csv << "最大WCRT," << statistics_.max_wcrt << "\n";
    csv << "最大WCET," << statistics_.max_wcet << "\n";
    csv << "分析时间(毫秒)," << std::fixed << std::setprecision(2) << statistics_.analysis_time_ms << "\n";
    csv << "可调度率," << std::fixed << std::setprecision(4) << statistics_.get_schedulability_ratio() << "\n";
    csv << "整体可调度性," << (statistics_.is_overall_schedulable() ? "是" : "否") << "\n";
    
    return csv.str();
}

// TaskAnalysisManagerFactory 实现

std::unique_ptr<TaskAnalysisManager> TaskAnalysisManagerFactory::create_simple_manager(
    const ptpn::PriorityTPNGraph& petri_net,
    const AnalysisConfig& config) {
    
    auto manager = std::make_unique<TaskAnalysisManager>(config);
    
    // 创建静态计算器
    auto wcrt_calculator = WCRTCalculatorFactory::create_static_calculator(petri_net);
    manager->set_wcrt_calculator(std::move(wcrt_calculator));
    
    // 创建Petri网死锁检测器
    auto deadlock_detector = DeadlockDetectorFactory::create_petri_net_detector(petri_net);
    manager->set_deadlock_detector(std::move(deadlock_detector));
    
    // 创建基于WCRT的可调度性分析器
    auto wcrt_calc = WCRTCalculatorFactory::create_static_calculator(petri_net);
    auto deadlock_det = DeadlockDetectorFactory::create_petri_net_detector(petri_net);
    auto schedulability_analyzer = SchedulabilityAnalyzerFactory::create_wcrt_analyzer(
        std::move(wcrt_calc), std::move(deadlock_det));
    manager->set_schedulability_analyzer(std::move(schedulability_analyzer));
    
    BOOST_LOG_TRIVIAL(info) << "[ANALYSIS MANAGER FACTORY] 创建简单管理器";
    
    return manager;
}

// TaskAnalysisManagerFactory 模板方法实现

template<typename StateGraphType>
std::unique_ptr<TaskAnalysisManager> TaskAnalysisManagerFactory::create_advanced_manager(
    const ptpn::PriorityTPNGraph& petri_net,
    const StateGraphType& state_graph,
    const AnalysisConfig& config) {
    
    auto manager = std::make_unique<TaskAnalysisManager>(config);
    
    // 创建WCRT计算器
    auto wcrt_calculator = WCRTCalculatorFactory::create_static_calculator(petri_net);
    manager->set_wcrt_calculator(std::move(wcrt_calculator));
    
    // 创建死锁检测器
    auto deadlock_detector = DeadlockDetectorFactory::create_state_graph_detector<StateGraphType, typename StateGraphType::vertex_descriptor, priority_scg::PriorityStateClass>(state_graph, petri_net);
    manager->set_deadlock_detector(std::move(deadlock_detector));
    
    // 创建可调度性分析器
    auto schedulability_analyzer = SchedulabilityAnalyzerFactory::create_wcrt_analyzer(
        WCRTCalculatorFactory::create_static_calculator(petri_net),
        DeadlockDetectorFactory::create_state_graph_detector<StateGraphType, typename StateGraphType::vertex_descriptor, priority_scg::PriorityStateClass>(state_graph, petri_net)
    );
    manager->set_schedulability_analyzer(std::move(schedulability_analyzer));
    
    BOOST_LOG_TRIVIAL(info) << "[ANALYSIS MANAGER FACTORY] 创建高级管理器";
    
    return manager;
}

// 显式实例化模板
template std::unique_ptr<TaskAnalysisManager> TaskAnalysisManagerFactory::create_advanced_manager<priority_scg::StateClassGraph>(
    const ptpn::PriorityTPNGraph& petri_net,
    const priority_scg::StateClassGraph& state_graph,
    const AnalysisConfig& config);

} // namespace task_analysis
