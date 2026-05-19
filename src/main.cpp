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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <variant>

#include "json/json.h"
#include "tdg/tdg.h"
#include "petri/petri.h"
#include "petri/graph.h"
#include "tdg2pn/tdg2pn.h"
#include "analysis/state.h"

using namespace std;
namespace fs = std::filesystem;

namespace {

fs::path artifact_path_for(const fs::path& input_path, const string& filename) {
  return input_path.parent_path() / filename;
}

bool write_wcet_json(const tdg::TDG& tdg, const fs::path& output_path) {
  nlohmann::json wcet = nlohmann::json::object();
  wcet["tasks"] = nlohmann::json::array();

  size_t task_id = 0;
  for (const auto& node : tdg.all_task) {
    if (!holds_alternative<TaskNode>(node)) {
      continue;
    }

    const auto& task = get<TaskNode>(node);
    int task_wcet = 0;
    for (const auto& interval : task.time) {
      task_wcet += interval.second;
    }

    wcet["tasks"].push_back({
        {"id", task_id++},
        {"name", task.name},
        {"wcet", task_wcet},
        {"segments", task.time.size()},
    });
  }

  ofstream out(output_path);
  if (!out.is_open()) {
    return false;
  }

  out << wcet.dump(2) << '\n';
  return true;
}

}  // namespace

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
  size_t max_states = 10000;
  string tina_file, romeo_file;

  app.add_option("-f,--file", input_file, "Input JSON file")
      ->required(true);
  app.add_option("-m,--max-states", max_states,
                 "Maximum number of states in reachability graph (default: 10000)");
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
  spdlog::info("[JSON] Starting JSON parsing: {}", input_file);

  parse::Parser parser;
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
  tdg::TDG tdg(parser.get_num_cpus(), parser.get_cores_per_cpu());
  tdg.parse_json(input_file);

  fs::path input_path(input_file);
  fs::path output_dir = input_path.parent_path();
  fs::path tdg_dot_path = artifact_path_for(input_path, "tdg.dot");
  fs::path ptpn_dot_path = artifact_path_for(input_path, "ptpn.dot");
  fs::path state_class_dot_path = artifact_path_for(input_path, "state-class-graph.dot");
  fs::path wcet_json_path = artifact_path_for(input_path, "wcet.json");

  tdg.export_to_dot(tdg_dot_path.string());
  spdlog::info("[OUTPUT] TDG DOT exported to: {}", tdg_dot_path.string());

  if (!write_wcet_json(tdg, wcet_json_path)) {
    spdlog::warn("[OUTPUT] Failed to save WCET JSON to: {}", wcet_json_path.string());
  } else {
    spdlog::info("[OUTPUT] WCET JSON exported to: {}", wcet_json_path.string());
  }

  auto tdg_end = chrono::high_resolution_clock::now();
  auto tdg_duration = chrono::duration_cast<chrono::milliseconds>(tdg_end - tdg_start);
  size_t tdg_memory = get_memory_usage() - initial_memory;

  spdlog::info("\n[STATS] TDG Parsing: {} ms, {} KB", tdg_duration.count(), tdg_memory);

  // Transform to PTPN
  spdlog::info("\n[PTPN] Converting to PTPN...");
  petri::PTPN ptpn;
  converter::TDG2PN::transform(tdg, ptpn);
  spdlog::info("[PTPN] PTPN conversion completed");
  spdlog::info("  Places: {}", ptpn.num_places());
  spdlog::info("  Transitions: {}", ptpn.num_transitions());

  cout << ptpn.to_string();

  graph::GraphPTPN graph_ptpn(ptpn);
  if (graph_ptpn.save_to_dot(ptpn_dot_path.string())) {
    spdlog::info("[OUTPUT] PTPN saved to: {}", ptpn_dot_path.string());
  } else {
    spdlog::warn("[OUTPUT] Failed to save PTPN");
  }

  state_class::StateClassReachabilityGraph reachability_graph(ptpn);
  size_t state_count = reachability_graph.build(max_states);
  const auto& reachability_stats = reachability_graph.get_statistics();
  if (reachability_stats.truncated) {
    spdlog::warn("[SCG] Reachability graph truncated at {} states; use --max-states to raise the bound", max_states);
  } else {
    spdlog::info("[SCG] Reachability graph built with {} states", state_count);
  }
  if (reachability_graph.save_to_dot(state_class_dot_path.string())) {
    spdlog::info("[OUTPUT] State class graph saved to: {}",
                 state_class_dot_path.string());
  } else {
    spdlog::warn("[OUTPUT] Failed to save state class graph");
  }

  auto ptpn_end = chrono::high_resolution_clock::now();
  auto ptpn_duration = chrono::duration_cast<chrono::milliseconds>(ptpn_end - tdg_end);
  size_t ptpn_memory = get_memory_usage() - tdg_memory;

  spdlog::info("\n[STATS] PTPN Conversion: {} ms, {} KB", ptpn_duration.count(), ptpn_memory);

  // Optional: Export to other formats
  if (!tina_file.empty()) {
    spdlog::info("[OUTPUT] Tina export not implemented");
  }

  if (!romeo_file.empty()) {
    spdlog::info("[OUTPUT] Romeo export not implemented");
  }

  auto end_time = chrono::high_resolution_clock::now();
  auto total_duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
  size_t total_memory = get_memory_usage() - initial_memory;

  spdlog::info("\n[STATS] Total: {} ms, {} KB", total_duration.count(), total_memory);

  return 0;
}