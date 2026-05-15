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

#include <spdlog/spdlog.h>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <iostream>
#include <limits>

#include "clap.h"
#include "matrix_ptpn.h"
#include "graph_ptpn.h"
#include "state_class_graph.h"

using namespace std;
namespace po = boost::program_options;

// 获取当前进程的内存使用情况(以MB为单位)
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
  if (task_info(task, TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&t_info),
                &t_info_count) == KERN_SUCCESS) {
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
void print_usage(const po::options_description& desc) {
  std::cout << "Priority Timed Petri Net Analyzer (PTPN)" << std::endl;
  std::cout << "Usage: ./PTPN --cpus <num_cpus> --cores <cores_per_cpu> [options]"
            << std::endl;
  std::cout << std::endl;
  std::cout << desc << std::endl;
  std::cout << "Examples:" << std::endl;
  std::cout << "  ./PTPN --cpus 2 --cores 4 --file tasks.json" << std::endl;
  std::cout << "  ./PTPN --cpus 1 --cores 2 --file tasks.dot" << std::endl;
  std::cout << "  ./PTPN --cpus 2 --cores 4 --file tasks.json --export-dot"
            << std::endl;
  std::cout << "  ./PTPN --cpus 2 --cores 4 --file tasks.dot --max_states 1000"
            << std::endl;
}

int main(int argc, char* argv[]) {
  spdlog::info("==========================================");
  spdlog::info("PTPN - Priority Timed Petri Net Analyzer");
  spdlog::info("==========================================");

  int deadline;
  int num_cpus;
  int cores_per_cpu;
  std::string file_path;
  std::string dot_style;
  std::string scg_type;
  std::string tina_file_path;
  std::string romeo_file_path;
  size_t max_states = std::numeric_limits<size_t>::max();
  bool export_dot = false;

  po::options_description desc("Options");
  desc.add_options()("help", "Show help information")(
      "deadline", po::value<int>(&deadline)->default_value(0),
      "Set deadline for checking")(
      "file", po::value<std::string>(&file_path)->default_value("dag.dot"),
      "Input file path (.json or .dot)")(
      "cpus", po::value<int>(&num_cpus)->required(), "Number of CPUs")(
      "cores", po::value<int>(&cores_per_cpu)->required(),
      "Number of cores per CPU")(
      "max_states", po::value<size_t>(&max_states),
      "Maximum number of states in reachability graph")(
      "export-dot",
      po::value<bool>(&export_dot)->default_value(false)->implicit_value(true),
      "Export input to DOT format as reference")(
      "import_dot", po::value<std::string>(), "Import from DOT file")(
      "tina",
      po::value<std::string>(&tina_file_path)->implicit_value("ptpn.net"),
      "Export to Tina .net format")(
      "romeo",
      po::value<std::string>(&romeo_file_path)->implicit_value("ptpn.xml"),
      "Export to Romeo XML format");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);

  if (vm.count("help")) {
    print_usage(desc);
    return 0;
  }
  po::notify(vm);

  // Detect input format
  auto input_format = TDG::detect_format(file_path);
  if (input_format == InputFormat::JSON) {
    spdlog::info("[INPUT] Detected JSON format: ") << file_path;
  } else {
    spdlog::info("[INPUT] Detected DOT format: ") << file_path;
  }

  size_t initial_memory = get_memory_usage();
  auto start_time = std::chrono::high_resolution_clock::now();

  auto tdg_start = std::chrono::high_resolution_clock::now();
  spdlog::info("[TDG] Starting parsing: ") << file_path;
  TDG tdg_rap(file_path, num_cpus, cores_per_cpu);
  spdlog::info("[TDG] TDG object created successfully");

  // Parse based on detected format
  if (input_format == InputFormat::JSON) {
    tdg_rap.parse_json(file_path);
  } else {
    tdg_rap.parse_tdg();
  }
  spdlog::info("[TDG] Parsing completed");

  // Optional: Export to DOT for reference
  if (export_dot) {
    std::string dot_path = file_path;
    // Replace .json with .dot
    size_t dot_pos = dot_path.rfind('.');
    if (dot_pos != std::string::npos) {
      dot_path = dot_path.substr(0, dot_pos);
    }
    dot_path += ".dot";
    tdg_rap.export_to_dot(dot_path);
  }

  tdg_rap.classify_priority();
  spdlog::info("[TDG] Priority classification completed");

  // ptpn::PriorityTPN ptpn;
  // spdlog::info("PriorityTPN object created");

  // ptpn.transform_tdg_to_ptpn(tdg_rap);
  // spdlog::info("TDG to PTPN transformation completed");

  // ptpn.save_ptpn_and_dot("ptpn.dot");
  auto tdg_end = std::chrono::high_resolution_clock::now();
  auto tdg_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      tdg_end - tdg_start);
  size_t tdg_memory = get_memory_usage() - initial_memory;

  spdlog::info("\n[STATS] Petri Net Generation: ")
                          << tdg_duration.count() << " ms, "
                          << tdg_memory << " KB";

  spdlog::info("\n[PTPN] Converting to Matrix PTPN...");
  matrix_ptpn::MatrixPTPN matrix_ptpn;
  matrix_ptpn.transform_tdg_to_matrix_ptpn(tdg_rap);
  spdlog::info("[PTPN] Matrix PTPN conversion completed");
  spdlog::info("  Places: ") << matrix_ptpn.num_places();
  spdlog::info("  Transitions: ") << matrix_ptpn.num_transitions();

  std::cout << matrix_ptpn.to_string();

  std::string matrix_ptpn_dot_file = "matrix_ptpn.dot";
  graph_ptpn::GraphPTPN graph_ptpn(matrix_ptpn);
  if (graph_ptpn.save_to_dot(matrix_ptpn_dot_file)) {
    spdlog::info("[OUTPUT] Matrix PTPN saved to: ") << matrix_ptpn_dot_file;
  } else {
    spdlog::warn << "[OUTPUT] Failed to save Matrix PTPN";
  }

  spdlog::info("\n[SCG] Building State Class Reachability Graph...");
  size_t scg_start_memory = get_memory_usage();
  auto scg_start = std::chrono::high_resolution_clock::now();
  state_class::StateClassReachabilityGraph scg(matrix_ptpn);
  spdlog::info("[SCG] Starting build (max_states=") << max_states << ")...";

  size_t num_states = scg.build(max_states);
  spdlog::info("[SCG] Build completed");

  const auto& stats = scg.get_statistics();
  spdlog::info("  Total states: ") << stats.total_states;
  spdlog::info("  Total transitions: ") << stats.total_transitions;
  spdlog::info("  Enabled transitions: ") << stats.enabled_transitions_count;
  spdlog::info("  Pruned states: ") << stats.pruned_states_count;

  std::string scg_dot_file = "state_class_graph.dot";
  if (scg.save_to_dot(scg_dot_file)) {
    spdlog::info("[OUTPUT] State class graph (DOT) saved to: ") << scg_dot_file;
  }

  std::string scg_json_file = "state_class_graph.json";
  if (scg.save_to_json(scg_json_file)) {
    spdlog::info("[OUTPUT] State class graph (JSON) saved to: ") << scg_json_file;
  }

  auto scg_end = std::chrono::high_resolution_clock::now();
  auto scg_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      scg_end - scg_start);
  size_t scg_memory = get_memory_usage() - scg_start_memory;

  spdlog::info("\n[STATS] State Class Generation: ")
                          << scg_duration.count() << " ms, "
                          << scg_memory << " KB";

  // spdlog::info("\n开始任务分析...");
  // auto analysis_start = std::chrono::high_resolution_clock::now();

  // task_analysis::AnalysisConfig config;
  // config.enable_wcrt_analysis = true;
  // config.enable_wcet_analysis = true;
  // config.enable_schedulability_check = true;
  // config.enable_deadlock_detection = true;
  // config.deadline = deadline > 0 ? deadline : -1;
  // config.max_analysis_depth = max_states;
  // config.verbose_output = true;

  // auto manager =
  // task_analysis::TaskAnalysisManagerFactory::create_advanced_manager(
  //     ptpn.get_graph(), priority_analyzer.get_graph(), config);

  // auto analysis_results = manager->perform_complete_analysis();

  // auto analysis_end = std::chrono::high_resolution_clock::now();
  // auto analysis_duration =
  // std::chrono::duration_cast<std::chrono::milliseconds>(
  //     analysis_end - analysis_start);

  // spdlog::info("\n任务分析统计:") << "  分析时间: " <<
  // analysis_duration.count() << " 毫秒" << "  分析任务数: " <<
  // analysis_results.size();

  // auto report = manager->generate_analysis_report();
  // spdlog::info("\n分析报告:") << report;

  // auto stats = manager->get_statistics();
  // std::cout << "\n详细统计:" << std::endl;
  // std::cout << "  可调度任务数: " << stats.schedulable_tasks << "/" <<
  // stats.total_tasks << std::endl; std::cout << "  包含死锁的任务数: " <<
  // stats.deadlock_tasks << std::endl; std::cout << "  最大WCRT: " <<
  // stats.max_wcrt << std::endl; std::cout << "  最大WCET: " << stats.max_wcet
  // << std::endl; std::cout << "  整体可调度性: " <<
  // (stats.is_overall_schedulable() ? "可调度" : "不可调度") << std::endl;

  // if (manager->save_results_to_file("analysis_results.json", "json")) {
  //   std::cout << "\n分析结果已保存到 analysis_results.json" << std::endl;
  // }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);
  size_t total_memory = get_memory_usage() - initial_memory;

  std::cout << "\n========================================" << std::endl;
  std::cout << "Total Statistics:" << std::endl;
  std::cout << "  Total time: " << total_duration.count() << " ms" << std::endl;
  std::cout << "  Total memory: " << total_memory << " KB" << std::endl;
  std::cout << "========================================" << std::endl;

  return 0;
}
