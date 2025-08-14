#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/task.h>
#include <mach/mach_init.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

#include "clap.h"
#include "priority_time_petri_net.h"
#include "priority_state_class.h"
#include "priority_state_graph.h"
#include <boost/program_options.hpp>
#include <iostream>
#include <spdlog/spdlog.h>
#include <chrono>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>

using namespace std;
namespace po = program_options;

// 获取当前进程的内存使用情况（以MB为单位）
size_t get_memory_usage()
{
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
  {
    return pmc.WorkingSetSize / 1024 / 1024;
  }
  return 0;
#elif defined(__APPLE__)
  task_t task = mach_task_self();
  struct task_basic_info t_info;
  mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
  if (task_info(task, TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count) == KERN_SUCCESS)
  {
    return t_info.resident_size / 1024 / 1024;
  }
  return 0;
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0)
  {
    return usage.ru_maxrss / 1024;
  }
  return 0;
#endif
}

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
  std::cout << "  ./PPTPN --cpus 2 --cores 4 --file my_task.dot --tina my_petri.net" << std::endl;
  std::cout << "  ./PPTPN --cpus 2 --cores 4 --file my_task.dot --romeo my_petri.xml" << std::endl;
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
  std::string tina_file_path;
  std::string romeo_file_path;
  size_t max_states = 100; // 截断的最大状态数

  po::options_description desc("选项");
  desc.add_options()("help", "显示帮助信息")("deadline", po::value<int>(&deadline)->default_value(0), "设置截止时间进行检查")("file", po::value<std::string>(&file_path)->default_value("dag.dot"), "指定Petri网的dot文件路径")("scg_type", po::value<std::string>(&scg_type)->default_value("original"), "状态类图算法类型:priority或 differential")("cpus", po::value<int>(&num_cpus)->required(), "CPU数量")("cores", po::value<int>(&cores_per_cpu)->required(), "每个CPU的核心数")("max_states", po::value<size_t>(&max_states)->default_value(100), "状态类图生成的最大状态数限制")("import_dot", po::value<std::string>(), "从DOT文件导入Petri网")("tina", po::value<std::string>(&tina_file_path)->implicit_value("ptpn.net"), "导出为Tina .net格式")("romeo", po::value<std::string>(&romeo_file_path)->implicit_value("ptpn.xml"), "导出为Romeo XML格式");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help"))
  {
    print_usage(desc);
    return 1;
  }

  size_t initial_memory = get_memory_usage();
  auto start_time = std::chrono::high_resolution_clock::now();

  // 解析TDG并生成Petri网
  auto tdg_start = std::chrono::high_resolution_clock::now();
  TDG tdg_rap(file_path, num_cpus, cores_per_cpu);
  tdg_rap.parse_tdg();
  tdg_rap.classify_priority();
  ptpn::PriorityTPN ptpn;
  ptpn.transform_tdg_to_ptpn(tdg_rap);
  ptpn.save_ptpn_and_dot("ptpn.dot");
  auto tdg_end = std::chrono::high_resolution_clock::now();
  auto tdg_duration = std::chrono::duration_cast<std::chrono::milliseconds>(tdg_end - tdg_start);
  size_t tdg_memory = get_memory_usage() - initial_memory;

  std::cout << "\nPetri网生成统计:" << std::endl;
  std::cout << "  时间: " << tdg_duration.count() << " 毫秒" << std::endl;
  std::cout << "  内存使用: " << tdg_memory << " KB" << std::endl;

  // 如果指定了tina选项，则导出为Tina格式
  if (vm.count("tina"))
  {
    std::cout << "导出为Tina格式: " << tina_file_path << std::endl;
    if (!ptpn.export_to_tina(tina_file_path))
    {
      std::cerr << "导出Tina格式失败" << std::endl;
      return 1;
    }
  }

  // 如果指定了romeo选项，则导出为Romeo格式
  if (vm.count("romeo"))
  {
    std::cout << "导出为Romeo格式: " << romeo_file_path << std::endl;
    if (!ptpn.export_to_romeo(romeo_file_path))
    {
      std::cerr << "导出Romeo格式失败" << std::endl;
      return 1;
    }
  }

  // 根据命令行参数选择使用哪种状态类算法
  if (scg_type == "priority")
  {
    std::cout << "\n使用优先级时间Petri网状态类算法..." << std::endl;

    // 记录状态类生成前的内存使用
    size_t scg_start_memory = get_memory_usage();
    auto scg_start = std::chrono::high_resolution_clock::now();

    // 使用优先级状态类分析器
    priority_scg::PriorityStateClassGraph priority_analyzer(ptpn.get_graph());
    priority_analyzer.generate_state_class_graph_with_limit(max_states);

    auto scg_end = std::chrono::high_resolution_clock::now();
    auto scg_duration = std::chrono::duration_cast<std::chrono::milliseconds>(scg_end - scg_start);
    size_t scg_memory = get_memory_usage() - scg_start_memory;

    std::cout << "\n状态类生成统计:" << std::endl;
    std::cout << "  时间: " << scg_duration.count() << " 毫秒" << std::endl;
    std::cout << "  内存使用: " << scg_memory << " KB" << std::endl;

    if (!priority_analyzer.save_to_dot("priority_state_classes.dot"))
    {
      std::cerr << "保存DOT文件失败" << std::endl;
    }
    if (!priority_analyzer.save_to_json("priority_state_classes.json"))
    {
      std::cerr << "保存JSON文件失败" << std::endl;
    }

    priority_analyzer.print_graph_info();
  }

  // 输出总体统计信息
  auto end_time = std::chrono::high_resolution_clock::now();
  auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  size_t total_memory = get_memory_usage() - initial_memory;

  std::cout << "\n总体统计:" << std::endl;
  std::cout << "  总时间: " << total_duration.count() << " 毫秒" << std::endl;
  std::cout << "  总内存使用: " << total_memory << " KB" << std::endl;

  return 0;
}
