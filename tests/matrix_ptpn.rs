//! 矩阵 PTPN 单元测试

use ptpn::{MatrixPTPN, TimeInterval};

#[test]
fn test_matrix_ptpn_basic() {
    let mut net = MatrixPTPN::new();

    let p0 = net.add_place("p0", 1);
    let p1 = net.add_place("p1", 1);
    let t0 = net.add_transition(
        "t0",
        TimeInterval {
            earliest: 0,
            latest: 10,
        },
        1,
        0,
        false,
    );

    net.set_pre_arc(p0, t0, 1);
    net.set_post_arc(t0, p1, 1);
    net.set_initial_marking(p0, 1);

    assert_eq!(net.num_places(), 2);
    assert_eq!(net.num_transitions(), 1);
    assert!(MatrixPTPN::is_enabled(net.get_marking(), &net, t0));
    assert_eq!(net.get_enabled_transitions(), vec![t0]);
}

#[test]
fn test_fire_transition() {
    let mut net = MatrixPTPN::new();
    let p0 = net.add_place("p0", 1);
    let p1 = net.add_place("p1", 1);
    let t0 = net.add_transition("t0", TimeInterval::default(), 1, 0, false);
    net.set_pre_arc(p0, t0, 1);
    net.set_post_arc(t0, p1, 1);
    net.set_initial_marking(p0, 1);

    let new_marking = MatrixPTPN::fire(net.get_marking(), &net, t0).unwrap();
    assert_eq!(new_marking[p0], 0);
    assert_eq!(new_marking[p1], 1);
}
