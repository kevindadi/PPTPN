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

#include <CLI/CLI.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <variant>

#include "analysis/metrics.h"
#include "analysis/ptpn_analysis.h"
#include "json/json.h"
#include "parser/ptpn_parser.h"
#include "petri/export_dot.h"
#include "petri/export_ptpn.h"
#include "petri/export_romeo.h"
#include "petri/petri.h"
#include "tdg/tdg.h"
#include "tdg2pn/tdg2pn.h"
#include "tdg2ptopner/tdg2ptopner.h"
#include "tdg2ptopner/validate.h"
#include "types/types.h"

using namespace std;
namespace fs = std::filesystem;

size_t get_memory_usage();

namespace {

enum class InputFormat { AUTO, TDG, PTPN };

struct ExportTargets {
  string tdg_dot;
  string wcet_json;
  string ptpn_dot;
  string scg_dot;
  string metrics_json;
  string romeo_file;
  string ppn_file;
  string tina_file;
};

struct PipelineOptions {
  size_t max_states = 10000;
  bool debug_mode = false;
  string canonicalization_mode = "equality";
  bool skip_analysis = false;
  optional<SchedulePolicy> policy_override;
  ExportTargets exports;
};

struct ExportCommandOptions {
  string input_file;
  string output_file;
  string input_format = "auto";
  optional<SchedulePolicy> policy_override;
  bool debug_mode = false;
};

bool has_any_export(const ExportTargets& exports) {
  return !exports.tdg_dot.empty() || !exports.wcet_json.empty() ||
         !exports.ptpn_dot.empty() || !exports.scg_dot.empty() ||
         !exports.metrics_json.empty() || !exports.romeo_file.empty() ||
         !exports.ppn_file.empty() || !exports.tina_file.empty();
}

bool should_run_analysis(const PipelineOptions& opts) {
  if (!opts.exports.scg_dot.empty() || !opts.exports.metrics_json.empty()) {
    return true;
  }
  return !opts.skip_analysis;
}

InputFormat resolve_input_format(const fs::path& input_path,
                                 const string& format_hint) {
  if (format_hint == "tdg") {
    return InputFormat::TDG;
  }
  if (format_hint == "ptpn") {
    return InputFormat::PTPN;
  }
  const string ext = input_path.extension().string();
  if (ext == ".ptpn") {
    return InputFormat::PTPN;
  }
  return InputFormat::TDG;
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

SchedulePolicy parse_policy_option(const string& policy) {
  const SchedulePolicy parsed = parse_schedule_policy(policy);
  if (parsed == SchedulePolicy::UNKNOWN) {
    throw CLI::ValidationError(
        policy,
        "Expected fixed, fixed_prior_with_restart, fixed_prior_with_resume, "
        "fifo, rm, dm, edf, llf, pip, pcp, or srp");
  }
  return parsed;
}

void apply_policy_override(tdg::TDG& tdg,
                           const optional<SchedulePolicy>& policy_override) {
  if (!policy_override.has_value()) {
    return;
  }
  tdg.policy = policy_override.value();
  spdlog::info("[TDG] Scheduling policy override: {}",
               schedule_policy_to_string(tdg.policy));
}

void add_export_target_options(CLI::App* cmd, ExportTargets& exports) {
  cmd->add_option("--export-tdg", exports.tdg_dot,
                  "Write TDG Graphviz DOT to PATH");
  cmd->add_option("--export-wcet", exports.wcet_json,
                  "Write per-task WCET summary JSON to PATH");
  cmd->add_option("--export-ptpn-dot", exports.ptpn_dot,
                  "Write PTPN structure Graphviz DOT to PATH");
  cmd->add_option("--export-scg", exports.scg_dot,
                  "Write state-class reachability graph DOT to PATH");
  cmd->add_option("--export-metrics", exports.metrics_json,
                  "Write performance metrics JSON to PATH");
  cmd->add_option("--romeo", exports.romeo_file, "Export Romeo CTS to PATH");
  cmd->add_option("--ppn", exports.ppn_file, "Export PToPNer .ppn to PATH");
  cmd->add_option("--tina", exports.tina_file,
                  "Export Tina .net to PATH (not implemented)");
}

void add_common_pipeline_options(CLI::App* cmd, PipelineOptions& opts) {
  cmd->add_option(
      "-m,--max-states", opts.max_states,
      "Maximum number of states in reachability graph (default: 10000)");
  cmd->add_flag("--no-analysis", opts.skip_analysis,
                "Skip state-class reachability analysis");
  cmd->add_flag("--debug", opts.debug_mode, "Enable debug logging");
  cmd->add_option("--canonicalization", opts.canonicalization_mode,
                  "Canonicalization mode: equality, max-lower, or intersection "
                  "(default: equality)")
      ->check(CLI::IsMember({"equality", "max-lower", "intersection"}));
  add_export_target_options(cmd, opts.exports);
}

int validate_ptopner_tdg(const tdg::TDG& tdg) {
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
  return 0;
}

int export_romeo_from_ptpn(const petri::PTPN& ptpn, const string& output_path) {
  const auto export_model = petri::exporting::build_export_model(ptpn);
  if (petri::exporting::save_to_romeo_cts(export_model, output_path)) {
    spdlog::info("[OUTPUT] Romeo CTS exported to: {}", output_path);
    return 0;
  }
  cerr << "ERROR: Failed to export Romeo CTS" << endl;
  return 1;
}

int export_ppn_from_ptpn(const petri::PTPN& ptpn, const string& output_path) {
  const auto ppn_export =
      ptopner_export::export_ptpn_to_ppn_file(ptpn, output_path);
  if (!ppn_export.success) {
    cerr << "ERROR: PToPNer export failed: " << ppn_export.error_message
         << endl;
    return 1;
  }
  spdlog::info("[OUTPUT] PToPNer .ppn exported to: {}", output_path);
  return 0;
}

int load_tdg_from_json(const string& input_file, tdg::TDG& tdg,
                       parse::Parser& parser) {
  const auto parse_result = parser.parse_file(input_file);
  if (!parse_result.success) {
    cerr << "ERROR: Failed to parse JSON file: " << parse_result.error_message
         << endl;
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

  tdg = tdg::TDG(parser.get_num_cpus(), parser.get_cores_per_cpu());
  tdg.parse_json(input_file);
  return 0;
}

int build_ptpn_from_tdg(tdg::TDG& tdg, petri::PTPN& ptpn) {
  spdlog::info("\n[PTPN] Converting TDG to PTPN...");
  converter::TDG2PN::transform(tdg, ptpn);
  spdlog::info("[PTPN] PTPN conversion completed");
  spdlog::info("  Places: {}", ptpn.num_places());
  spdlog::info("  Transitions: {}", ptpn.num_transitions());
  spdlog::info("  Policy: {}", schedule_policy_to_string(tdg.policy));
  return 0;
}

int run_ptpn_postprocess(
    const petri::PTPN& ptpn, const string& input_label,
    const PipelineOptions& opts,
    const chrono::time_point<chrono::high_resolution_clock>& pipeline_start,
    size_t initial_memory) {
  if (opts.debug_mode) {
    cout << ptpn.to_string();
  }

  const auto export_model = petri::exporting::build_export_model(ptpn);

  if (!opts.exports.ptpn_dot.empty()) {
    if (petri::exporting::save_to_dot(export_model, opts.exports.ptpn_dot)) {
      spdlog::info("[OUTPUT] PTPN DOT exported to: {}", opts.exports.ptpn_dot);
    } else {
      spdlog::warn("[OUTPUT] Failed to export PTPN DOT");
      return 1;
    }
  }

  if (!opts.exports.ppn_file.empty()) {
    if (export_ppn_from_ptpn(ptpn, opts.exports.ppn_file) != 0) {
      return 1;
    }
  }

  if (!opts.exports.tina_file.empty()) {
    spdlog::info("[OUTPUT] Tina export not implemented");
  }

  if (!opts.exports.romeo_file.empty()) {
    if (export_romeo_from_ptpn(ptpn, opts.exports.romeo_file) != 0) {
      return 1;
    }
  }

  if (should_run_analysis(opts)) {
    const auto canonicalization =
        parse_canonicalization(opts.canonicalization_mode);
    state_class::StateClassReachabilityGraph reachability_graph(ptpn);
    reachability_graph.set_canonicalization_mode(canonicalization);
    spdlog::info("[SCG] Canonicalization mode: {}", opts.canonicalization_mode);
    const size_t state_count = reachability_graph.build(opts.max_states);
    const auto& reachability_stats = reachability_graph.get_statistics();
    if (reachability_stats.truncated) {
      spdlog::warn(
          "[SCG] Reachability graph truncated at {} states; use --max-states "
          "to raise the bound",
          opts.max_states);
    } else {
      spdlog::info("[SCG] Reachability graph built with {} states",
                   state_count);
    }

    if (!opts.exports.scg_dot.empty()) {
      if (reachability_graph.save_to_dot(opts.exports.scg_dot)) {
        spdlog::info("[OUTPUT] State class graph exported to: {}",
                     opts.exports.scg_dot);
      } else {
        spdlog::warn("[OUTPUT] Failed to export state class graph");
        return 1;
      }
    }

    if (!opts.exports.metrics_json.empty()) {
      const bool exact =
          canonicalization == state_class::CanonicalizationMode::EQUALITY;
      if (!exact) {
        spdlog::warn(
            "[METRICS] Canonicalization '{}' merges state classes; metrics are "
            "approximate. Use --canonicalization equality for sound bounds",
            opts.canonicalization_mode);
      }
      state_class::MetricsAnalyzer analyzer(reachability_graph.get_graph(), ptpn,
                                            reachability_graph.get_initial_vertex(),
                                            exact);
      const state_class::MetricsReport report = analyzer.analyze();
      if (state_class::MetricsAnalyzer::save_to_json(report,
                                                     opts.exports.metrics_json)) {
        spdlog::info("[OUTPUT] Metrics exported to: {}",
                     opts.exports.metrics_json);
        spdlog::info("[METRICS] schedulable={}, bounded={}, deadlocks={}",
                     report.schedulable ? "true" : "false",
                     report.bounded ? "true" : "false",
                     report.deadlock_states.size());
      } else {
        spdlog::warn("[OUTPUT] Failed to export metrics");
        return 1;
      }
    }
  } else {
    spdlog::info("[SCG] Reachability analysis skipped");
  }

  const auto end_time = chrono::high_resolution_clock::now();
  const auto total_duration =
      chrono::duration_cast<chrono::milliseconds>(end_time - pipeline_start);
  const size_t total_memory = get_memory_usage() - initial_memory;
  spdlog::info("\n[STATS] {} pipeline: {} ms, {} KB", input_label,
               total_duration.count(), total_memory);
  return 0;
}

int run_tdg_pipeline(const string& input_file, PipelineOptions opts,
                     size_t initial_memory) {
  const auto pipeline_start = chrono::high_resolution_clock::now();
  const auto tdg_start = chrono::high_resolution_clock::now();
  spdlog::info("[TDG] Loading JSON: {}", input_file);

  parse::Parser parser;
  tdg::TDG tdg(1, 1);
  if (load_tdg_from_json(input_file, tdg, parser) != 0) {
    return 1;
  }

  apply_policy_override(tdg, opts.policy_override);

  spdlog::info("[TDG] Configuration: {} CPUs, {} cores per CPU, policy={}",
               parser.get_num_cpus(), parser.get_cores_per_cpu(),
               schedule_policy_to_string(tdg.policy));

  if (!opts.exports.tdg_dot.empty()) {
    tdg.export_to_dot(opts.exports.tdg_dot);
    spdlog::info("[OUTPUT] TDG DOT exported to: {}", opts.exports.tdg_dot);
  }

  if (!opts.exports.wcet_json.empty()) {
    if (!write_wcet_json(tdg, opts.exports.wcet_json)) {
      spdlog::warn("[OUTPUT] Failed to save WCET JSON to: {}",
                   opts.exports.wcet_json);
      return 1;
    }
    spdlog::info("[OUTPUT] WCET JSON exported to: {}", opts.exports.wcet_json);
  }

  const auto tdg_end = chrono::high_resolution_clock::now();
  const auto tdg_duration =
      chrono::duration_cast<chrono::milliseconds>(tdg_end - tdg_start);
  const size_t tdg_memory = get_memory_usage() - initial_memory;
  spdlog::info("\n[STATS] TDG parsing: {} ms, {} KB", tdg_duration.count(),
               tdg_memory);

  if (!opts.exports.ppn_file.empty()) {
    if (validate_ptopner_tdg(tdg) != 0) {
      return 1;
    }
  }

  petri::PTPN ptpn;
  build_ptpn_from_tdg(tdg, ptpn);

  if (!should_run_analysis(opts) && !has_any_export(opts.exports)) {
    spdlog::warn(
        "[MAIN] No exports requested and analysis disabled; nothing to do");
    return 0;
  }

  return run_ptpn_postprocess(ptpn, "TDG", opts, pipeline_start,
                              initial_memory);
}

int run_ptpn_pipeline(const string& input_file, const PipelineOptions& opts,
                      size_t initial_memory) {
  const auto pipeline_start = chrono::high_resolution_clock::now();
  spdlog::info("[PTPN] Loading source: {}", input_file);

  const petri::PTPN ptpn = parser::PTPNBuilder::parse_file(input_file);
  if (parser::PTPNBuilder::has_error()) {
    cerr << "ERROR: Failed to parse PTPN file: "
         << parser::PTPNBuilder::error_message() << endl;
    return 1;
  }

  spdlog::info("[PTPN] Parse completed");
  spdlog::info("  Places: {}", ptpn.num_places());
  spdlog::info("  Transitions: {}", ptpn.num_transitions());

  if (!should_run_analysis(opts) && !has_any_export(opts.exports)) {
    spdlog::warn(
        "[MAIN] No exports requested and analysis disabled; nothing to do");
    return 0;
  }

  return run_ptpn_postprocess(ptpn, "PTPN", opts, pipeline_start,
                              initial_memory);
}

int run_export_romeo(const ExportCommandOptions& opts) {
  const fs::path input_path(opts.input_file);
  const InputFormat format = resolve_input_format(input_path, opts.input_format);

  if (format == InputFormat::PTPN) {
    spdlog::info("[EXPORT] Romeo from PTPN source: {}", opts.input_file);
    const petri::PTPN ptpn = parser::PTPNBuilder::parse_file(opts.input_file);
    if (parser::PTPNBuilder::has_error()) {
      cerr << "ERROR: Failed to parse PTPN file: "
           << parser::PTPNBuilder::error_message() << endl;
      return 1;
    }
    return export_romeo_from_ptpn(ptpn, opts.output_file);
  }

  spdlog::info("[EXPORT] Romeo from TDG JSON: {}", opts.input_file);
  parse::Parser parser;
  tdg::TDG tdg(1, 1);
  if (load_tdg_from_json(opts.input_file, tdg, parser) != 0) {
    return 1;
  }

  apply_policy_override(tdg, opts.policy_override);
  if (!opts.policy_override.has_value()) {
    spdlog::info(
        "[EXPORT] Using TDG policy {} (Romeo path typically uses "
        "fixed_prior_with_resume or fixed)",
        schedule_policy_to_string(tdg.policy));
  }

  petri::PTPN ptpn;
  build_ptpn_from_tdg(tdg, ptpn);
  return export_romeo_from_ptpn(ptpn, opts.output_file);
}

int run_export_ptopner(const ExportCommandOptions& opts) {
  const fs::path input_path(opts.input_file);
  const InputFormat format = resolve_input_format(input_path, opts.input_format);

  if (format == InputFormat::PTPN) {
    spdlog::info("[EXPORT] PToPNer from PTPN source: {}", opts.input_file);
    const petri::PTPN ptpn = parser::PTPNBuilder::parse_file(opts.input_file);
    if (parser::PTPNBuilder::has_error()) {
      cerr << "ERROR: Failed to parse PTPN file: "
           << parser::PTPNBuilder::error_message() << endl;
      return 1;
    }
    return export_ppn_from_ptpn(ptpn, opts.output_file);
  }

  spdlog::info("[EXPORT] PToPNer from TDG JSON: {}", opts.input_file);
  parse::Parser parser;
  tdg::TDG tdg(1, 1);
  if (load_tdg_from_json(opts.input_file, tdg, parser) != 0) {
    return 1;
  }

  if (opts.policy_override.has_value()) {
    if (opts.policy_override.value() != SchedulePolicy::FIXED_PRIOR_WITH_RESTART) {
      cerr << "ERROR: PToPNer export requires fixed_prior_with_restart policy"
           << endl;
      return 1;
    }
    tdg.policy = SchedulePolicy::FIXED_PRIOR_WITH_RESTART;
  } else if (tdg.policy != SchedulePolicy::FIXED_PRIOR_WITH_RESTART) {
    spdlog::info(
        "[EXPORT] Overriding TDG policy {} -> fixed_prior_with_restart for "
        "PToPNer",
        schedule_policy_to_string(tdg.policy));
    tdg.policy = SchedulePolicy::FIXED_PRIOR_WITH_RESTART;
  }

  if (validate_ptopner_tdg(tdg) != 0) {
    return 1;
  }

  petri::PTPN ptpn;
  build_ptpn_from_tdg(tdg, ptpn);
  return export_ppn_from_ptpn(ptpn, opts.output_file);
}

void configure_export_subcommand(CLI::App* cmd, ExportCommandOptions& opts,
                                 const string& footer) {
  cmd->add_option("-f,--file", opts.input_file, "Input TDG JSON or .ptpn file")
      ->required(true);
  cmd->add_option("-o,--output", opts.output_file, "Output file path")
      ->required(true);
  cmd->add_option(
         "--from", opts.input_format,
         "Input format: auto, tdg, or ptpn (default: auto)")
      ->check(CLI::IsMember({"auto", "tdg", "ptpn"}));
  cmd->add_flag("--debug", opts.debug_mode, "Enable debug logging");
  cmd->footer(footer);
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
  app.footer(
      "Usage:\n"
      "  ptpn tdg -f <input.json> [--export-scg graph.dot] ...\n"
      "  ptpn ptpn -f <model.ptpn> [--romeo out.cts] ...\n"
      "  ptpn export romeo -f <input> -o <out.cts> [--policy ...]\n"
      "  ptpn export ptopner -f <input> -o <out.ppn>\n"
      "\n"
      "File exports are opt-in. Run 'ptpn <subcommand> -h' for details.");

  PipelineOptions tdg_opts;
  PipelineOptions ptpn_opts;
  ExportCommandOptions romeo_export_opts;
  ExportCommandOptions ptopner_export_opts;
  string tdg_file;
  string ptpn_file;
  string tdg_policy_string;
  string romeo_policy_string;
  string ptopner_policy_string;

  auto* tdg_cmd = app.add_subcommand("tdg", "Analyze from TDG JSON input");
  tdg_cmd->add_option("-f,--file", tdg_file, "Input TDG JSON file")
      ->required(true);
  add_common_pipeline_options(tdg_cmd, tdg_opts);
  tdg_cmd
      ->add_option(
          "--policy", tdg_policy_string,
          "Override TDG scheduling policy for lowering "
          "(fixed_prior_with_resume for native analysis, "
          "fixed_prior_with_restart for PToPNer)")
      ->transform([&tdg_opts](const string& value) {
        tdg_opts.policy_override = parse_policy_option(value);
        return value;
      });
  tdg_cmd->footer(
      "Examples:\n"
      "  ptpn tdg -f example/common/input.json\n"
      "  ptpn tdg -f input.json --export-scg scg.dot --export-ptpn-dot net.dot\n"
      "  ptpn tdg -f input.json --policy fixed_prior_with_resume --romeo "
      "out.cts\n"
      "  ptpn tdg -f input.json --policy fixed_prior_with_restart --ppn "
      "out.ppn\n"
      "  ptpn tdg -f input.json --no-analysis --export-wcet wcet.json");

  auto* ptpn_cmd = app.add_subcommand("ptpn", "Analyze from PTPN source file");
  ptpn_cmd->add_option("-f,--file", ptpn_file, "Input .ptpn source file")
      ->required(true);
  add_common_pipeline_options(ptpn_cmd, ptpn_opts);
  ptpn_cmd->footer(
      "Examples:\n"
      "  ptpn ptpn -f example/common/simple.ptpn\n"
      "  ptpn ptpn -f model.ptpn --export-scg scg.dot --canonicalization "
      "max-lower\n"
      "  ptpn ptpn -f model.ptpn --no-analysis --romeo out.cts --debug");

  auto* export_cmd =
      app.add_subcommand("export", "Export to Romeo or PToPNer formats");
  export_cmd->require_subcommand(1);

  auto* export_romeo_cmd = export_cmd->add_subcommand(
      "romeo",
      "Export Romeo CTS (resume-style TDG lowering recommended)");
  configure_export_subcommand(
      export_romeo_cmd, romeo_export_opts,
      "Examples:\n"
      "  ptpn export romeo -f example/common/input.json -o out.cts\n"
      "  ptpn export romeo -f input.json -o out.cts "
      "--policy fixed_prior_with_resume\n"
      "  ptpn export romeo -f model.ptpn -o out.cts --from ptpn");
  export_romeo_cmd
      ->add_option(
          "--policy", romeo_policy_string,
          "Override TDG scheduling policy (default: use JSON policy)")
      ->transform([&romeo_export_opts](const string& value) {
        romeo_export_opts.policy_override = parse_policy_option(value);
        return value;
      });

  auto* export_ptopner_cmd = export_cmd->add_subcommand(
      "ptopner",
      "Export PToPNer .ppn (restart-style TDG lowering required)");
  configure_export_subcommand(
      export_ptopner_cmd, ptopner_export_opts,
      "Examples:\n"
      "  ptpn export ptopner -f example/common/input.json -o out.ppn\n"
      "  ptpn export ptopner -f input.json -o out.ppn "
      "--policy fixed_prior_with_restart\n"
      "  ptpn export ptopner -f model.ptpn -o out.ppn --from ptpn");
  export_ptopner_cmd
      ->add_option(
          "--policy", ptopner_policy_string,
          "Must be fixed_prior_with_restart when exporting from TDG JSON")
      ->transform([&ptopner_export_opts](const string& value) {
        ptopner_export_opts.policy_override = parse_policy_option(value);
        return value;
      });

  CLI11_PARSE(app, argc, argv);

  bool debug_mode = false;
  if (tdg_cmd->parsed()) {
    debug_mode = tdg_opts.debug_mode;
  } else if (ptpn_cmd->parsed()) {
    debug_mode = ptpn_opts.debug_mode;
  } else if (export_romeo_cmd->parsed()) {
    debug_mode = romeo_export_opts.debug_mode;
  } else if (export_ptopner_cmd->parsed()) {
    debug_mode = ptopner_export_opts.debug_mode;
  }

  if (debug_mode) {
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
  if (ptpn_cmd->parsed()) {
    return run_ptpn_pipeline(ptpn_file, ptpn_opts, initial_memory);
  }
  if (export_romeo_cmd->parsed()) {
    return run_export_romeo(romeo_export_opts);
  }
  if (export_ptopner_cmd->parsed()) {
    return run_export_ptopner(ptopner_export_opts);
  }

  return 1;
}
