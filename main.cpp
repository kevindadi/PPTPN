#include "clap.h"
#include "priority_time_petri_net.h"
#include "priority_state_class.h"
#include "differential_state_class.h"
#include <boost/program_options.hpp>
#include <iostream>

namespace po = boost::program_options;

// 显示使用说明
void print_usage(const po::options_description &desc)
{
  std::cout << "优先级时间Petri网分析工具 (PPTPN)" << std::endl;
  std::cout << "用法: ./PPTPN --cpus <num_cpus> --cores <cores_per_cpu> [选项]" << std::endl;
  std::cout << std::endl;
  std::cout << desc << std::endl;
  std::cout << "示例:" << std::endl;
  std::cout << "  ./PPTPN --cpus 2 --cores 4 --file my_task.dot --scg_type priority" << std::endl;
  std::cout << "  ./PPTPN --cpus 1 --cores 2 --file simple.dot --scg_type differential --deadline 100" << std::endl;
}

int main(int argc, char *argv[])
{
  int deadline;
  int num_cpus;
  int cores_per_cpu;
  std::string file_path;
  std::string dot_style;
  std::string scg_type;
  bool test_mode = false;

  po::options_description desc("选项");
  desc.add_options()("help", "显示帮助信息")("deadline", po::value<int>(&deadline)->default_value(0), "设置截止时间进行检查")("style", po::value<std::string>(&dot_style)->default_value("NEWPN"), "点文件样式，支持 PSTPN 或 PTPN")("file", po::value<std::string>(&file_path)->default_value("dag.dot"), "指定Petri网的dot文件路径")("scg_type", po::value<std::string>(&scg_type)->default_value("original"), "状态类图算法类型：original、priority、priority_corrected 或 differential")("cpus", po::value<int>(&num_cpus)->required(), "CPU数量")("cores", po::value<int>(&cores_per_cpu)->required(), "每个CPU的核心数")("test", po::bool_switch(&test_mode), "运行测试模式");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help"))
  {
    print_usage(desc);
    return 1;
  }

  TDG tdg_rap(file_path, num_cpus, cores_per_cpu);
  tdg_rap.parse_tdg();
  tdg_rap.classify_priority();
  ptpn::PriorityTPN ptpn;
  ptpn.transform_tdg_to_ptpn(tdg_rap);

  // 根据命令行参数选择使用哪种状态类算法
  if (scg_type == "priority")
  {
    std::cout << "使用优先级时间Petri网状态类算法..." << std::endl;

    // 使用优先级状态类分析器
    priority_scg::PriorityStateClassAnalyzer priority_analyzer(ptpn.get_graph());

    if (test_mode)
    {
      priority_analyzer.test_state_class_generation();
      return 0;
    }

    priority_analyzer.generate_state_class_graph();

    // 导出状态类图到DOT文件
    priority_analyzer.export_to_dot("priority_state_classes.dot");

    // 检查是否有死锁状态
    if (priority_analyzer.has_deadlock_states())
    {
      std::cout << "检测到死锁状态！" << std::endl;
      auto deadlock_states = priority_analyzer.get_deadlock_states();
      std::cout << "死锁状态数量: " << deadlock_states.size() << std::endl;
    }
    else
    {
      std::cout << "没有检测到死锁状态。" << std::endl;
    }

    // 计算最大执行时间
    auto execution_time = priority_analyzer.calculate_max_execution_time();
    std::cout << "最大执行时间区间: [" << execution_time.lower << ", "
              << (execution_time.upper == INT_MAX ? "∞" : std::to_string(execution_time.upper))
              << "]" << std::endl;
  }
  else if (scg_type == "priority_corrected")
  {
    std::cout << "使用修正后的优先级时间Petri网状态类算法..." << std::endl;

    // 使用优先级状态类分析器
    priority_scg::PriorityStateClassAnalyzer priority_analyzer(ptpn.get_graph());

    if (test_mode)
    {
      priority_analyzer.test_state_class_generation();
      return 0;
    }

    priority_analyzer.generate_state_class_graph_corrected();

    // 导出状态类图到DOT文件
    priority_analyzer.export_to_dot("priority_corrected_state_classes.dot");

    // 检查是否有死锁状态
    if (priority_analyzer.has_deadlock_states())
    {
      std::cout << "检测到死锁状态！" << std::endl;
      auto deadlock_states = priority_analyzer.get_deadlock_states();
      std::cout << "死锁状态数量: " << deadlock_states.size() << std::endl;
    }
    else
    {
      std::cout << "没有检测到死锁状态。" << std::endl;
    }

    // 计算最大执行时间
    auto execution_time = priority_analyzer.calculate_max_execution_time();
    std::cout << "最大执行时间区间: [" << execution_time.lower << ", "
              << (execution_time.upper == INT_MAX ? "∞" : std::to_string(execution_time.upper))
              << "]" << std::endl;
  }
  else if (scg_type == "differential")
  {
    std::cout << "使用差分边界矩阵状态类算法..." << std::endl;

    // 使用差分边界矩阵状态类分析器
    differential_scg::DifferentialStateClassAnalyzer differential_analyzer(ptpn.get_graph());
    differential_analyzer.generate_state_class_graph();

    // 导出状态类图到DOT文件
    differential_analyzer.export_to_dot("differential_state_classes.dot");

    // 检查是否有死锁状态
    if (differential_analyzer.has_deadlock_states())
    {
      std::cout << "检测到死锁状态！" << std::endl;
      auto deadlock_states = differential_analyzer.get_deadlock_states();
      std::cout << "死锁状态数量: " << deadlock_states.size() << std::endl;
    }
    else
    {
      std::cout << "没有检测到死锁状态。" << std::endl;
    }

    // 计算最大执行时间
    auto execution_time = differential_analyzer.calculate_max_execution_time();
    std::cout << "最大执行时间区间: [" << execution_time.lower << ", "
              << (std::isinf(execution_time.upper) ? "∞" : std::to_string(execution_time.upper))
              << "]" << std::endl;
  }
  else if (test_mode)
  {
    // 如果指定了测试模式，但没有指定具体的算法，默认使用修正后的优先级算法测试
    priority_scg::PriorityStateClassAnalyzer priority_analyzer(ptpn.get_graph());
    priority_analyzer.test_state_class_generation();
  }

  return 0;
}
