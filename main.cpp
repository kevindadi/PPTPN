#include "clap.h"
#include "priority_time_petri_net.h"
#include "priority_state_class.h"
#include "priority_state_graph.h"
#include <boost/program_options.hpp>
#include <iostream>
#include <spdlog/spdlog.h>

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
  spdlog::set_level(spdlog::level::info);
  int deadline;
  int num_cpus;
  int cores_per_cpu;
  std::string file_path;
  std::string dot_style;
  std::string scg_type;

  po::options_description desc("选项");
  desc.add_options()("help", "显示帮助信息")("deadline", po::value<int>(&deadline)->default_value(0), "设置截止时间进行检查")("file", po::value<std::string>(&file_path)->default_value("dag.dot"), "指定Petri网的dot文件路径")("scg_type", po::value<std::string>(&scg_type)->default_value("original"), "状态类图算法类型:priority或 differential")("cpus", po::value<int>(&num_cpus)->required(), "CPU数量")("cores", po::value<int>(&cores_per_cpu)->required(), "每个CPU的核心数");

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
  ptpn.save_ptpn_and_dot("ptpn.dot");

  // 根据命令行参数选择使用哪种状态类算法
  if (scg_type == "priority")
  {
    std::cout << "使用优先级时间Petri网状态类算法..." << std::endl;

    // 使用优先级状态类分析器
    priority_scg::PriorityStateClassGraph priority_analyzer(ptpn.get_graph());

    priority_analyzer.generate_state_class_graph();

    // 导出状态类图到DOT文件
    // priority_analyzer.export_to_dot("priority_state_classes.dot");
  }

  return 0;
}
