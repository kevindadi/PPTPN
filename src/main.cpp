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
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <variant>

#include "json/json.h"
#include "tdg/tdg.h"
#include "petri/petri.h"
#include "petri/export_dot.h"
#include "petri/export_ptpn.h"
#include "petri/export_romeo.h"
#include "parser/ptpn_parser.h"
#include "tdg2pn/tdg2pn.h"
#include "tdg2ptopner/validate.h"
#include "tdg2ptopner/tdg2ptopner.h"
#include "analysis/graph.h"

using namespace std;
namespace fs = std::filesystem;

size_t get_memory_usage();

namespace {

struct AnalyzeOptions {
  size_t max_states = 10000;
  string tina_file;
  string romeo_file;
  string ppn_file;
  bool debug_mode = false;
  string canonicalization_mode = "equality";
};

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

state_class::CanonicalizationMode parse_canonicalization(const string& mode) {
  if (mode == "max-lower") {
    return state_class::CanonicalizationMode::MAX_LOWER_BOUND;
  }
  if (mode == "intersection") {
    return state_class::CanonicalizationMode::INTERSECTION;
  }
  return state_class::CanonicalizationMode::EQUALITY;
}

void add_common_analyze_options(CLI::App* cmd, AnalyzeOptions& opts) {
  cmd->add_option("-m,--max-states", opts.max_states,
                  "Maximum number of states in reachability graph (default: 10000)");
  cmd->add_option("--tina", opts.tina_file, "Export to Tina .net format");
  cmd->add_option("--romeo", opts.romeo_file, "Export to Romeo CTS format");
  cmd->add_option("--ppn", opts.ppn_file, "Export to PToPNer .ppn format");
  cmd->add_flag("--debug", opts.debug_mode, "Enable debug logging");
  cmd->add_option("--canonicalization", opts.canonicalization_mode,
                  "Canonicalization mode: equality, max-lower, or intersection "
                  "(default: equality)")
      ->check(CLI::IsMember({"equality", "max-lower", "intersection"}));
}

int run_ptpn_analysis(const petri::PTPN& ptpn, const fs::path& output_dir,
                      const string& input_label, const AnalyzeOptions& opts,
                      size_t initial_memory, const chrono::time_point<chrono::high_resolution_clock>& pipeline_start) {
  const fs::path ptpn_dot_path = output_dir / "ptpn.dot";
  const fs::path state_class_dot_path = output_dir / "state-class-graph.dot";

  if (!opts.ppn_file.empty()) {
    const auto ppn_export = ptopner_export::export_ptpn_to_ppn_file(ptpn, opts.ppn_file);
    if (!ppn_export.success) {
      cerr << "ERROR: PToPNer export failed: " << ppn_export.error_message << endl;
      return 1;
    }
    spdlog::info("[OUTPUT] PToPNer .ppn exported to: {}", opts.ppn_file);
  }

  if (opts.debug_mode) {
    cout << ptpn.to_string();
  }

  const auto export_model = petri::exporting::build_export_model(ptpn);
  if (petri::exporting::save_to_dot(export_model, ptpn_dot_path.string())) {
    spdlog::info("[OUTPUT] PTPN saved to: {}", ptpn_dot_path.string());
  } else {
    spdlog::warn("[OUTPUT] Failed to save PTPN");
  }

  const auto canonicalization = parse_canonicalization(opts.canonicalization_mode);
  state_class::StateClassReachabilityGraph reachability_graph(ptpn);
  reachability_graph.set_canonicalization_mode(canonicalization);
  spdlog::info("[SCG] Canonicalization mode: {}", opts.canonicalization_mode);
  const size_t state_count = reachability_graph.build(opts.max_states);
  const auto& reachability_stats = reachability_graph.get_statistics();
  if (reachability_stats.truncated) {
    spdlog::warn("[SCG] Reachability graph truncated at {} states; use --max-states to raise the bound",
                 opts.max_states);
  } else {
    spdlog::info("[SCG] Reachability graph built with {} states", state_count);
  }
  if (reachability_graph.save_to_dot(state_class_dot_path.string())) {
    spdlog::info("[OUTPUT] State class graph saved to: {}", state_class_dot_path.string());
  } else {
    spdlog::warn("[OUTPUT] Failed to save state class graph");
  }

  if (!opts.tina_file.empty()) {
    spdlog::info("[OUTPUT] Tina export not implemented");
  }

  if (!opts.romeo_file.empty()) {
    if (petri::exporting::save_to_romeo_cts(export_model, opts.romeo_file)) {
      spdlog::info("[OUTPUT] Romeo CTS exported to: {}", opts.romeo_file);
    } else {
      spdlog::warn("[OUTPUT] Failed to export Romeo CTS");
    }
  }

  const auto end_time = chrono::high_resolution_clock::now();
  const auto total_duration =
      chrono::duration_cast<chrono::milliseconds>(end_time - pipeline_start);
  const size_t total_memory = get_memory_usage() - initial_memory;
  spdlog::info("\n[STATS] {} analysis: {} ms, {} KB", input_label, total_duration.count(),
               total_memory);

  return 0;
}

int run_tdg_pipeline(const string& input_file, const AnalyzeOptions& opts,
                     size_t initial_memory) {
  const auto pipeline_start = chrono::high_resolution_clock::now();
  const auto tdg_start = chrono::high_resolution_clock::now();
  spdlog::info("[TDG] Loading JSON: {}", input_file);

  parse::Parser parser;
  const auto parse_result = parser.parse_file(input_file);
  if (!parse_result.success) {
    cerr << "ERROR: Failed to parse JSON file: " << parse_result.error_message << endl;
    return 1;
  }

  const auto validation = parser.validate();
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

  spdlog::info("[TDG] Configuration: {} CPUs, {} cores per CPU", parser.get_num_cpus(),
               parser.get_cores_per_cpu());

  tdg::TDG tdg(parser.get_num_cpus(), parser.get_cores_per_cpu());
  tdg.parse_json(input_file);

  const fs::path input_path(input_file);
  const fs::path output_dir = input_path.parent_path();
  const fs::path tdg_dot_path = artifact_path_for(input_path, "tdg.dot");
  const fs::path wcet_json_path = artifact_path_for(input_path, "wcet.json");

  tdg.export_to_dot(tdg_dot_path.string());
  spdlog::info("[OUTPUT] TDG DOT exported to: {}", tdg_dot_path.string());

  if (!write_wcet_json(tdg, wcet_json_path)) {
    spdlog::warn("[OUTPUT] Failed to save WCET JSON to: {}", wcet_json_path.string());
  } else {
    spdlog::info("[OUTPUT] WCET JSON exported to: {}", wcet_json_path.string());
  }

  const auto tdg_end = chrono::high_resolution_clock::now();
  const auto tdg_duration = chrono::duration_cast<chrono::milliseconds>(tdg_end - tdg_start);
  const size_t tdg_memory = get_memory_usage() - initial_memory;
  spdlog::info("\n[STATS] TDG parsing: {} ms, {} KB", tdg_duration.count(), tdg_memory);

  if (!opts.ppn_file.empty()) {
    const auto ppn_validation = ptopner_export::validate_for_ptopner(tdg);
    if (!ppn_validation.ok) {
      cerr << "ERROR: PToPNer export validation failed:" << endl;
      for (const auto& err : ppn_validation.errors) {
        cerr << "  - " << err << endl;
      }
      return 1;
    }
    if (!ppn_validation.warnings.empty()) {
      cout << "PToPNer export warnings:" << endl;
      for (const auto& warn : ppn_validation.warnings) {
        cout << "  - " << warn << endl;
      }
    }
  }

  spdlog::info("\n[PTPN] Converting TDG to PTPN...");
  petri::PTPN ptpn;
  converter::TDG2PN::transform(tdg, ptpn);
  spdlog::info("[PTPN] PTPN conversion completed");
  spdlog::info("  Places: {}", ptpn.num_places());
  spdlog::info("  Transitions: {}", ptpn.num_transitions());

  return run_ptpn_analysis(ptpn, output_dir, "TDG", opts, initial_memory, pipeline_start);
}

int run_ptpn_pipeline(const string& input_file, const AnalyzeOptions& opts,
                      size_t initial_memory) {
  const auto pipeline_start = chrono::high_resolution_clock::now();
  spdlog::info("[PTPN] Loading source: {}", input_file);

  const petri::PTPN ptpn = parser::PTPNBuilder::parse_file(input_file);
  if (parser::PTPNBuilder::has_error()) {
    cerr << "ERROR: Failed to parse PTPN file: " << parser::PTPNBuilder::error_message() << endl;
    return 1;
  }

  spdlog::info("[PTPN] Parse completed");
  spdlog::info("  Places: {}", ptpn.num_places());
  spdlog::info("  Transitions: {}", ptpn.num_transitions());

  const fs::path input_path(input_file);
  const fs::path output_dir = input_path.parent_path();

  return run_ptpn_analysis(ptpn, output_dir, "PTPN", opts, initial_memory, pipeline_start);
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
  app.set_version_flag("-v,--version", "1.0.0");
  app.require_subcommand(1);

  AnalyzeOptions tdg_opts;
  AnalyzeOptions ptpn_opts;
  string tdg_file;
  string ptpn_file;

  auto* tdg_cmd = app.add_subcommand("tdg", "Analyze from TDG JSON input");
  tdg_cmd->add_option("-f,--file", tdg_file, "Input TDG JSON file")->required(true);
  add_common_analyze_options(tdg_cmd, tdg_opts);

  auto* ptpn_cmd = app.add_subcommand("ptpn", "Analyze from PTPN source file");
  ptpn_cmd->add_option("-f,--file", ptpn_file, "Input .ptpn source file")->required(true);
  add_common_analyze_options(ptpn_cmd, ptpn_opts);

  CLI11_PARSE(app, argc, argv);

  const AnalyzeOptions& active_opts = tdg_cmd->parsed() ? tdg_opts : ptpn_opts;
  if (active_opts.debug_mode) {
    spdlog::set_level(spdlog::level::debug);
    spdlog::debug("[MAIN] Debug logging enabled");
  }

  spdlog::info("==========================================");
  spdlog::info("PTPN - Priority Timed Petri Net Analyzer");
  spdlog::info("==========================================");

  const size_t initial_memory = get_memory_usage();

  if (tdg_cmd->parsed()) {
    return run_tdg_pipeline(tdg_file, tdg_opts, initial_memory);
  }

  return run_ptpn_pipeline(ptpn_file, ptpn_opts, initial_memory);
}
