//! TDG 到矩阵 PTPN 转换测试

use ptpn::{parse_dot_file, MatrixPTPN};
use std::path::Path;

#[test]
fn test_matrix_conversion() {
    let path = Path::new("example/common.dot");
    if !path.exists() {
        eprintln!("Skipping: example/common.dot not found");
        return;
    }

    let mut tdg = parse_dot_file(path, 1, 2).unwrap();
    let mut matrix = MatrixPTPN::new();
    matrix.transform_tdg_to_matrix_ptpn(&mut tdg);

    assert!(matrix.num_places() > 0);
    assert!(matrix.num_transitions() > 0);
    assert_eq!(matrix.get_marking().len(), matrix.num_places());
}
