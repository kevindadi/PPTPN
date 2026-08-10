//! Ported from `test/test_analysis_semantics.cpp`.

use ptpn::analysis::dbm::{get_dbm_instrumentation, reset_dbm_instrumentation, DBM, INF_TIME};
use ptpn::analysis::ptpn_analysis::{
    out_edge_transitions, StateClassReachabilityGraph, ScGraph,
};
use ptpn::analysis::state_class::{contains, StateClass};
use ptpn::petri::{PTPN, TimeInterval, INF};

fn make_two_independent_transitions_net() -> PTPN {
    let mut ptpn = PTPN::new();
    let left_in = ptpn.add_place("left_in", 1, false);
    let right_in = ptpn.add_place("right_in", 1, false);
    ptpn.add_place("left_done", 1, false);
    ptpn.add_place("right_done", 1, false);
    ptpn.set_initial_marking(left_in, 1);
    ptpn.set_initial_marking(right_in, 1);

    let left = ptpn.add_transition("left", TimeInterval::closed(0, 2), INF, -1, false);
    let right = ptpn.add_transition("right", TimeInterval::closed(0, 2), INF, -1, false);
    ptpn.set_pre_arc(left_in, left, 1);
    ptpn.set_post_arc(left, 2, 1);
    ptpn.set_pre_arc(right_in, right, 1);
    ptpn.set_post_arc(right, 3, 1);
    ptpn
}

fn make_same_core_priority_net() -> PTPN {
    let mut ptpn = PTPN::new();
    let input = ptpn.add_place("input", 2, false);
    ptpn.set_initial_marking(input, 1);

    ptpn.add_transition("low", TimeInterval::closed(0, 5), 1, 0, true);
    ptpn.add_transition("high", TimeInterval::closed(3, 3), 9, 0, false);
    ptpn.set_pre_arc(input, 0, 1);
    ptpn.set_post_arc(0, input, 1);
    ptpn.set_pre_arc(input, 1, 1);
    ptpn.set_post_arc(1, input, 1);
    ptpn
}

fn make_persistent_survivor_net() -> PTPN {
    let mut ptpn = PTPN::new();
    let trigger_in = ptpn.add_place("trigger_in", 1, false);
    let survivor_in = ptpn.add_place("survivor_in", 1, false);
    ptpn.add_place("trigger_done", 1, false);
    ptpn.add_place("survivor_done", 1, false);
    ptpn.set_initial_marking(trigger_in, 1);
    ptpn.set_initial_marking(survivor_in, 1);

    let trigger = ptpn.add_transition("trigger", TimeInterval::closed(2, 2), INF, -1, false);
    let survivor = ptpn.add_transition("survivor", TimeInterval::closed(0, 5), INF, -1, false);
    ptpn.set_pre_arc(trigger_in, trigger, 1);
    ptpn.set_post_arc(trigger, 2, 1);
    ptpn.set_pre_arc(survivor_in, survivor, 1);
    ptpn.set_post_arc(survivor, 3, 1);
    ptpn
}

fn make_resume_net() -> PTPN {
    let mut ptpn = PTPN::new();
    let low_in = ptpn.add_place("low_in", 1, false);
    let high_in = ptpn.add_place("high_in", 1, false);
    ptpn.add_place("low_done", 1, false);
    ptpn.add_place("high_done", 1, false);
    ptpn.set_initial_marking(low_in, 1);
    ptpn.set_initial_marking(high_in, 1);

    ptpn.add_transition("low", TimeInterval::closed(0, 8), 1, 0, true);
    ptpn.add_transition("high", TimeInterval::closed(0, 3), 9, 0, false);
    ptpn.set_pre_arc(low_in, 0, 1);
    ptpn.set_post_arc(0, 2, 1);
    ptpn.set_pre_arc(high_in, 1, 1);
    ptpn.set_post_arc(1, 3, 1);
    ptpn
}

fn make_newly_enabled_siblings_net() -> PTPN {
    let mut ptpn = PTPN::new();
    let input = ptpn.add_place("input", 1, false);
    let shared = ptpn.add_place("shared", 2, false);
    ptpn.add_place("left_done", 1, false);
    ptpn.add_place("right_done", 1, false);
    ptpn.set_initial_marking(input, 1);

    let trigger = ptpn.add_transition("trigger", TimeInterval::closed(0, 0), INF, -1, false);
    let left = ptpn.add_transition("left", TimeInterval::closed(0, 4), INF, -1, false);
    let right = ptpn.add_transition("right", TimeInterval::closed(0, 6), INF, -1, false);
    ptpn.set_pre_arc(input, trigger, 1);
    ptpn.set_post_arc(trigger, shared, 2);
    ptpn.set_pre_arc(shared, left, 1);
    ptpn.set_post_arc(left, 2, 1);
    ptpn.set_pre_arc(shared, right, 1);
    ptpn.set_post_arc(right, 3, 1);
    ptpn
}

#[test]
fn dbm_future_removes_only_unfrozen_lower_bounds() {
    let mut dbm = DBM::new(3);
    dbm.set_constraint(0, 1, -2);
    dbm.set_constraint(0, 2, -4);
    dbm.freeze_clock(2);

    reset_dbm_instrumentation();
    dbm.future();

    assert_eq!(dbm.get_constraint(0, 1), INF_TIME);
    assert_eq!(dbm.get_constraint(0, 2), -4);
    assert_eq!(get_dbm_instrumentation().minimize_calls, 1);
}

#[test]
fn dbm_constrain_upper_bound_only_tightens_finite_bounds() {
    let mut dbm = DBM::new(2);
    dbm.set_constraint(1, 0, 9);

    dbm.constrain_upper_bound(1, 7);
    assert_eq!(dbm.get_constraint(1, 0), 7);
    dbm.constrain_upper_bound(1, 8);
    assert_eq!(dbm.get_constraint(1, 0), 7);
    dbm.constrain_upper_bound(1, INF_TIME);
    assert_eq!(dbm.get_constraint(1, 0), 7);
}

#[test]
fn dbm_synchronize_clocks_forces_pairwise_equality() {
    let mut dbm = DBM::new(3);
    dbm.set_constraint(0, 1, -2);
    dbm.set_constraint(1, 0, 5);
    dbm.set_constraint(0, 2, -4);
    dbm.set_constraint(2, 0, 7);

    dbm.synchronize_clocks(&[1, 2]);

    assert_eq!(dbm.get_constraint(1, 2), 0);
    assert_eq!(dbm.get_constraint(2, 1), 0);
}

#[test]
fn dbm_included_in_detects_zone_subset() {
    let mut tight = DBM::new(2);
    tight.set_constraint(0, 1, -2);
    tight.set_constraint(1, 0, 4);
    tight.minimize();

    let mut loose = DBM::new(2);
    loose.set_constraint(0, 1, -1);
    loose.set_constraint(1, 0, 6);
    loose.minimize();

    assert!(tight.included_in(&loose));
    assert!(!loose.included_in(&tight));
}

#[test]
fn initial_class_pins_every_clock_to_zero() {
    let ptpn = make_newly_enabled_siblings_net();
    let graph = StateClassReachabilityGraph::new(&ptpn);
    let initial = graph.compute_initial_class();

    assert!(initial.has_exec_clock(0));
    let idx = initial.exec_index(0) as usize;
    assert_eq!(initial.zone.get_constraint(0, idx), 0);
    assert_eq!(initial.zone.get_constraint(idx, 0), 0);
}

#[test]
fn branches_over_every_firable_transition() {
    let ptpn = make_two_independent_transitions_net();
    let mut graph = StateClassReachabilityGraph::new(&ptpn);
    graph.build(64);

    let transitions = out_edge_transitions(graph.get_graph(), graph.get_initial_vertex());
    assert_eq!(transitions.iter().filter(|&&t| t == 0).count(), 1);
    assert_eq!(transitions.iter().filter(|&&t| t == 1).count(), 1);
}

#[test]
fn priority_filter_fires_high_priority_not_earliest() {
    let ptpn = make_same_core_priority_net();
    let mut graph = StateClassReachabilityGraph::new(&ptpn);
    graph.build(64);

    let initial: &StateClass = &graph.get_graph()[petgraph::graph::NodeIndex::new(
        graph.get_initial_vertex(),
    )];
    assert_eq!(initial.priority_enabled, vec![1]);
    assert_eq!(initial.suspended, vec![0]);

    let transitions = out_edge_transitions(graph.get_graph(), graph.get_initial_vertex());
    assert_eq!(transitions.iter().filter(|&&t| t == 1).count(), 1);
    assert_eq!(transitions.iter().filter(|&&t| t == 0).count(), 0);
}

#[test]
fn control_transitions_are_also_priority_filtered() {
    let mut ptpn = PTPN::new();
    let low_in = ptpn.add_place("low_in", 1, false);
    let high_in = ptpn.add_place("high_in", 1, false);
    ptpn.add_place("low_done", 1, false);
    ptpn.add_place("high_done", 1, false);
    ptpn.set_initial_marking(low_in, 1);
    ptpn.set_initial_marking(high_in, 1);

    ptpn.add_transition("low_ctrl", TimeInterval::closed(0, 0), 0, -1, false);
    ptpn.add_transition("high_ctrl", TimeInterval::closed(0, 0), 1, -1, false);
    ptpn.set_pre_arc(low_in, 0, 1);
    ptpn.set_post_arc(0, 2, 1);
    ptpn.set_pre_arc(high_in, 1, 1);
    ptpn.set_post_arc(1, 3, 1);

    let graph = StateClassReachabilityGraph::new(&ptpn);
    let initial = graph.compute_initial_class();
    assert_eq!(initial.priority_enabled, vec![1]);
}

fn make_same_core_equal_priority_net() -> PTPN {
    let mut ptpn = PTPN::new();
    let a_in = ptpn.add_place("a_in", 1, false);
    let b_in = ptpn.add_place("b_in", 1, false);
    ptpn.add_place("a_done", 1, false);
    ptpn.add_place("b_done", 1, false);
    ptpn.set_initial_marking(a_in, 1);
    ptpn.set_initial_marking(b_in, 1);

    ptpn.add_transition("a_exec", TimeInterval::closed(1, 2), 5, 0, true);
    ptpn.add_transition("b_exec", TimeInterval::closed(1, 2), 5, 0, true);
    ptpn.set_pre_arc(a_in, 0, 1);
    ptpn.set_post_arc(0, 2, 1);
    ptpn.set_pre_arc(b_in, 1, 1);
    ptpn.set_post_arc(1, 3, 1);
    ptpn
}

#[test]
fn core_capacity_one_enforces_mutual_exclusion() {
    let mut ptpn = make_same_core_equal_priority_net();
    ptpn.core_parallelism.insert(0, 1);

    let graph = StateClassReachabilityGraph::new(&ptpn);
    let initial = graph.compute_initial_class();
    assert_eq!(initial.priority_enabled, vec![0]);
    assert_eq!(initial.suspended, vec![1]);
}

#[test]
fn core_capacity_two_allows_parallel_execution() {
    let mut ptpn = make_same_core_equal_priority_net();
    ptpn.core_parallelism.insert(0, 2);

    let graph = StateClassReachabilityGraph::new(&ptpn);
    let initial = graph.compute_initial_class();
    assert_eq!(initial.priority_enabled, vec![0, 1]);
    assert!(initial.suspended.is_empty());
}

#[test]
fn suspended_transition_freezes_exec_and_runs_suspension_clock() {
    let ptpn = make_same_core_priority_net();
    let graph = StateClassReachabilityGraph::new(&ptpn);
    let initial = graph.compute_initial_class();
    let elapsed = graph.time_elapse(&initial);

    assert!(elapsed.has_exec_clock(0));
    assert!(elapsed.has_susp_clock(0));
    assert!(elapsed.has_exec_clock(1));

    let low_exec = elapsed.exec_index(0) as usize;
    let low_susp = elapsed.susp_index(0) as usize;
    let high_exec = elapsed.exec_index(1) as usize;

    assert_eq!(elapsed.zone.get_constraint(low_exec, 0), 0);
    assert_eq!(elapsed.zone.get_constraint(high_exec, 0), 3);
    assert_eq!(elapsed.zone.get_constraint(low_susp, 0), 3);
}

#[test]
fn persistent_transition_keeps_accumulated_clock() {
    let ptpn = make_persistent_survivor_net();
    let graph = StateClassReachabilityGraph::new(&ptpn);
    let initial = graph.compute_initial_class();
    let elapsed = graph.time_elapse(&initial);

    let mut successor = StateClass::default();
    assert!(graph.fire(&elapsed, 0, &mut successor));

    assert!(contains(&successor.struct_enabled, 1));
    assert!(successor.has_exec_clock(1));
    let survivor = successor.exec_index(1) as usize;
    assert_eq!(successor.zone.get_constraint(0, survivor), -2);
}

#[test]
fn resumed_transition_drops_suspension_keeps_exec() {
    let ptpn = make_resume_net();
    let graph = StateClassReachabilityGraph::new(&ptpn);
    let initial = graph.compute_initial_class();

    assert_eq!(initial.priority_enabled, vec![1]);
    assert_eq!(initial.suspended, vec![0]);

    let elapsed = graph.time_elapse(&initial);
    let mut successor = StateClass::default();
    assert!(graph.fire(&elapsed, 1, &mut successor));

    assert_eq!(successor.priority_enabled, vec![0]);
    assert!(successor.suspended.is_empty());
    assert!(successor.has_exec_clock(0));
    assert!(!successor.has_susp_clock(0));
}

#[test]
fn newly_enabled_transitions_reset_to_zero() {
    let ptpn = make_newly_enabled_siblings_net();
    let graph = StateClassReachabilityGraph::new(&ptpn);
    let initial = graph.compute_initial_class();
    let elapsed = graph.time_elapse(&initial);

    let mut successor = StateClass::default();
    assert!(graph.fire(&elapsed, 0, &mut successor));

    assert!(successor.has_exec_clock(1));
    assert!(successor.has_exec_clock(2));
    let left = successor.exec_index(1) as usize;
    let right = successor.exec_index(2) as usize;
    assert_eq!(successor.zone.get_constraint(0, left), 0);
    assert_eq!(successor.zone.get_constraint(0, right), 0);
}

#[test]
fn build_terminates_and_counts_states() {
    let ptpn = make_persistent_survivor_net();
    let mut graph = StateClassReachabilityGraph::new(&ptpn);
    let states = graph.build(64);

    assert!(states >= 1);
    assert!(!graph.get_statistics().truncated);
    assert_eq!(states, graph.get_statistics().total_states);
}

#[test]
fn named_dump_includes_place_and_clock_labels() {
    let ptpn = make_same_core_priority_net();
    let graph = StateClassReachabilityGraph::new(&ptpn);
    let initial = graph.compute_initial_class();

    let dump = graph.format_state_dump(&initial);
    assert!(dump.contains("input"));
    assert!(dump.contains("E_pri"));

    let zone = graph.format_named_dbm(&initial);
    assert!(zone.contains("h(T1)"));
}
