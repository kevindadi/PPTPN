//! R-PTPN 基础测试

use ptpn::examples::three_task;
use ptpn::scg;

#[test]
fn test_three_task_scg() {
    let ptpn = three_task::build_three_task_ptpn();
    let scg = scg::build_scg(&ptpn);
    assert!(scg.classes.len() > 0);
    assert!(!scg.edges.is_empty() || scg.classes.len() <= 1);
}
