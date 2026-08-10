//! PTPN CLI — Priority Timed Petri Net analyzer.
//!
//! Subcommands:
//!   ptpn tdg  -f <input.json>   TDG JSON -> TDG -> PTPN -> state-class analysis
//!   ptpn ptpn -f <model.ptpn>   `.ptpn` source -> PTPN -> state-class analysis

use clap::{Parser as ClapParser, Subcommand};
use ptpn::analysis::canonicalization::CanonicalizationMode;
use ptpn::analysis::metrics::MetricsAnalyzer;
use ptpn::analysis::ptpn_analysis::StateClassReachabilityGraph;
use ptpn::json::Parser as JsonParser;
use ptpn::petri::PTPN;
use ptpn::tdg::TDG;
use ptpn::tdg2pn::TDG2PN;
use std::path::Path;

#[derive(ClapParser)]
#[command(name = "ptpn", version = "1.0.0", about = "PTPN - Priority Timed Petri Net Analyzer")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Analyze from TDG JSON input.
    Tdg {
        #[arg(short, long)]
        file: String,
        #[arg(short = 'm', long, default_value_t = 10000)]
        max_states: usize,
        #[arg(long, default_value = "equality")]
        canonicalization: String,
        #[arg(long)]
        no_analysis: bool,
        #[arg(long)]
        extrapolation: bool,
        #[arg(long)]
        export_scg: Option<String>,
        #[arg(long)]
        export_ptpn: Option<String>,
        #[arg(long)]
        export_metrics: Option<String>,
        #[arg(long)]
        export_tdg: Option<String>,
        #[arg(long)]
        export_wcet: Option<String>,
    },
    /// Analyze from PTPN source file.
    Ptpn {
        #[arg(short, long)]
        file: String,
        #[arg(short = 'm', long, default_value_t = 10000)]
        max_states: usize,
        #[arg(long, default_value = "equality")]
        canonicalization: String,
        #[arg(long)]
        no_analysis: bool,
        #[arg(long)]
        extrapolation: bool,
        #[arg(long)]
        export_scg: Option<String>,
        #[arg(long)]
        export_ptpn: Option<String>,
        #[arg(long)]
        export_metrics: Option<String>,
    },
}

fn parse_canonicalization(mode: &str) -> CanonicalizationMode {
    match mode {
        "max-lower" => CanonicalizationMode::MaxLowerBound,
        "intersection" => CanonicalizationMode::Intersection,
        _ => CanonicalizationMode::Equality,
    }
}

fn export_ptpn_dot(ptpn: &PTPN, path: &str) -> bool {
    let mut out = String::from("digraph PTPN {\n  rankdir=LR;\n");
    for (i, place) in ptpn.places.iter().enumerate() {
        out.push_str(&format!(
            "  p{} [label=\"{}\\n[{}]\"];\n",
            i,
            place.name,
            if place.capacity == ptpn::petri::INF {
                "inf".to_string()
            } else {
                place.capacity.to_string()
            }
        ));
    }
    for (i, trans) in ptpn.transitions.iter().enumerate() {
        out.push_str(&format!(
            "  t{} [label=\"T{}\\n{}\"];\n",
            i, i, trans.name
        ));
    }
    for t in 0..ptpn.num_transitions() {
        for &(p, w) in &ptpn.pre_arcs[t] {
            let _ = w;
            out.push_str(&format!("  p{} -> t{};\n", p, t));
        }
        for &(p, w) in &ptpn.post_arcs[t] {
            let _ = w;
            out.push_str(&format!("  t{} -> p{};\n", t, p));
        }
    }
    out.push_str("}\n");
    match std::fs::write(path, out) {
        Ok(_) => true,
        Err(_) => false,
    }
}

fn write_wcet_json(tdg: &TDG, path: &str) -> bool {
    let mut out = String::from("{\n  \"tasks\": [\n");
    let mut task_id = 0;
    let mut first = true;
    for node in &tdg.all_task {
        let Some(task) = node.as_task() else {
            continue;
        };
        let task_wcet: i32 = task.time.iter().map(|&(_, hi)| hi).sum();
        if !first {
            out.push_str(",\n");
        }
        first = false;
        out.push_str("    {\n");
        out.push_str(&format!("      \"id\": {},\n", task_id));
        out.push_str(&format!("      \"name\": \"{}\",\n", task.name));
        out.push_str(&format!("      \"wcet\": {},\n", task_wcet));
        out.push_str(&format!("      \"segments\": {}\n", task.time.len()));
        out.push_str("    }");
        task_id += 1;
    }
    out.push_str("\n  ]\n}\n");
    match std::fs::write(path, out) {
        Ok(_) => true,
        Err(_) => false,
    }
}

fn run_analysis(
    ptpn: &PTPN,
    max_states: usize,
    canonicalization: CanonicalizationMode,
    extrapolation: bool,
    scg_dot: Option<&str>,
    scg_json: Option<&str>,
    metrics_json: Option<&str>,
) {
    let mut graph = StateClassReachabilityGraph::new(ptpn);
    graph.set_canonicalization_mode(canonicalization);
    if extrapolation {
        graph.set_extrapolation(true);
    }
    let state_count = graph.build(max_states);
    let stats = graph.get_statistics().clone();

    println!("[SCG] Reachability graph built with {} states", state_count);
    println!(
        "[SCG] transitions={}, dedup_hits={}, truncated={}",
        stats.total_transitions, stats.dedup_hits, if stats.truncated { "true" } else { "false" }
    );

    if let Some(path) = scg_dot {
        if graph.save_to_dot(path) {
            println!("[OUTPUT] State class graph exported to: {}", path);
        } else {
            eprintln!("[OUTPUT] Failed to export state class graph to {}", path);
        }
    }
    if let Some(path) = scg_json {
        if graph.save_to_json(path) {
            println!("[OUTPUT] State class graph JSON exported to: {}", path);
        }
    }

    if let Some(path) = metrics_json {
        let exact = canonicalization == CanonicalizationMode::Equality;
        let analyzer = MetricsAnalyzer::new(
            graph.get_graph(),
            ptpn,
            graph.get_initial_vertex(),
            exact,
        );
        let report = analyzer.analyze();
        if MetricsAnalyzer::save_to_json(&report, path) {
            println!("[OUTPUT] Metrics exported to: {}", path);
            println!(
                "[METRICS] schedulable={}, bounded={}, deadlocks={}",
                report.schedulable,
                report.bounded,
                report.deadlock_states.len()
            );
        }
    }
}

fn main() {
    let cli = Cli::parse();
    match &cli.command {
        Commands::Tdg {
            file,
            max_states,
            canonicalization,
            no_analysis,
            extrapolation,
            export_scg,
            export_ptpn,
            export_metrics,
            export_tdg,
            export_wcet,
        } => {
            let mut parser = JsonParser::new();
            let parse_result = parser.parse_file(file);
            if !parse_result.success {
                eprintln!("ERROR: Failed to parse JSON file: {}", parse_result.error_message);
                std::process::exit(1);
            }
            let validation = parser.validate();
            if !validation.success {
                eprintln!("ERROR: Input validation failed:");
                for err in &validation.errors {
                    eprintln!("  - {}", err);
                }
                std::process::exit(1);
            }
            for warn in &validation.warnings {
                println!("Warnings:");
                println!("  - {}", warn);
            }

            let mut tdg = TDG::new(parser.get_num_cpus(), parser.get_cores_per_cpu());
            tdg.load_from_parser(&parser, false);

            if let Some(path) = export_tdg {
                if tdg.export_to_dot(path) {
                    println!("[OUTPUT] TDG DOT exported to: {}", path);
                }
            }
            if let Some(path) = export_wcet {
                if write_wcet_json(&tdg, path) {
                    println!("[OUTPUT] WCET JSON exported to: {}", path);
                }
            }

            let mut ptpn = PTPN::new();
            TDG2PN::transform(&tdg, &mut ptpn);
            println!(
                "[PTPN] Places: {}, Transitions: {}",
                ptpn.num_places(),
                ptpn.num_transitions()
            );

            if let Some(path) = export_ptpn {
                if export_ptpn_dot(&ptpn, path) {
                    println!("[OUTPUT] PTPN DOT exported to: {}", path);
                }
            }

            if *no_analysis {
                println!("[SCG] Reachability analysis skipped");
                return;
            }

            let mode = parse_canonicalization(canonicalization);
            run_analysis(
                &ptpn,
                *max_states,
                mode,
                *extrapolation,
                export_scg.as_deref(),
                None,
                export_metrics.as_deref(),
            );
        }
        Commands::Ptpn {
            file,
            max_states,
            canonicalization,
            no_analysis,
            extrapolation,
            export_scg,
            export_ptpn,
            export_metrics,
        } => {
            let ptpn = match ptpn::parser::PTPNBuilder::parse_file(file) {
                Ok(p) => p,
                Err(e) => {
                    eprintln!("ERROR: Failed to parse PTPN file: {}", e);
                    std::process::exit(1);
                }
            };
            println!(
                "[PTPN] Places: {}, Transitions: {}",
                ptpn.num_places(),
                ptpn.num_transitions()
            );

            if *no_analysis {
                println!("[SCG] Reachability analysis skipped");
                return;
            }

            if let Some(path) = export_ptpn {
                if export_ptpn_dot(&ptpn, path) {
                    println!("[OUTPUT] PTPN DOT exported to: {}", path);
                }
            }

            let mode = parse_canonicalization(canonicalization);
            run_analysis(
                &ptpn,
                *max_states,
                mode,
                *extrapolation,
                export_scg.as_deref(),
                None,
                export_metrics.as_deref(),
            );
        }
    }
}

// Ensures the `Path` import is used (kept for future extension).
#[allow(dead_code)]
fn _path_placeholder(p: &Path) -> String {
    p.display().to_string()
}
