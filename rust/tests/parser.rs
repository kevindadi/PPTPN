//! Ported from `test/test_ptpn_parser.cpp`.

use ptpn::parser::{PTPNAST, PTPNParser};

fn parse_ok(input: &str) -> PTPNAST {
    let mut ast = PTPNAST::default();
    let mut error = String::new();
    assert!(
        PTPNParser::parse(input, &mut ast, &mut error),
        "parse failed: {}",
        error
    );
    ast
}

#[test]
fn parse_places() {
    let ast = parse_ok("places P0, P1, P2");
    assert_eq!(ast.places.len(), 3);
    assert_eq!(ast.places[0].id, "P0");
    assert_eq!(ast.places[1].id, "P1");
    assert_eq!(ast.places[2].id, "P2");
}

#[test]
fn parse_places_with_name() {
    let ast = parse_ok("places P0:Start, P1:Buffer");
    assert_eq!(ast.places.len(), 2);
    assert_eq!(ast.places[0].id, "P0");
    assert_eq!(ast.places[0].name, "Start");
    assert_eq!(ast.places[1].id, "P1");
    assert_eq!(ast.places[1].name, "Buffer");
}

#[test]
fn parse_places_with_capacity() {
    let ast = parse_ok("places P0:1, P1:5, P2:10");
    assert_eq!(ast.places[0].capacity, 1);
    assert_eq!(ast.places[1].capacity, 5);
    assert_eq!(ast.places[2].capacity, 10);
}

#[test]
fn parse_places_with_name_and_capacity() {
    let ast = parse_ok("places P0:Start:1, P1:Buffer:5");
    assert_eq!(ast.places[0].id, "P0");
    assert_eq!(ast.places[0].name, "Start");
    assert_eq!(ast.places[0].capacity, 1);
    assert_eq!(ast.places[1].id, "P1");
    assert_eq!(ast.places[1].name, "Buffer");
    assert_eq!(ast.places[1].capacity, 5);
}

#[test]
fn parse_transitions() {
    let ast = parse_ok("transitions T0 [0, 0]");
    assert_eq!(ast.transitions.len(), 1);
    assert_eq!(ast.transitions[0].id, "T0");
    assert_eq!(ast.transitions[0].time_min, 0);
    assert_eq!(ast.transitions[0].time_max, 0);
}

#[test]
fn parse_transitions_with_attributes() {
    let ast = parse_ok("transitions T0 [1, 5] @priority=10 @core=2");
    assert_eq!(ast.transitions[0].id, "T0");
    assert_eq!(ast.transitions[0].time_min, 1);
    assert_eq!(ast.transitions[0].time_max, 5);
    assert_eq!(ast.transitions[0].priority, 10);
    assert_eq!(ast.transitions[0].core, 2);
}

#[test]
fn parse_transitions_with_suspendable() {
    let ast = parse_ok("transitions T0 [3, 8] suspendable");
    assert_eq!(ast.transitions[0].time_min, 3);
    assert_eq!(ast.transitions[0].time_max, 8);
    assert!(ast.transitions[0].suspendable);
}

#[test]
fn parse_arcs() {
    let ast = parse_ok("places P0, P1\ntransitions T0 [0, 0]\nP0 -> T0\nT0 -> P1");
    assert_eq!(ast.arcs.len(), 2);
    assert_eq!(ast.arcs[0].source, "P0");
    assert_eq!(ast.arcs[0].target, "T0");
    assert_eq!(ast.arcs[1].source, "T0");
    assert_eq!(ast.arcs[1].target, "P1");
}

#[test]
fn parse_arcs_with_weight() {
    let ast = parse_ok("places P0\ntransitions T0 [0, 0]\nP0 -> T0:2");
    assert_eq!(ast.arcs.len(), 1);
    assert_eq!(ast.arcs[0].source, "P0");
    assert_eq!(ast.arcs[0].target, "T0");
    assert_eq!(ast.arcs[0].weight, 2);
}

#[test]
fn parse_init() {
    let ast = parse_ok("places P0\n@init P0:1");
    assert_eq!(ast.initial_marking.len(), 1);
    assert_eq!(ast.initial_marking[0].place, "P0");
    assert_eq!(ast.initial_marking[0].tokens, 1);
}

#[test]
fn parse_init_multiple() {
    let ast = parse_ok("places P0, P1\n@init P0:1, P1:2");
    assert_eq!(ast.initial_marking.len(), 2);
    assert_eq!(ast.initial_marking[0].place, "P0");
    assert_eq!(ast.initial_marking[0].tokens, 1);
    assert_eq!(ast.initial_marking[1].place, "P1");
    assert_eq!(ast.initial_marking[1].tokens, 2);
}

#[test]
fn parse_complete_ptpn() {
    let content = r#"
places
    P0: Start:1
    P1: Running:1

transitions
    T0 [0, 0] @priority=0, core=-1
    T1 [3, 8] @priority=97, core=0, suspendable

P0 -> T0
T0 -> P1
P1 -> T1

@init P0:1
"#;
    let ast = parse_ok(content);
    assert_eq!(ast.places.len(), 2);
    assert_eq!(ast.transitions.len(), 2);
    assert_eq!(ast.arcs.len(), 3);
    assert_eq!(ast.initial_marking.len(), 1);
}

#[test]
fn parse_with_comments() {
    let content = r#"
// This is a comment
places P0, P1 // inline comment
/* block comment */
transitions T0 [0, 0]
P0 -> T0
T0 -> P1
"#;
    let ast = parse_ok(content);
    assert_eq!(ast.places.len(), 2);
    assert_eq!(ast.transitions.len(), 1);
    assert_eq!(ast.arcs.len(), 2);
}

#[test]
fn parse_empty_input() {
    let ast = parse_ok("");
    assert!(ast.places.is_empty());
}

#[test]
fn parse_multiple_transitions() {
    let content = r#"
transitions
    T0 [0, 0] @priority=0, core=-1
    T1 [1, 5] @priority=50, core=0
    T2 [10, 100] suspendable
"#;
    let ast = parse_ok(content);
    assert_eq!(ast.transitions.len(), 3);
    assert_eq!(ast.transitions[0].id, "T0");
    assert_eq!(ast.transitions[1].id, "T1");
    assert_eq!(ast.transitions[2].id, "T2");
    assert!(ast.transitions[2].suspendable);
}

#[test]
fn parse_transition_with_name() {
    let ast = parse_ok("transitions T0:Task1 [1, 10]");
    assert_eq!(ast.transitions[0].id, "T0");
    assert_eq!(ast.transitions[0].name, "Task1");
}

#[test]
fn parse_priority_shorthand() {
    let ast = parse_ok("transitions T0 [1, 5] @98, core=0");
    assert_eq!(ast.transitions[0].priority, 98);
    assert_eq!(ast.transitions[0].core, 0);
}

#[test]
fn parse_negative_core() {
    let ast = parse_ok("transitions T0 [0, 0] @priority=0, core=-1");
    assert_eq!(ast.transitions[0].core, -1);
}

#[test]
fn reject_duplicate_place() {
    let mut ast = PTPNAST::default();
    let mut error = String::new();
    assert!(!PTPNParser::parse("places P0, P0", &mut ast, &mut error));
    assert!(error.contains("Duplicate place"));
}

#[test]
fn reject_unknown_arc_reference() {
    let mut ast = PTPNAST::default();
    let mut error = String::new();
    assert!(!PTPNParser::parse(
        "places P0\ntransitions T0 [0,0]\nP0 -> T0\nT0 -> Missing",
        &mut ast,
        &mut error
    ));
    assert!(error.contains("not found"));
}

#[test]
fn parse_transitions_with_strict_left_endpoint() {
    let ast = parse_ok("transitions T0 (1, 5]");
    assert!(ast.transitions[0].left_open);
    assert!(!ast.transitions[0].right_open);
    assert_eq!(ast.transitions[0].time_min, 1);
    assert_eq!(ast.transitions[0].time_max, 5);
}

#[test]
fn parse_transitions_with_strict_right_endpoint() {
    let ast = parse_ok("transitions T0 [1, 5)");
    assert!(!ast.transitions[0].left_open);
    assert!(ast.transitions[0].right_open);
}

#[test]
fn parse_transitions_with_both_open_endpoints() {
    let ast = parse_ok("transitions T0 (1, 5)");
    assert!(ast.transitions[0].left_open);
    assert!(ast.transitions[0].right_open);
}

#[test]
fn reject_empty_strict_integer_interval() {
    let mut ast = PTPNAST::default();
    let mut error = String::new();
    assert!(!PTPNParser::parse("transitions T0 (1, 2)", &mut ast, &mut error));
    assert!(error.contains("empty integer time range"));
}

#[test]
fn strict_left_endpoint_inclusive_right_regression() {
    let ast = parse_ok("transitions T0 [1, 5]");
    assert!(!ast.transitions[0].left_open);
    assert!(!ast.transitions[0].right_open);
    assert_eq!(ast.transitions[0].time_min, 1);
    assert_eq!(ast.transitions[0].time_max, 5);
}

#[test]
fn builder_preserves_strict_interval_metadata() {
    let ptpn = ptpn::parser::PTPNBuilder::parse("transitions T0 (1, 5] ").unwrap();
    assert_eq!(ptpn.num_transitions(), 1);
    let transition = ptpn.get_transition(0);
    assert!(transition.time_interval.left_open);
    assert!(!transition.time_interval.right_open);
    assert_eq!(transition.time_interval.effective_earliest(), 2);
    assert_eq!(transition.time_interval.effective_latest(), 5);
}
