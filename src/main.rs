//! PTPN 主程序
//!
//! TDG 解析 → 矩阵 PTPN → 状态类图

use clap::Parser;
use ptpn::{matrix_to_graph, parse_dot_file, save_to_dot};
use std::path::PathBuf;
use tracing::{info, Level};
use tracing_subscriber::FmtSubscriber;

#[derive(Parser, Debug)]
#[command(name = "ptpn")]
#[command(about = "优先级时间 Petri 网分析工具")]
struct Args {
    /// TDG DOT 文件路径
    #[arg(short, long, default_value = "example/common.dot")]
    file: PathBuf,

    /// CPU 数量
    #[arg(long, default_value = "1")]
    cpus: i32,

    /// 每 CPU 核心数
    #[arg(long, default_value = "2")]
    cores: i32,

    /// 最大状态数限制
    #[arg(long)]
    max_states: Option<usize>,

    /// 输出 PTPN 图 DOT 路径
    #[arg(long, default_value = "matrix_ptpn.dot")]
    output_ptpn: PathBuf,

    /// 输出状态类图 DOT 路径
    #[arg(long, default_value = "state_class_graph.dot")]
    output_scg: PathBuf,

    /// 输出状态类图 JSON 路径
    #[arg(long, default_value = "state_class_graph.json")]
    output_scg_json: PathBuf,
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let subscriber = FmtSubscriber::builder()
        .with_max_level(Level::INFO)
        .with_target(true)
        .with_env_filter(
            tracing_subscriber::EnvFilter::from_default_env().add_directive(Level::INFO.into()),
        )
        .finish();
    tracing::subscriber::set_global_default(subscriber)?;

    let args = Args::parse();

    info!("Starting TDG parsing for file: {}", args.file.display());
    let mut tdg = parse_dot_file(&args.file, args.cpus, args.cores)?;
    info!("TDG parsing completed");

    info!("转换为矩阵形式的 PTPN...");
    let mut matrix_ptpn = ptpn::MatrixPTPN::new();
    matrix_ptpn.transform_tdg_to_matrix_ptpn(&mut tdg);
    info!(
        "矩阵形式 PTPN 转换完成: {} 个库所, {} 个变迁",
        matrix_ptpn.num_places(),
        matrix_ptpn.num_transitions()
    );

    let graph = matrix_to_graph(&matrix_ptpn);
    if save_to_dot(&graph, &args.output_ptpn).is_ok() {
        info!("矩阵 PTPN 图已导出到: {}", args.output_ptpn.display());
    }

    let max_states = args.max_states.unwrap_or(usize::MAX);
    info!("开始构建状态类可达图 (最大状态数: {})...", max_states);

    let mut scg = ptpn::StateClassReachabilityGraph::new(matrix_ptpn);
    let num_states = scg.build(max_states);
    info!("状态类可达图构建完成");

    let stats = scg.get_statistics();
    info!("  生成状态数: {}", stats.total_states);
    info!("  状态转移数: {}", stats.total_transitions);
    info!("  使能变迁计数: {}", stats.enabled_transitions_count);
    info!("  剪枝状态数: {}", stats.pruned_states_count);

    if scg.save_to_dot(&args.output_scg).is_ok() {
        info!("状态类图已导出到: {}", args.output_scg.display());
    }
    if scg.save_to_json(&args.output_scg_json).is_ok() {
        info!("状态类图已导出到: {}", args.output_scg_json.display());
    }

    Ok(())
}
