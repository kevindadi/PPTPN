//! PTPN 主程序
//!
//! 使用 R-PTPN 库进行状态类图构建与分析

use clap::Parser;
use priority::{parse_dot_file, tdg_to_ptpn};
use ptpn::scg;
use std::path::PathBuf;
use tracing::{info, Level};
use tracing_subscriber::FmtSubscriber;

#[derive(Parser, Debug)]
#[command(name = "priority")]
#[command(about = "优先级时间 Petri 网分析工具 (基于 R-PTPN)")]
struct Args {
    /// TDG DOT 文件路径 (可选，不指定则使用内置示例)
    #[arg(short, long)]
    file: Option<PathBuf>,

    /// CPU 数量
    #[arg(long, default_value = "1")]
    cpus: i32,

    /// 每 CPU 核心数
    #[arg(long, default_value = "2")]
    cores: i32,

    /// 使用内置 three_task 示例 (忽略 --file)
    #[arg(long)]
    example: bool,
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

    let ptpn = if args.example {
        info!("使用内置 three_task 示例");
        ptpn::examples::three_task::build_three_task_ptpn()
    } else if let Some(ref path) = args.file {
        info!("解析 TDG 文件: {}", path.display());
        let mut tdg = parse_dot_file(path, args.cpus, args.cores)?;
        info!("TDG 解析完成");
        tdg_to_ptpn(&mut tdg)
    } else {
        info!("未指定输入，使用内置 three_task 示例");
        ptpn::examples::three_task::build_three_task_ptpn()
    };

    info!("构建状态类图 (SCG)...");
    let scg = scg::build_scg(&ptpn);
    info!("SCG 构建完成: {} 个状态类, {} 条边", scg.classes.len(), scg.edges.len());

    for task in &ptpn.tasks {
        let wcet = ptpn::analysis::compute_wcet(&ptpn, &scg, *task);
        let wcrt = ptpn::analysis::compute_wcrt(&ptpn, &scg, *task);
        info!("Task {}: WCET={}, WCRT={}", task, wcet, wcrt);
    }

    let deadlocks = ptpn::deadlock::detect_global_deadlocks(&ptpn, &scg);
    let starvations = ptpn::deadlock::detect_starvation_sccs(&ptpn, &scg);
    info!("Deadlocks: {:?}", deadlocks);
    info!("Starvations: {} SCCs", starvations.len());

    Ok(())
}
