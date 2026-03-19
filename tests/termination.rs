//! 终止性测试：验证各示例能正常完成 parse → convert → build_scg 流程

use priority::{parse_dot_file, tdg_to_ptpn};
use ptpn::scg;
use std::path::Path;

fn run_pipeline(path: &Path, cpus: i32, cores: i32) {
    let mut tdg = parse_dot_file(path, cpus, cores).expect("parse_dot_file 应成功");
    let ptpn = tdg_to_ptpn(&mut tdg);
    let scg = scg::build_scg(&ptpn);

    assert!(
        ptpn.p1.len() + ptpn.p2.len() > 0,
        "PTPN 应有库所"
    );
    assert!(
        ptpn.t1.len() + ptpn.t2.len() > 0,
        "PTPN 应有变迁"
    );
    assert!(
        scg.classes.len() > 0,
        "SCG 应有至少一个状态类"
    );
}

#[test]
fn test_simple_chain_terminates() {
    let path = Path::new("example/simple_chain.dot");
    if !path.exists() {
        eprintln!("Skipping: {} not found", path.display());
        return;
    }
    run_pipeline(path, 1, 2);
}

#[test]
fn test_two_tasks_terminates() {
    let path = Path::new("example/two_tasks.dot");
    if !path.exists() {
        eprintln!("Skipping: {} not found", path.display());
        return;
    }
    run_pipeline(path, 1, 2);
}

#[test]
fn test_fork_join_terminates() {
    let path = Path::new("example/fork_join.dot");
    if !path.exists() {
        eprintln!("Skipping: {} not found", path.display());
        return;
    }
    run_pipeline(path, 1, 2);
}

#[test]
fn test_single_task_terminates() {
    let path = Path::new("example/single_task.dot");
    if !path.exists() {
        eprintln!("Skipping: {} not found", path.display());
        return;
    }
    run_pipeline(path, 1, 2);
}

#[test]
fn test_common_terminates() {
    let path = Path::new("example/common.dot");
    if !path.exists() {
        eprintln!("Skipping: {} not found", path.display());
        return;
    }
    run_pipeline(path, 1, 2);
}
