#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "analysis/state.h"
#include "json/json.h"
#include "petri/petri.h"
#include "tdg/tdg.h"
#include "tdg2pn/tdg2pn.h"

namespace {

struct BenchmarkRow {
  size_t threads = 0;
  size_t runs = 0;
  double best_ms = 0.0;
  double median_ms = 0.0;
  double speedup_vs_1x = 0.0;
  size_t states = 0;
  size_t transitions = 0;
  size_t dbm_minimize_calls = 0;
  bool truncated = false;
  bool matches_baseline = true;
};

std::vector<size_t> parse_thread_list(const std::string& value) {
  std::vector<size_t> threads;
  std::stringstream stream(value);
  std::string token;
  while (std::getline(stream, token, ',')) {
    if (token.empty()) {
      continue;
    }
    threads.push_back(static_cast<size_t>(std::stoul(token)));
  }
  if (threads.empty()) {
    throw CLI::ValidationError("--threads", "expected a comma-separated list like 1,2,4,8");
  }
  return threads;
}

BenchmarkRow run_benchmark(const std::string& input_file, size_t max_states,
                           size_t thread_count, size_t repeat_count) {
  BenchmarkRow row;
  row.threads = thread_count;
  row.runs = repeat_count;

  std::vector<double> durations_ms;
  durations_ms.reserve(repeat_count);

  for (size_t run = 0; run < repeat_count; ++run) {
    parse::Parser parser;
    auto parse_result = parser.parse_file(input_file);
    if (!parse_result.success) {
      throw std::runtime_error("Failed to parse input file: " + parse_result.error_message);
    }

    auto validation = parser.validate();
    if (!validation.success) {
      throw std::runtime_error("Input validation failed");
    }

    tdg::TDG tdg(parser.get_num_cpus(), parser.get_cores_per_cpu());
    tdg.parse_json(input_file);

    petri::PTPN ptpn;
    converter::TDG2PN::transform(tdg, ptpn);

    state_class::StateClassReachabilityGraph reachability_graph(ptpn);

    const auto start = std::chrono::steady_clock::now();
    reachability_graph.build(max_states, thread_count);
    const auto end = std::chrono::steady_clock::now();

    const double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    durations_ms.push_back(elapsed_ms);

    const auto& stats = reachability_graph.get_statistics();
    if (run == 0) {
      row.states = stats.total_states;
      row.transitions = stats.total_transitions;
      row.dbm_minimize_calls = stats.dbm_minimize_calls;
      row.truncated = stats.truncated;
    }
  }

  std::sort(durations_ms.begin(), durations_ms.end());
  row.best_ms = durations_ms.front();
  row.median_ms = durations_ms[durations_ms.size() / 2];
  return row;
}

void print_table(const std::vector<BenchmarkRow>& rows) {
  std::cout << std::left << std::setw(8) << "threads"
            << std::setw(8) << "runs"
            << std::setw(12) << "best_ms"
            << std::setw(12) << "median_ms"
            << std::setw(14) << "speedup_vs_1x"
            << std::setw(10) << "states"
            << std::setw(14) << "transitions"
            << std::setw(12) << "truncated"
            << std::setw(18) << "dbm_minimize"
            << "matches_1x" << '\n';

  for (const auto& row : rows) {
    std::cout << std::left << std::setw(8) << row.threads
              << std::setw(8) << row.runs
              << std::setw(12) << std::fixed << std::setprecision(2) << row.best_ms
              << std::setw(12) << row.median_ms
              << std::setw(14) << row.speedup_vs_1x
              << std::setw(10) << row.states
              << std::setw(14) << row.transitions
              << std::setw(12) << (row.truncated ? "true" : "false")
              << std::setw(18) << row.dbm_minimize_calls
              << (row.matches_baseline ? "yes" : "NO") << '\n';
  }
}

void print_csv(const std::vector<BenchmarkRow>& rows) {
  std::cout << "threads,runs,best_ms,median_ms,speedup_vs_1x,states,transitions,truncated,dbm_minimize_calls,matches_1x\n";
  for (const auto& row : rows) {
    std::cout << row.threads << ','
              << row.runs << ','
              << std::fixed << std::setprecision(2) << row.best_ms << ','
              << row.median_ms << ','
              << row.speedup_vs_1x << ','
              << row.states << ','
              << row.transitions << ','
              << (row.truncated ? "true" : "false") << ','
              << row.dbm_minimize_calls << ','
              << (row.matches_baseline ? "true" : "false") << '\n';
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  CLI::App app{"PTPN reachability benchmark"};

  std::string input_file;
  size_t max_states = 10000;
  std::string thread_list = "1,2,4,8";
  size_t repeat_count = 5;
  std::string format = "table";

  app.add_option("-f,--file", input_file, "Input JSON file")->required(true);
  app.add_option("-m,--max-states", max_states, "Maximum number of states");
  app.add_option("-t,--threads", thread_list,
                 "Comma-separated thread counts (for example 1,2,4,8)");
  app.add_option("--repeat", repeat_count, "Runs per thread count");
  app.add_option("--format", format, "Output format: table or csv");

  CLI11_PARSE(app, argc, argv);

  const auto threads = parse_thread_list(thread_list);
  std::vector<BenchmarkRow> rows;
  rows.reserve(threads.size());

  for (size_t thread_count : threads) {
    spdlog::info("[BENCH] Running build benchmark with {} thread(s)", thread_count);
    rows.push_back(run_benchmark(input_file, max_states, thread_count, repeat_count));
  }

  const double baseline_ms = rows.front().median_ms;
  const size_t baseline_states = rows.front().states;
  const size_t baseline_transitions = rows.front().transitions;
  const bool baseline_truncated = rows.front().truncated;

  for (auto& row : rows) {
    row.speedup_vs_1x = baseline_ms / row.median_ms;
    row.matches_baseline = row.states == baseline_states &&
                           row.transitions == baseline_transitions &&
                           row.truncated == baseline_truncated;
  }

  if (format == "csv") {
    print_csv(rows);
  } else {
    print_table(rows);
  }

  const auto mismatch = std::find_if(rows.begin(), rows.end(), [](const auto& row) {
    return !row.matches_baseline;
  });
  if (mismatch != rows.end()) {
    spdlog::warn("[BENCH] At least one thread count produced a different graph summary than the 1-thread baseline");
  }

  return 0;
}
