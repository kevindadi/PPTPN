//! Ported from `test/test_metrics.cpp`.

use ptpn::analysis::metrics::{MetricsAnalyzer, MetricsReport};
use ptpn::analysis::StateClassReachabilityGraph;
use ptpn::petri::{PTPN, TaskInfo, TimeInterval};

fn make_single_task_net(with_consume: bool) -> PTPN {
    let mut ptpn = PTPN::new();
    let entry = ptpn.add_place("Tentry", 1, false);
    let get_core = ptpn.add_transition("Tget_core", TimeInterval::closed(0, 0), 1, 0, false);
    let ready = ptpn.add_place("Tready", 1, false);
    let exec = ptpn.add_transition("Texec", TimeInterval::closed(3, 5), 1, 0, true);
    let exit = ptpn.add_place("Texit", 1, false);

    ptpn.set_initial_marking(entry, 1);
    ptpn.set_pre_arc(entry, get_core, 1);
    ptpn.set_post_arc(get_core, ready, 1);
    ptpn.set_pre_arc(ready, exec, 1);
    ptpn.set_post_arc(exec, exit, 1);

    if with_consume {
        let done = ptpn.add_place("Tdone", 1, false);
        let consume = ptpn.add_transition("Tconsume", TimeInterval::closed(0, 0), 0, -1, false);
        ptpn.set_pre_arc(exit, consume, 1);
        ptpn.set_post_arc(consume, done, 1);
    }

    ptpn.node_pn_map.insert("T".to_string(), vec![entry, get_core, ready, exec, exit]);
    ptpn.node_start_end_map.insert("T".to_string(), (entry, exit));

    let info = TaskInfo {
        core: 0,
        priority: 1,
        wcet: 5,
        bcet: 3,
        period: 0,
        deadline: 0,
        locks: vec![],
    };
    ptpn.task_info.insert("T".to_string(), info);
    ptpn
}

fn analyze(net: &PTPN) -> MetricsReport {
    let mut graph = StateClassReachabilityGraph::new(&net.net, net.m0.clone());
    graph.build(256);
    let analyzer = MetricsAnalyzer::new(graph.get_graph(), net, graph.get_graph().initial, true);
    analyzer.analyze()
}

#[test]
fn single_task_response_time_matches_execution_window() {
    let net = make_single_task_net(true);
    let report = analyze(&net);

    assert_eq!(report.tasks.len(), 1);
    let t = &report.tasks[0];
    assert_eq!(t.name, "T");
    assert!(t.observed);
    assert_eq!(t.activations, 1);
    assert!(!t.wcrt.infinite);
    assert_eq!(t.wcrt.value, 5);
    assert!(!t.bcrt.infinite);
    assert_eq!(t.bcrt.value, 3);
    assert_eq!(t.jitter.value, 2);
    assert_eq!(t.max_in_flight, 1);
    assert_eq!(t.max_preemptions, 0);
    assert!(!t.deadline_missed);

    assert!(report.bounded);
    assert!(report.schedulable);
    assert!(report.deadlock_states.is_empty());
}

#[test]
fn stuck_task_is_reported_as_deadlock() {
    let mut net = PTPN::new();
    let entry = net.add_place("Uentry", 1, false);
    let get_core = net.add_transition("Uget_core", TimeInterval::closed(0, 0), 1, 0, false);
    let ready = net.add_place("Uready", 1, false);
    let cpu = net.add_place("cpu", 1, false);
    net.set_initial_marking(entry, 1);
    net.set_pre_arc(entry, get_core, 1);
    net.set_pre_arc(cpu, get_core, 1);
    net.set_post_arc(get_core, ready, 1);

    net.node_pn_map.insert("U".to_string(), vec![entry, get_core, ready]);
    net.node_start_end_map.insert("U".to_string(), (entry, ready));
    let info = TaskInfo {
        core: 0,
        ..Default::default()
    };
    net.task_info.insert("U".to_string(), info);

    let report = analyze(&net);
    assert!(!report.deadlock_states.is_empty());
}

#[test]
fn net_without_task_info_yields_structural_only() {
    let mut net = PTPN::new();
    let in_place = net.add_place("in", 1, false);
    let done = net.add_place("done", 1, false);
    let t = net.add_transition("t", TimeInterval::closed(0, 2), 0, -1, false);
    net.set_initial_marking(in_place, 1);
    net.set_pre_arc(in_place, t, 1);
    net.set_post_arc(t, done, 1);

    let report = analyze(&net);
    assert!(report.tasks.is_empty());
    assert!(report.bounded);
    assert!(report.states >= 1);
}
