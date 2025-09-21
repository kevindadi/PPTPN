#if defined(_WIN32)
#include <psapi.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_init.h>
#include <mach/task.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

#include "clap.h"
#include "priority_state_graph.h"
#include "priority_time_petri_net.h"
#include "task_analysis_manager.h"
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <iostream>
#include <limits>
#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

using namespace std;
namespace po = boost::program_options;

// 获取当前进程的内存使用情况（以MB为单位）
size_t get_memory_usage() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    return pmc.WorkingSetSize / 1024 / 1024;
  }
  return 0;
#elif defined(__APPLE__)
  const task_t task = mach_task_self();
  struct task_basic_info t_info{};
  mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
  if (task_info(task, TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&t_info), &t_info_count) ==
      KERN_SUCCESS) {
    return t_info.resident_size / 1024 / 1024;
  }
  return 0;
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    return usage.ru_maxrss / 1024;
  }
  return 0;
#endif
}

// 显示使用说明
void print_usage(const po::options_description &desc) {
  std::cout << "优先级时间Petri网分析工具 (PPTPN)" << std::endl;
  std::cout << "用法: ./PPTPN --cpus <num_cpus> --cores <cores_per_cpu> [选项]"
            << std::endl;
  std::cout << std::endl;
  std::cout << desc << std::endl;
  std::cout << "示例:" << std::endl;
  std::cout
      << "  ./PPTPN --cpus 2 --cores 4 --file my_task.dot "
      << std::endl;
  std::cout << "  ./PPTPN --cpus 1 --cores 2 --file simple.dot --deadline 100"
            << std::endl;
  std::cout
      << "  ./PPTPN --cpus 2 --cores 4 --file my_task.dot --tina my_petri.net"
      << std::endl;
  std::cout
      << "  ./PPTPN --cpus 2 --cores 4 --file my_task.dot --romeo my_petri.xml"
      << std::endl;
}

int main(int argc, char *argv[]) {
  // 初始化 Boost.Log：控制台输出与日志级别
  boost::log::add_console_log(std::clog);
  boost::log::add_common_attributes();
  boost::log::core::get()->set_filter(
      boost::log::trivial::severity >= boost::log::trivial::info);
  int deadline;
  int num_cpus;
  int cores_per_cpu;
  std::string file_path;
  std::string dot_style;
  std::string scg_type;
  std::string tina_file_path;
  std::string romeo_file_path;
  size_t max_states = std::numeric_limits<size_t>::max(); // 默认无限制

  po::options_description desc("选项");
  desc.add_options()("help", "显示帮助信息")(
      "deadline", po::value<int>(&deadline)->default_value(0),
      "设置截止时间进行检查")(
      "file", po::value<std::string>(&file_path)->default_value("dag.dot"),
      "指定Petri网的dot文件路径")(
      "cpus", po::value<int>(&num_cpus)->required(), "CPU数量")(
      "cores", po::value<int>(&cores_per_cpu)->required(), "每个CPU的核心数")(
      "max_states", po::value<size_t>(&max_states),
      "状态类图生成的最大状态数限制（不指定则无限制）")("import_dot", po::value<std::string>(),
                                      "从DOT文件导入Petri网")(
      "tina",
      po::value<std::string>(&tina_file_path)->implicit_value("ptpn.net"),
      "导出为Tina .net格式")(
      "romeo",
      po::value<std::string>(&romeo_file_path)->implicit_value("ptpn.xml"),
      "导出为Romeo XML格式");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help")) {
    print_usage(desc);
    return 1;
  }

  size_t initial_memory = get_memory_usage();
  auto start_time = std::chrono::high_resolution_clock::now();

  // 解析TDG并生成Petri网
  auto tdg_start = std::chrono::high_resolution_clock::now();
  BOOST_LOG_TRIVIAL(info) << "Starting TDG parsing for file: " << file_path;
  TDG tdg_rap(file_path, num_cpus, cores_per_cpu);
  BOOST_LOG_TRIVIAL(info) << "TDG object created successfully";

  tdg_rap.parse_tdg();
  BOOST_LOG_TRIVIAL(info) << "TDG parsing completed";

  tdg_rap.classify_priority();
  BOOST_LOG_TRIVIAL(info) << "Priority classification completed";

  ptpn::PriorityTPN ptpn;
  BOOST_LOG_TRIVIAL(info) << "PriorityTPN object created";

  ptpn.transform_tdg_to_ptpn(tdg_rap);
  BOOST_LOG_TRIVIAL(info) << "TDG to PTPN transformation completed";

  ptpn.save_ptpn_and_dot("ptpn.dot");
  auto tdg_end = std::chrono::high_resolution_clock::now();
  auto tdg_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      tdg_end - tdg_start);
  size_t tdg_memory = get_memory_usage() - initial_memory;

  BOOST_LOG_TRIVIAL(info) << "\nPetri网生成统计:"<< "  时间: " << tdg_duration.count() << " 毫秒" << "  内存使用: " << tdg_memory << " KB";

  // 如果指定了tina选项，则导出为Tina格式
  if (vm.count("tina")) {
    BOOST_LOG_TRIVIAL(info) << "导出为Tina格式: " << tina_file_path;
    if (!ptpn.export_to_tina(tina_file_path)) {
      BOOST_LOG_TRIVIAL(error) << "导出Tina格式失败";
      return 1;
    }
  }

  // 如果指定了romeo选项，则导出为Romeo格式
  if (vm.count("romeo")) {
    BOOST_LOG_TRIVIAL(info) << "导出为Romeo格式: " << romeo_file_path;
    if (!ptpn.export_to_romeo(romeo_file_path)) {
      BOOST_LOG_TRIVIAL(error) << "导出Romeo格式失败";
      return 1;
    }
  }

    BOOST_LOG_TRIVIAL(info) << "\n使用优先级时间Petri网状态类算法...";

    // 记录状态类生成前的内存使用
    size_t scg_start_memory = get_memory_usage();
    auto scg_start = std::chrono::high_resolution_clock::now();

    // 使用优先级状态类分析器
    priority_scg::PriorityStateClassGraph priority_analyzer(ptpn.get_graph());
    
    if (max_states == std::numeric_limits<size_t>::max()) {
      BOOST_LOG_TRIVIAL(info) << "状态类生成无限制...";
      priority_analyzer.generate_state_class_graph();
    } else {
      BOOST_LOG_TRIVIAL(info) << "状态类生成限制: " << max_states << " 个状态";
      priority_analyzer.generate_state_class_graph_with_limit(max_states);
    }

    auto scg_end = std::chrono::high_resolution_clock::now();
    auto scg_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        scg_end - scg_start);
    size_t scg_memory = get_memory_usage() - scg_start_memory;

    BOOST_LOG_TRIVIAL(info) << "\n状态类生成统计:"<< "  时间: " << scg_duration.count() << " 毫秒" << "  内存使用: " << scg_memory << " KB";

    if (!priority_analyzer.save_to_dot("priority_state_classes.dot")) {
      BOOST_LOG_TRIVIAL(error) << "保存DOT文件失败";
    }
    if (!priority_analyzer.save_to_json("priority_state_classes.json")) {
      BOOST_LOG_TRIVIAL(error) << "保存JSON文件失败";
    }

    priority_analyzer.print_graph_info();
    
    // 使用重构后的分析系统
    BOOST_LOG_TRIVIAL(info) << "\n开始任务分析...";
    auto analysis_start = std::chrono::high_resolution_clock::now();
    
    // 创建分析配置
    task_analysis::AnalysisConfig config;
    config.enable_wcrt_analysis = true;
    config.enable_wcet_analysis = true;
    config.enable_schedulability_check = true;
    config.enable_deadlock_detection = true;
    config.deadline = deadline > 0 ? deadline : -1;
    config.max_analysis_depth = max_states;
    config.verbose_output = true;
    
    // 创建分析管理器（使用状态类图的高级版本）
    auto manager = task_analysis::TaskAnalysisManagerFactory::create_advanced_manager(
        ptpn.get_graph(), priority_analyzer.get_graph(), config);
    
    // 执行完整分析
    auto analysis_results = manager->perform_complete_analysis();
    
    auto analysis_end = std::chrono::high_resolution_clock::now();
    auto analysis_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        analysis_end - analysis_start);
    
    BOOST_LOG_TRIVIAL(info)<< "\n任务分析统计:" << "  分析时间: " << analysis_duration.count() << " 毫秒" << "  分析任务数: " << analysis_results.size();
    
    // 生成分析报告
    auto report = manager->generate_analysis_report();
    BOOST_LOG_TRIVIAL(info) << "\n分析报告:" << report;
    
    // 获取统计信息
    auto stats = manager->get_statistics();
    std::cout << "\n详细统计:" << std::endl;
    std::cout << "  可调度任务数: " << stats.schedulable_tasks << "/" << stats.total_tasks << std::endl;
    std::cout << "  包含死锁的任务数: " << stats.deadlock_tasks << std::endl;
    std::cout << "  最大WCRT: " << stats.max_wcrt << std::endl;
    std::cout << "  最大WCET: " << stats.max_wcet << std::endl;
    std::cout << "  整体可调度性: " << (stats.is_overall_schedulable() ? "可调度" : "不可调度") << std::endl;
    
    // 保存分析结果
    if (manager->save_results_to_file("analysis_results.json", "json")) {
      std::cout << "\n分析结果已保存到 analysis_results.json" << std::endl;
    }


  // 输出总体统计信息
  auto end_time = std::chrono::high_resolution_clock::now();
  auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);
  size_t total_memory = get_memory_usage() - initial_memory;

  std::cout << "\n总体统计:" << std::endl;
  std::cout << "  总时间: " << total_duration.count() << " 毫秒" << std::endl;
  std::cout << "  总内存使用: " << total_memory << " KB" << std::endl;

  return 0;
}
