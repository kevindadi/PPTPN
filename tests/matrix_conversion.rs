//! TDG 到 PTPN 转换测试

use priority::{parse_dot_file, tdg_to_ptpn};
use ptpn::scg;
use std::path::Path;

#[test]
fn test_tdg_to_ptpn() {
    let path = Path::new("example/common.dot");
    if !path.exists() {
        eprintln!("Skipping: example/common.dot not found");
        return;
    }

    let mut tdg = parse_dot_file(path, 1, 2).unwrap();
    let ptpn = tdg_to_ptpn(&mut tdg);

    assert!(ptpn.p1.len() + ptpn.p2.len() > 0);
    assert!(ptpn.t1.len() + ptpn.t2.len() > 0);

    let scg = scg::build_scg(&ptpn);
    assert!(scg.classes.len() > 0);
}
