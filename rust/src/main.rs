//! PTPN CLI — Priority Timed Petri Net analyzer.
//!
//! Subcommands:
//!   ptpn tdg  -f <input.json>   TDG JSON -> TDG -> PTPN -> state-class analysis
//!   ptpn ptpn -f <model.ptpn>   `.ptpn` source -> PTPN -> state-class analysis

use clap::{Parser as ClapParser, Subcommand};
use ptpn::analysis::metrics::MetricsAnalyzer;
use ptpn::analysis::{CanonicalizationMode, StateClassReachabilityGraph, TimedReachabilityConfig};
use ptpn::json::Parser as JsonParser;
use ptpn::petri::PTPN;
use ptpn::tdg::TDG;
use ptpn::tdg2pn::TDG2PN;
use unipn::net::ArcDir;

#[derive(ClapParser)]
#[command(
    name = "ptpn",
    version = "1.0.0",
    about = "PTPN - Priority Timed Petri Net Analyzer"
)]
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
    for (i, place) in ptpn.net.places.iter().enumerate() {
        let cap = match place.kind.capacity {
            None => "inf".to_string(),
            Some(c) => c.to_string(),
        };
        out.push_str(&format!(
            "  p{} [label=\"{}\\n[{}]\"];\n",
            i, place.name, cap
        ));
    }
    for (i, trans) in ptpn.net.transitions.iter().enumerate() {
        out.push_str(&format!("  t{} [label=\"T{}\\n{}\"];\n", i, i, trans.name));
    }
    for arc in &ptpn.net.arcs {
        match arc.direction {
            ArcDir::Input => out.push_str(&format!(
                "  p{} -> t{};\n",
                arc.place.index(),
                arc.transition.index()
            )),
            ArcDir::Output => out.push_str(&format!(
                "  t{} -> p{};\n",
                arc.transition.index(),
                arc.place.index()
            )),
            _ => {}
        }
    }
    out.push_str("}\n");
    std::fs::write(path, out).is_ok()
}

fn export_scg_dot(graph: &unipn::analysis::timed::StateClassGraph, path: &str) -> bool {
    let mut out = String::from("digraph StateClassGraph {\n  rankdir=LR;\n");
    for (i, state) in graph.states.iter().enumerate() {
        out.push_str(&format!(
            "  s{} [label=\"s{}\\n{}\"];\n",
            state.id, i, state.id
        ));
    }
    for &(src, tgt, ref fe) in &graph.edges {
        out.push_str(&format!(
            "  s{} -> s{} [label=\"T{}@{}\"];\n",
            src, tgt, fe.transition_id, fe.firing_min
        ));
    }
    out.push_str("}\n");
    std::fs::write(path, out).is_ok()
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
    std::fs::write(path, out).is_ok()
}

fn run_analysis(
    ptpn: &PTPN,
    max_states: usize,
    canonicalization: CanonicalizationMode,
    extrapolation: bool,
    scg_dot: Option<&str>,
    metrics_json: Option<&str>,
) {
    let config = TimedReachabilityConfig {
        canonicalization,
        extrapolation,
        core_parallelism: ptpn.core_parallelism.clone(),
    };
    let mut graph = StateClassReachabilityGraph::with_config(&ptpn.net, ptpn.m0.clone(), config);
    let state_count = graph.build(max_states);
    let stats = graph.get_graph().stats.clone();

    println!("[SCG] Reachability graph built with {} states", state_count);
    println!(
        "[SCG] transitions={}, dedup_hits={}, truncated={}",
        stats.total_transitions,
        stats.dedup_hits,
        if stats.truncated { "true" } else { "false" }
    );

    if let Some(path) = scg_dot
        && export_scg_dot(graph.get_graph(), path)
    {
        println!("[OUTPUT] State class graph exported to: {}", path);
    }

    if let Some(path) = metrics_json {
        let exact = canonicalization == CanonicalizationMode::Equality;
        let analyzer =
            MetricsAnalyzer::new(graph.get_graph(), ptpn, graph.get_graph().initial, exact);
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
                eprintln!(
                    "ERROR: Failed to parse JSON file: {}",
                    parse_result.error_message
                );
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

            if let Some(path) = export_tdg
                && tdg.export_to_dot(path)
            {
                println!("[OUTPUT] TDG DOT exported to: {}", path);
            }
            if let Some(path) = export_wcet
                && write_wcet_json(&tdg, path)
            {
                println!("[OUTPUT] WCET JSON exported to: {}", path);
            }

            let mut ptpn = PTPN::new();
            TDG2PN::transform(&tdg, &mut ptpn);
            println!(
                "[PTPN] Places: {}, Transitions: {}",
                ptpn.num_places(),
                ptpn.num_transitions()
            );

            if let Some(path) = export_ptpn
                && export_ptpn_dot(&ptpn, path)
            {
                println!("[OUTPUT] PTPN DOT exported to: {}", path);
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

            if let Some(path) = export_ptpn
                && export_ptpn_dot(&ptpn, path)
            {
                println!("[OUTPUT] PTPN DOT exported to: {}", path);
            }

            let mode = parse_canonicalization(canonicalization);
            run_analysis(
                &ptpn,
                *max_states,
                mode,
                *extrapolation,
                export_scg.as_deref(),
                export_metrics.as_deref(),
            );
        }
    }
}
