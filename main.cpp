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

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <chrono>
#include <iostream>
#include <limits>

#include "clap.h"
#include "json_tdg.h"
#include "matrix_ptpn.h"
#include "graph_ptpn.h"
#include "state_class_graph.h"

using namespace std;

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

int main(int argc, char* argv[]) {
  CLI::App app{"PTPN - Priority Timed Petri Net Analyzer"};

  string input_file;
  size_t max_states = numeric_limits<size_t>::max();
  bool export_dot = false;
  string tina_file, romeo_file;

  app.add_option("-f,--file", input_file, "Input JSON file")
      ->required(true);
  app.add_option("-m,--max-states", max_states, "Maximum number of states in reachability graph");
  app.add_flag("-e,--export-dot", export_dot, "Export TDG as DOT after parsing for verification");
  app.add_option("--tina", tina_file, "Export to Tina .net format");
  app.add_option("--romeo", romeo_file, "Export to Romeo XML format");
  app.set_version_flag("-v,--version", "1.0.0");

  CLI11_PARSE(app, argc, argv);

  spdlog::info("==========================================");
  spdlog::info("PTPN - Priority Timed Petri Net Analyzer");
  spdlog::info("==========================================");

  size_t initial_memory = get_memory_usage();
  auto start_time = chrono::high_resolution_clock::now();

  // Parse JSON
  auto tdg_start = chrono::high_resolution_clock::now();
  spdlog::info("[TDG] Starting JSON parsing: {}", input_file);

  json_tdg::JsonTDGParser parser;
  auto parse_result = parser.parse_file(input_file);

  if (!parse_result.success) {
    cerr << "ERROR: Failed to parse JSON file: " << parse_result.error_message << endl;
    return 1;
  }

  // Validate input
  auto validation = parser.validate();
  if (!validation.success) {
    cerr << "ERROR: Input validation failed:" << endl;
    for (const auto& err : validation.errors) {
      cerr << "  - " << err << endl;
    }
    return 1;
  }

  if (!validation.warnings.empty()) {
    cout << "Warnings:" << endl;
    for (const auto& warn : validation.warnings) {
      cout << "  - " << warn << endl;
    }
  }

  spdlog::info("[TDG] Configuration: {} CPUs, {} cores per CPU",
               parser.get_num_cpus(), parser.get_cores_per_cpu());

  // Create TDG
  TDG tdg(parser.get_num_cpus(), parser.get_cores_per_cpu());
  tdg.parse_json(input_file);
  tdg.classify_priority();

  // Optional: Export DOT for verification
  if (export_dot) {
    string dot_path = input_file;
    size_t dot_pos = dot_path.rfind('.');
    if (dot_pos != string::npos) {
      dot_path = dot_path.substr(0, dot_pos);
    }
    dot_path += ".dot";
    tdg.export_to_dot(dot_path);
    spdlog::info("[OUTPUT] TDG DOT exported to: {}", dot_path);
  }

  auto tdg_end = chrono::high_resolution_clock::now();
  auto tdg_duration = chrono::duration_cast<chrono::milliseconds>(tdg_end - tdg_start);
  size_t tdg_memory = get_memory_usage() - initial_memory;

  spdlog::info("\n[STATS] TDG Parsing: {} ms, {} KB", tdg_duration.count(), tdg_memory);

  // Transform to Matrix PTPN
  spdlog::info("\n[PTPN] Converting to Matrix PTPN...");
  matrix_ptpn::MatrixPTPN matrix_ptpn;
  matrix_ptpn.transform_tdg_to_matrix_ptpn(tdg);
  spdlog::info("[PTPN] Matrix PTPN conversion completed");
  spdlog::info("  Places: {}", matrix_ptpn.num_places());
  spdlog::info("  Transitions: {}", matrix_ptpn.num_transitions());

  cout << matrix_ptpn.to_string();

  string matrix_ptpn_dot_file = "matrix_ptpn.dot";
  graph_ptpn::GraphPTPN graph_ptpn(matrix_ptpn);
  if (graph_ptpn.save_to_dot(matrix_ptpn_dot_file)) {
    spdlog::info("[OUTPUT] Matrix PTPN saved to: {}", matrix_ptpn_dot_file);
  } else {
    spdlog::warn("[OUTPUT] Failed to save Matrix PTPN");
  }

  // Build State Class Reachability Graph
  spdlog::info("\n[SCG] Building State Class Reachability Graph...");
  size_t scg_start_memory = get_memory_usage();
  auto scg_start = chrono::high_resolution_clock::now();
  state_class::StateClassReachabilityGraph scg(matrix_ptpn);
  spdlog::info("[SCG] Starting build (max_states={})...", max_states);

  size_t num_states = scg.build(max_states);
  spdlog::info("[SCG] Build completed");

  const auto& stats = scg.get_statistics();
  spdlog::info("  Total states: {}", stats.total_states);
  spdlog::info("  Total transitions: {}", stats.total_transitions);
  spdlog::info("  Enabled transitions: {}", stats.enabled_transitions_count);
  spdlog::info("  Pruned states: {}", stats.pruned_states_count);

  string scg_dot_file = "state_class_graph.dot";
  if (scg.save_to_dot(scg_dot_file)) {
    spdlog::info("[OUTPUT] State class graph (DOT) saved to: {}", scg_dot_file);
  }

  string scg_json_file = "state_class_graph.json";
  if (scg.save_to_json(scg_json_file)) {
    spdlog::info("[OUTPUT] State class graph (JSON) saved to: {}", scg_json_file);
  }

  auto scg_end = chrono::high_resolution_clock::now();
  auto scg_duration = chrono::duration_cast<chrono::milliseconds>(scg_end - scg_start);
  size_t scg_memory = get_memory_usage() - scg_start_memory;

  spdlog::info("\n[STATS] State Class Generation: {} ms, {} KB", scg_duration.count(), scg_memory);

  // Final statistics
  auto end_time = chrono::high_resolution_clock::now();
  auto total_duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
  size_t total_memory = get_memory_usage() - initial_memory;

  cout << "\n========================================" << endl;
  cout << "Total Statistics:" << endl;
  cout << "  Total time: " << total_duration.count() << " ms" << endl;
  cout << "  Total memory: " << total_memory << " KB" << endl;
  cout << "========================================" << endl;

  return 0;
}