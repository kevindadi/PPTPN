//! 状态类图与分析测试

use ptpn::analysis;
use ptpn::deadlock;
use ptpn::examples::three_task;
use ptpn::scg;

#[test]
fn test_three_task_analysis() {
    let ptpn = three_task::build_three_task_ptpn();
    let scg = scg::build_scg(&ptpn);

    for task in &ptpn.tasks {
        let _wcet = analysis::compute_wcet(&ptpn, &scg, *task);
        let _wcrt = analysis::compute_wcrt(&ptpn, &scg, *task);
    }

    let _deadlocks = deadlock::detect_global_deadlocks(&ptpn, &scg);
    let _starvations = deadlock::detect_starvation_sccs(&ptpn, &scg);
}
