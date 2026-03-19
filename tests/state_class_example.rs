//! 状态类生成测试

use ptpn::{parse_dot_file, MatrixPTPN, StateClassReachabilityGraph};
use std::path::Path;

#[test]
fn test_state_class_example() {
    let path = Path::new("example/common.dot");
    if !path.exists() {
        eprintln!("Skipping: example/common.dot not found");
        return;
    }

    let mut tdg = parse_dot_file(path, 1, 2).unwrap();
    let mut matrix = MatrixPTPN::new();
    matrix.transform_tdg_to_matrix_ptpn(&mut tdg);

    let mut scg = StateClassReachabilityGraph::new(matrix);
    let num_states = scg.build(50);

    assert!(num_states > 0);
    let stats = scg.get_statistics();
    assert!(stats.total_states > 0);
    assert!(stats.total_transitions >= 0);
}
