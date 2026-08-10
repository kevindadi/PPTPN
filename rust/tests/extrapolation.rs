//! Ported from `test/test_extrapolation.cpp`.

use ptpn::analysis::dbm::DBM;
use ptpn::analysis::ptpn_analysis::{reachable_markings, StateClassReachabilityGraph};
use ptpn::petri::{PTPN, TimeInterval, INF};

fn make_unbounded_clock_net() -> PTPN {
    let mut ptpn = PTPN::new();
    let tick_p = ptpn.add_place("tick_p", 1, false);
    let slow_p = ptpn.add_place("slow_p", 1, false);
    let slow_done = ptpn.add_place("slow_done", 1, false);
    ptpn.set_initial_marking(tick_p, 1);
    ptpn.set_initial_marking(slow_p, 1);

    let tick = ptpn.add_transition("tick", TimeInterval::closed(1, 1), INF, -1, false);
    let slow = ptpn.add_transition("slow", TimeInterval::closed(5, INF), INF, -1, false);
    ptpn.set_pre_arc(tick_p, tick, 1);
    ptpn.set_post_arc(tick, tick_p, 1);
    ptpn.set_pre_arc(slow_p, slow, 1);
    ptpn.set_post_arc(slow, slow_done, 1);
    ptpn
}

fn make_preemption_net() -> PTPN {
    let mut ptpn = PTPN::new();
    let low_in = ptpn.add_place("low_in", 1, false);
    let low_done = ptpn.add_place("low_done", 1, false);
    let high_in = ptpn.add_place("high_in", 1, false);
    let high_done = ptpn.add_place("high_done", 1, false);
    ptpn.set_initial_marking(low_in, 1);
    ptpn.set_initial_marking(high_in, 1);

    let low = ptpn.add_transition("low", TimeInterval::closed(4, 6), 1, 0, true);
    let high = ptpn.add_transition("high", TimeInterval::closed(2, 3), 9, 0, false);
    ptpn.set_pre_arc(low_in, low, 1);
    ptpn.set_post_arc(low, low_done, 1);
    ptpn.set_pre_arc(high_in, high, 1);
    ptpn.set_post_arc(high, high_done, 1);
    ptpn
}

#[test]
fn relaxes_bounds_above_k_and_clamps_below_minus_k() {
    let mut dbm = DBM::new(3);
    dbm.set_constraint(0, 1, -7);
    dbm.set_constraint(1, 0, 12);
    dbm.set_constraint(0, 2, -1);
    dbm.set_constraint(2, 0, 2);
    dbm.minimize();

    let original = dbm.clone();
    dbm.extrapolate(5);

    assert!(original.included_in(&dbm));
    assert_eq!(dbm.get_constraint(1, 0), ptpn::analysis::dbm::INF_TIME);
    assert_eq!(dbm.get_constraint(0, 1), -6);
    assert_eq!(dbm.get_constraint(2, 0), 2);
    assert_eq!(dbm.get_constraint(0, 2), -1);
}

#[test]
fn frozen_clock_rows_and_columns_are_untouched() {
    let mut dbm = DBM::new(3);
    dbm.set_constraint(0, 1, -7);
    dbm.set_constraint(1, 0, 12);
    dbm.set_constraint(0, 2, -8);
    dbm.set_constraint(2, 0, 9);
    dbm.minimize();
    dbm.freeze_clock(1);

    let original = dbm.clone();
    dbm.extrapolate(5);

    assert!(original.included_in(&dbm));
    assert_eq!(dbm.get_constraint(1, 0), 12);
    assert_eq!(dbm.get_constraint(0, 1), -7);
    assert_eq!(dbm.get_constraint(2, 0), 14);
    assert_eq!(dbm.get_constraint(0, 2), -5);
}

#[test]
fn unbounded_interval_net_becomes_finite() {
    let without_net = make_unbounded_clock_net();
    let mut without = StateClassReachabilityGraph::new(&without_net);
    without.build(200);
    assert!(without.get_statistics().truncated);

    let with_net = make_unbounded_clock_net();
    let mut with = StateClassReachabilityGraph::new(&with_net);
    with.set_extrapolation(true);
    assert_eq!(with.extrapolation_bound(), 5);
    with.build(200);
    assert!(!with.get_statistics().truncated);
    assert!(with.get_statistics().total_states < 50);

    assert_eq!(
        reachable_markings(without.get_graph()),
        reachable_markings(with.get_graph())
    );
}

#[test]
fn preemption_net_keeps_exact_state_graph() {
    let plain_net = make_preemption_net();
    let mut plain = StateClassReachabilityGraph::new(&plain_net);
    plain.build(10000);
    assert!(!plain.get_statistics().truncated);

    let extra_net = make_preemption_net();
    let mut extra = StateClassReachabilityGraph::new(&extra_net);
    extra.set_extrapolation(true);
    extra.build(10000);
    assert!(!extra.get_statistics().truncated);

    assert_eq!(
        reachable_markings(plain.get_graph()),
        reachable_markings(extra.get_graph())
    );
    assert!(extra.get_statistics().total_states <= plain.get_statistics().total_states);
}

#[test]
fn finite_net_is_unchanged() {
    let plain_net = make_preemption_net();
    let mut plain = StateClassReachabilityGraph::new(&plain_net);
    plain.build(10000);

    let extra_net = make_preemption_net();
    let mut extra = StateClassReachabilityGraph::new(&extra_net);
    extra.set_extrapolation(true);
    extra.build(10000);

    assert_eq!(
        extra.get_statistics().total_states,
        plain.get_statistics().total_states
    );
    assert_eq!(
        extra.get_statistics().total_transitions,
        plain.get_statistics().total_transitions
    );
}
