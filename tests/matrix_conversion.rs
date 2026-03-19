//! TDG 到 PTPN 转换测试

use priority::{parse_dot_file, tdg_to_ptpn};
use ptpn::scg;
use std::path::Path;

#[test]
fn test_tdg_to_ptpn() {
    // 使用 simple_chain.dot 保证快速终止；common.dot 含周期任务较慢，见 tests/termination.rs
    let path = Path::new("example/simple_chain.dot");
    if !path.exists() {
        eprintln!("Skipping: example/simple_chain.dot not found");
        return;
    }

    let mut tdg = parse_dot_file(path, 1, 2).unwrap();
    let ptpn = tdg_to_ptpn(&mut tdg);

    assert!(ptpn.p1.len() + ptpn.p2.len() > 0);
    assert!(ptpn.t1.len() + ptpn.t2.len() > 0);

    let scg = scg::build_scg(&ptpn);
    assert!(scg.classes.len() > 0);
}
