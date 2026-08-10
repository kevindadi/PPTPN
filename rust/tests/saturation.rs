//! Ported from `test/test_saturation.cpp`.

use ptpn::json::Parser;
use ptpn::petri::{PTPN, TimeInterval};
use ptpn::tdg::TDG;
use ptpn::tdg2pn::TDG2PN;

fn make_producer_net(saturating_dst: bool) -> PTPN {
    let mut ptpn = PTPN::new();
    let src = ptpn.add_place("src", 2, false);
    let dst = ptpn.add_place("dst", 1, saturating_dst);
    let t = ptpn.add_transition("t", TimeInterval::closed(0, 0), ptpn::petri::INF, -1, false);
    ptpn.set_pre_arc(src, t, 1);
    ptpn.set_post_arc(t, dst, 1);
    ptpn.set_initial_marking(src, 2);
    ptpn.set_initial_marking(dst, 1);
    ptpn
}

#[test]
fn non_saturating_full_place_keeps_producer_enabled_and_clamps_tokens() {
    // Enabling is input-driven: a full successor place never disables the
    // producer. Firing proceeds and the overflow is clamped to capacity.
    let ptpn = make_producer_net(false);
    assert!(PTPN::is_enabled(ptpn.get_marking(), &ptpn, 0));

    ptpn::petri::reset_overflow_recording();
    let after = PTPN::fire(ptpn.get_marking(), &ptpn, 0);
    assert_eq!(after[0], 1); // src consumed one token
    assert_eq!(after[1], 1); // dst clamped at capacity
    // The clamp on a non-saturating place is an invalid behavior and is recorded.
    assert_eq!(ptpn::petri::overflowed_places(), vec![1]);
}

#[test]
fn saturating_full_place_keeps_producer_enabled_and_clamps_tokens() {
    let ptpn = make_producer_net(true);
    assert!(PTPN::is_enabled(ptpn.get_marking(), &ptpn, 0));

    ptpn::petri::reset_overflow_recording();
    let after = PTPN::fire(ptpn.get_marking(), &ptpn, 0);
    assert_eq!(after[0], 1);
    assert_eq!(after[1], 1);
    // Saturating places clamp silently; no invalid-behavior record.
    assert!(ptpn::petri::overflowed_places().is_empty());
}

#[test]
fn saturating_place_below_capacity_accumulates_normally() {
    let mut ptpn = PTPN::new();
    let src = ptpn.add_place("src", 2, false);
    let dst = ptpn.add_place("dst", 2, true);
    let t = ptpn.add_transition("t", TimeInterval::closed(0, 0), ptpn::petri::INF, -1, false);
    ptpn.set_pre_arc(src, t, 1);
    ptpn.set_post_arc(t, dst, 1);
    ptpn.set_initial_marking(src, 2);
    ptpn.set_initial_marking(dst, 1);

    let mut m = PTPN::fire(ptpn.get_marking(), &ptpn, 0);
    assert_eq!(m[dst], 2);
    m = PTPN::fire(&m, &ptpn, 0);
    assert_eq!(m[dst], 2);
    assert_eq!(m[src], 0);
}

#[test]
fn parses_configured_value_and_default() {
    let with_value = r#"{
        "graph": {"name": "CapTest"},
        "configuration": {"num_cpus": 1, "cores_per_cpu": 1, "task_place_capacity": 2},
        "nodes": [
          {"id": "A", "type": "task", "priority": 1, "core": 0, "time": [[1, 2]], "locks": []}
        ],
        "edges": []
      }"#;

    let mut parser = Parser::new();
    assert!(parser.parse_string(with_value).success);
    assert_eq!(parser.get_task_place_capacity(), 2);
    assert!(parser.validate().success);

    let without_value = r#"{
        "graph": {"name": "CapDefault"},
        "configuration": {"num_cpus": 1},
        "nodes": [], "edges": []
      }"#;
    let mut default_parser = Parser::new();
    assert!(default_parser.parse_string(without_value).success);
    assert_eq!(default_parser.get_task_place_capacity(), 1);
}

#[test]
fn rejects_non_positive_capacity() {
    let json = r#"{
        "graph": {"name": "CapBad"},
        "configuration": {"num_cpus": 1, "task_place_capacity": 0},
        "nodes": [], "edges": []
      }"#;
    let mut parser = Parser::new();
    assert!(parser.parse_string(json).success);
    let validation = parser.validate();
    assert!(!validation.success);
}

fn lower_periodic_net(task_place_capacity: i32) -> PTPN {
    let json = format!(
        r#"{{
            "graph": {{"name": "PeriodicSaturation"}},
            "configuration": {{
              "num_cpus": 1,
              "cores_per_cpu": 1,
              "policy": "fixed",
              "task_place_capacity": {},
              "start": [{{"task": "A", "tokens": 1}}],
              "end": ["A"],
              "periodic": [{{"task": "A", "period": 5}}]
            }},
            "nodes": [
              {{"id": "A", "type": "task", "priority": 1, "core": 0, "time": [[20, 20]], "locks": []}}
            ],
            "edges": []
          }}"#,
        task_place_capacity
    );

    let mut tdg = TDG::new(1, 1);
    tdg.parse_json_string(&json).unwrap();
    let mut ptpn = PTPN::new();
    TDG2PN::transform(&tdg, &mut ptpn);
    ptpn
}

#[test]
fn release_stays_enabled_when_entry_is_full() {
    let ptpn = lower_periodic_net(1);

    let entry = (0..ptpn.num_places())
        .find(|&p| ptpn.get_place(p).name == "Aentry")
        .expect("entry place");
    let fire = (0..ptpn.num_transitions())
        .find(|&t| ptpn.get_transition(t).name == "A_fire")
        .expect("fire transition");

    let entry_place = ptpn.get_place(entry);
    assert!(entry_place.saturate);
    assert_eq!(entry_place.capacity, 1);

    assert_eq!(ptpn.get_marking()[entry], 1);
    assert!(PTPN::is_enabled(ptpn.get_marking(), &ptpn, fire));

    let after = PTPN::fire(ptpn.get_marking(), &ptpn, fire);
    assert_eq!(after[entry], 1);
    assert!(PTPN::is_enabled(&after, &ptpn, fire));
}

#[test]
fn capacity_two_allows_one_pending_release() {
    let ptpn = lower_periodic_net(2);

    let entry = (0..ptpn.num_places())
        .find(|&p| ptpn.get_place(p).name == "Aentry")
        .expect("entry place");
    let fire = (0..ptpn.num_transitions())
        .find(|&t| ptpn.get_transition(t).name == "A_fire")
        .expect("fire transition");
    assert_eq!(ptpn.get_place(entry).capacity, 2);

    let mut m = ptpn.get_marking().clone();
    assert_eq!(m[entry], 1);
    m = PTPN::fire(&m, &ptpn, fire);
    assert_eq!(m[entry], 2);
    m = PTPN::fire(&m, &ptpn, fire);
    assert_eq!(m[entry], 2);
}
