//! State-class reachability graph construction (port of
//! `src/analysis/ptpn_analysis.cpp`).
//!
//! Builds the state-class reachability graph of a P-TPN: time elapse on a joint
//! DBM, then a branch for every priority-enabled transition that can fire.

use crate::analysis::canonicalization::{can_merge_into, check_equality, CanonicalizationMode};
use crate::analysis::dbm::{reset_dbm_instrumentation, DBM, INF_TIME};
use crate::analysis::scheduling::Scheduling;
use crate::analysis::state_class::{
    contains, hash_state_class, ClockKind, ClockVar, FiringEdge, StateClass, TransitionSet,
};
use crate::petri::{reset_overflow_recording, PTPN, INF};
use petgraph::graph::{DiGraph, NodeIndex};
use petgraph::visit::EdgeRef;
use petgraph::Direction;
use std::collections::HashMap;

/// Boost-graph analog: directed graph with StateClass vertex payload and
/// FiringEdge edge payload.
pub type ScGraph = DiGraph<StateClass, FiringEdge>;
pub type ScVertex = usize;

#[derive(Debug, Clone, Default)]
pub struct Statistics {
    pub total_states: usize,
    pub total_transitions: usize,
    pub dedup_hits: usize,
    pub truncated: bool,
}

/// Builds the state-class reachability graph.
pub struct StateClassReachabilityGraph<'a> {
    net: &'a PTPN,
    graph: ScGraph,
    initial_vertex: ScVertex,
    stats: Statistics,
    mode: CanonicalizationMode,
    extrapolation_enabled: bool,
    extrapolation_k: i32,
    next_id: usize,
    vertices_by_marking: HashMap<Vec<i32>, Vec<ScVertex>>,
    vertices_by_hash: HashMap<u64, Vec<ScVertex>>,
}

impl<'a> StateClassReachabilityGraph<'a> {
    pub fn new(net: &'a PTPN) -> Self {
        StateClassReachabilityGraph {
            net,
            graph: DiGraph::new(),
            initial_vertex: 0,
            stats: Statistics::default(),
            mode: CanonicalizationMode::Equality,
            extrapolation_enabled: false,
            extrapolation_k: -1,
            next_id: 0,
            vertices_by_marking: HashMap::new(),
            vertices_by_hash: HashMap::new(),
        }
    }

    pub fn set_canonicalization_mode(&mut self, mode: CanonicalizationMode) {
        self.mode = mode;
    }

    pub fn get_canonicalization_mode(&self) -> CanonicalizationMode {
        self.mode
    }

    pub fn set_extrapolation(&mut self, enabled: bool) {
        self.extrapolation_enabled = enabled;
        if !enabled {
            return;
        }
        // Uniform bound k: the largest finite endpoint of any static interval.
        let mut k: i32 = 0;
        for t in 0..self.net.num_transitions() {
            k = k.max(self.effective_earliest(t));
            let latest = self.effective_latest(t);
            if latest != INF_TIME {
                k = k.max(latest);
            }
        }
        self.extrapolation_k = k;
    }

    pub fn get_extrapolation(&self) -> bool {
        self.extrapolation_enabled
    }

    pub fn extrapolation_bound(&self) -> i32 {
        self.extrapolation_k
    }

    pub fn get_graph(&self) -> &ScGraph {
        &self.graph
    }

    pub fn get_graph_mut(&mut self) -> &mut ScGraph {
        &mut self.graph
    }

    pub fn get_initial_vertex(&self) -> ScVertex {
        self.initial_vertex
    }

    pub fn get_statistics(&self) -> &Statistics {
        &self.stats
    }

    fn effective_earliest(&self, transition: usize) -> i32 {
        self.net.get_transition(transition).time_interval.effective_earliest()
    }

    fn effective_latest(&self, transition: usize) -> i32 {
        let latest = self
            .net
            .get_transition(transition)
            .time_interval
            .effective_latest();
        if latest == INF {
            INF_TIME
        } else {
            latest
        }
    }

    fn recompute_sets(&self, state: &mut StateClass) {
        let (struct_enabled, priority_enabled, suspended) =
            Scheduling::compute_sets(self.net, &state.marking);
        state.struct_enabled = struct_enabled;
        state.priority_enabled = priority_enabled;
        state.suspended = suspended;
    }

    fn build_layout(&self, state: &mut StateClass) {
        let num_transitions = self.net.num_transitions();

        state.clock_vars.clear();
        state.clock_vars.push(ClockVar {
            kind: ClockKind::Zero,
            transition: 0,
        });
        state.exec_clock_of_transition = vec![-1; num_transitions];
        state.susp_clock_of_transition = vec![-1; num_transitions];

        for &t in &state.struct_enabled {
            let exec_idx = state.clock_vars.len();
            state.clock_vars.push(ClockVar {
                kind: ClockKind::Execution,
                transition: t,
            });
            state.exec_clock_of_transition[t] = exec_idx as i32;

            if contains(&state.suspended, t) {
                let susp_idx = state.clock_vars.len();
                state.clock_vars.push(ClockVar {
                    kind: ClockKind::Suspension,
                    transition: t,
                });
                state.susp_clock_of_transition[t] = susp_idx as i32;
            }
        }
    }

    pub fn compute_initial_class(&self) -> StateClass {
        let mut state = StateClass::default();
        state.marking = self.net.get_marking().clone();
        state.elapsed_time = 0.0;

        self.recompute_sets(&mut state);
        self.build_layout(&mut state);

        // Every clock starts at zero, pinned to x0.
        let n = state.clock_vars.len();
        let mut zone = DBM::new(n);
        for i in 1..n {
            zone.set_constraint(0, i, 0);
            zone.set_constraint(i, 0, 0);
        }
        zone.minimize();
        state.zone = zone;
        state
    }

    /// TimeElapse operator: running clocks advance, frozen clocks stay put.
    pub fn time_elapse(&self, state: &StateClass) -> StateClass {
        let mut out = state.clone();
        let n = out.zone.size();
        if n == 0 {
            return out;
        }

        // V_run = { h_t : t in E_pri } U { w_t : t in suspended }.
        let mut running = vec![false; n];
        for i in 1..n.min(out.clock_vars.len()) {
            let var = out.clock_vars[i];
            match var.kind {
                ClockKind::Suspension => running[i] = true,
                ClockKind::Execution => {
                    if contains(&out.priority_enabled, var.transition) {
                        running[i] = true;
                    }
                }
                ClockKind::Zero => {}
            }
        }

        // Release each running clock's upper bound relative to stationary vars.
        for i in 1..n {
            if !running[i] {
                continue;
            }
            for j in 0..n {
                if j != i && !running[j] {
                    out.zone.set_constraint(i, j, INF_TIME);
                }
            }
        }

        // Strong time semantics: cap each active execution clock at its deadline.
        for &t in &out.priority_enabled {
            if !out.has_exec_clock(t) {
                continue;
            }
            let upper = self.effective_latest(t);
            if upper == INF_TIME {
                continue;
            }
            let idx = out.exec_index(t) as usize;
            let current = out.zone.get_constraint(idx, 0);
            if current == INF_TIME || upper < current {
                out.zone.set_constraint(idx, 0, upper);
            }
        }

        out.zone.minimize();
        out
    }

    pub fn is_firable(&self, elapsed: &StateClass, t: usize) -> bool {
        if !contains(&elapsed.priority_enabled, t) || !elapsed.has_exec_clock(t) {
            return false;
        }
        let idx = elapsed.exec_index(t) as usize;
        let max_h = elapsed.zone.get_constraint(idx, 0); // largest feasible h_t
        if max_h == INF_TIME {
            return true;
        }
        max_h >= self.effective_earliest(t)
    }

    fn build_successor_zone(
        &self,
        successor: &mut StateClass,
        fired: &DBM,
        source: &StateClass,
        fired_transition: usize,
    ) {
        let n = successor.clock_vars.len();
        let mut zone = DBM::new(n);

        let mut source_index = vec![-1i32; n];
        source_index[0] = 0; // x0 maps to x0
        for i in 1..n {
            let var = successor.clock_vars[i];
            let t = var.transition;
            match var.kind {
                ClockKind::Execution => {
                    if t != fired_transition && source.has_exec_clock(t) {
                        source_index[i] = source.exec_index(t);
                    }
                }
                ClockKind::Suspension => {
                    if t != fired_transition && source.has_susp_clock(t) {
                        source_index[i] = source.susp_index(t);
                    }
                }
                ClockKind::Zero => {}
            }
        }

        // Carry over joint constraints between all surviving clocks.
        for i in 0..n {
            if source_index[i] < 0 {
                continue;
            }
            for j in 0..n {
                if source_index[j] < 0 {
                    continue;
                }
                zone.set_constraint(
                    i,
                    j,
                    fired.get_constraint(source_index[i] as usize, source_index[j] as usize),
                );
            }
        }

        // Pin every freshly created clock to zero (equal to x0).
        for i in 1..n {
            if source_index[i] < 0 {
                zone.set_constraint(0, i, 0);
                zone.set_constraint(i, 0, 0);
            }
        }

        zone.minimize();
        successor.zone = zone;
    }

    pub fn fire(&self, elapsed: &StateClass, t: usize, successor: &mut StateClass) -> bool {
        if !self.is_firable(elapsed, t) {
            return false;
        }

        // Step 1: intersect the firing-domain constraint h_t >= downSI(t).
        let mut fired = elapsed.zone.clone();
        let idx = elapsed.exec_index(t) as usize;
        let lower = self.effective_earliest(t);
        if !fired.tighten(0, idx, -lower) {
            return false;
        }

        // Step 2: discrete token shuffle.
        *successor = StateClass::default();
        successor.marking = PTPN::fire(&elapsed.marking, self.net, t);

        // Steps 3 & 4: recompute sets + layout, rebuild zone.
        self.recompute_sets(successor);
        self.build_layout(successor);
        self.build_successor_zone(successor, &fired, elapsed, t);

        let h_lower = -elapsed.zone.get_constraint(0, idx);
        let firing_instant = lower.max(h_lower);
        successor.elapsed_time = elapsed.elapsed_time + 0.max(firing_instant) as f64;

        true
    }

    fn find_match(&self, state: &StateClass) -> Option<ScVertex> {
        if self.mode == CanonicalizationMode::Equality {
            let hash = hash_state_class(state);
            if let Some(candidates) = self.vertices_by_hash.get(&hash) {
                for &candidate in candidates {
                    let existing = &self.graph[NodeIndex::new(candidate)];
                    if check_equality(state, existing) {
                        return Some(candidate);
                    }
                }
            }
            return None;
        }

        if let Some(candidates) = self.vertices_by_marking.get(&state.marking) {
            for &candidate in candidates {
                let existing = &self.graph[NodeIndex::new(candidate)];
                if can_merge_into(state, existing, self.mode) {
                    return Some(candidate);
                }
            }
        }
        None
    }

    fn add_state(&mut self, mut state: StateClass) -> ScVertex {
        state.id = self.next_id;
        self.next_id += 1;

        let node = self.graph.add_node(state);
        let idx = node.index();

        if self.mode == CanonicalizationMode::Equality {
            let hash = hash_state_class(&self.graph[node]);
            self.vertices_by_hash.entry(hash).or_default().push(idx);
        } else {
            let marking = self.graph[node].marking.clone();
            self.vertices_by_marking.entry(marking).or_default().push(idx);
        }
        idx
    }

    /// Explores the reachability graph, stopping once `max_states` classes exist.
    pub fn build(&mut self, max_states: usize) -> usize {
        self.graph.clear();
        self.vertices_by_marking.clear();
        self.vertices_by_hash.clear();
        self.stats = Statistics::default();
        self.next_id = 0;
        reset_dbm_instrumentation();
        reset_overflow_recording();

        let mut initial = self.compute_initial_class();
        if self.extrapolation_enabled {
            initial.zone.extrapolate(self.extrapolation_k);
        }
        self.initial_vertex = self.add_state(initial);
        self.stats.total_states = 1;

        let mut frontier: Vec<ScVertex> = vec![self.initial_vertex];

        while !frontier.is_empty() {
            let mut next_frontier: Vec<ScVertex> = Vec::new();

            for &u in &frontier {
                if self.stats.total_states >= max_states {
                    self.stats.truncated = true;
                    break;
                }

                // Extract everything from the source state up front (borrow rules).
                struct EntryBounds {
                    has_clock: bool,
                    low: i32,
                    high: i32,
                }
                let current = &self.graph[NodeIndex::new(u)];
                let elapsed = self.time_elapse(current);
                let enabled: TransitionSet = current.priority_enabled.clone();
                let mut entry_bounds: Vec<EntryBounds> = Vec::with_capacity(enabled.len());
                for &t in &enabled {
                    let hcidx = current.exec_index(t);
                    if hcidx > 0 {
                        let cidx = hcidx as usize;
                        entry_bounds.push(EntryBounds {
                            has_clock: true,
                            low: -current.zone.get_constraint(0, cidx),
                            high: current.zone.get_constraint(cidx, 0),
                        });
                    } else {
                        entry_bounds.push(EntryBounds {
                            has_clock: false,
                            low: 0,
                            high: 0,
                        });
                    }
                }

                for (k, &t) in enabled.iter().enumerate() {
                    if !self.is_firable(&elapsed, t) {
                        continue;
                    }

                    let mut successor = StateClass::default();
                    if !self.fire(&elapsed, t, &mut successor) {
                        continue;
                    }
                    if self.extrapolation_enabled {
                        successor.zone.extrapolate(self.extrapolation_k);
                    }

                    // The real firing window of h_t.
                    let hidx = elapsed.exec_index(t) as usize;
                    let h_low = -elapsed.zone.get_constraint(0, hidx);
                    let h_high = elapsed.zone.get_constraint(hidx, 0);
                    let up = self.effective_latest(t);
                    let fire_min = 0.max(self.effective_earliest(t)).max(h_low);
                    let mut fire_max = h_high;
                    if up != INF_TIME && (fire_max == INF_TIME || up < fire_max) {
                        fire_max = up;
                    }

                    let mut dwell_min = fire_min;
                    let mut dwell_max = fire_max;
                    if entry_bounds[k].has_clock {
                        let entry_low = entry_bounds[k].low;
                        let entry_high = entry_bounds[k].high;
                        dwell_min = 0.max(fire_min - entry_high);
                        dwell_max = if fire_max == INF_TIME || entry_low == INF_TIME {
                            INF_TIME
                        } else {
                            0.max(fire_max - entry_low)
                        };
                    }
                    let mut edge = FiringEdge::new(t as i32, fire_min, fire_max);
                    edge.dwell_min = dwell_min;
                    edge.dwell_max = dwell_max;

                    if let Some(v) = self.find_match(&successor) {
                        self.stats.dedup_hits += 1;
                        self.graph
                            .add_edge(NodeIndex::new(u), NodeIndex::new(v), edge);
                        self.stats.total_transitions += 1;
                        continue;
                    }

                    if self.stats.total_states >= max_states {
                        self.stats.truncated = true;
                        continue;
                    }

                    let v = self.add_state(successor);
                    self.stats.total_states += 1;
                    self.graph
                        .add_edge(NodeIndex::new(u), NodeIndex::new(v), edge);
                    self.stats.total_transitions += 1;
                    next_frontier.push(v);
                }

                if self.stats.truncated {
                    break;
                }
            }

            if self.stats.truncated {
                break;
            }
            frontier = next_frontier;
        }

        self.stats.total_states
    }
}

// ---------------------------------------------------------------------------
// Formatting / DOT helpers used by the CLI and tests.
// ---------------------------------------------------------------------------

pub fn escape_dot(value: &str) -> String {
    let mut out = String::new();
    for ch in value.chars() {
        match ch {
            '\\' => out.push_str("\\\\"),
            '"' => out.push_str("\\\""),
            '\n' => out.push_str("\\n"),
            _ => out.push(ch),
        }
    }
    out
}

fn escape_json(value: &str) -> String {
    let mut out = String::new();
    for ch in value.chars() {
        match ch {
            '\\' => out.push_str("\\\\"),
            '"' => out.push_str("\\\""),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            _ => out.push(ch),
        }
    }
    out
}

#[allow(dead_code)]
fn html_escape(value: &str) -> String {
    let mut out = String::new();
    for ch in value.chars() {
        match ch {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            _ => out.push(ch),
        }
    }
    out
}

impl<'a> StateClassReachabilityGraph<'a> {
    pub fn format_marking(net: &PTPN, marking: &[i32]) -> String {
        let mut out = String::from("[");
        let mut first = true;
        for (i, &tokens) in marking.iter().enumerate() {
            if tokens <= 0 {
                continue;
            }
            if !first {
                out.push_str(", ");
            }
            first = false;
            if i < net.num_places() {
                out.push_str(&net.get_place(i).name);
            } else {
                out.push_str(&format!("P{}", i));
            }
            if tokens != 1 {
                out.push_str(&format!("({})", tokens));
            }
        }
        if first {
            out.push_str("empty");
        }
        out.push(']');
        out
    }

    pub fn format_transition_label(&self, transition_id: usize) -> String {
        if transition_id >= self.net.num_transitions() {
            return format!("T{}", transition_id);
        }
        let trans = self.net.get_transition(transition_id);
        let mut out = format!("T{}({}", transition_id, trans.name);
        out.push_str(&format!(", priority={}", trans.priority));
        out.push_str(&format!(", core={}", trans.core));
        if trans.suspendable {
            out.push_str(", suspendable");
        }
        out.push(')');
        out
    }

    pub fn format_transitions(&self, transitions: &TransitionSet) -> String {
        if transitions.is_empty() {
            return "(none)".to_string();
        }
        let parts: Vec<String> = transitions
            .iter()
            .map(|&t| self.format_transition_label(t))
            .collect();
        parts.join(", ")
    }

    pub fn format_named_dbm(&self, state: &StateClass) -> String {
        if state.zone.size() == 0 {
            return "DBM(empty)".to_string();
        }

        let mut labels: Vec<String> = vec!["x0".to_string(); state.zone.size()];
        let mut cell_width = 6usize;
        for i in 1..state.zone.size().min(state.clock_vars.len()) {
            let var = state.clock_vars[i];
            let prefix = if var.kind == ClockKind::Suspension { "w" } else { "h" };
            labels[i] = format!("{}(T{})", prefix, var.transition);
            cell_width = cell_width.max(labels[i].len() + 2);
        }

        let mut out = format!("DBM(size={})\n", state.zone.size());
        out.push_str(&format!("{:<width$}|", "", width = cell_width));
        for label in &labels {
            out.push_str(&format!(" {:<width$}|", label, width = cell_width));
        }
        out.push('\n');

        for i in 0..state.zone.size() {
            out.push_str(&format!("{:<width$}|", labels[i], width = cell_width));
            for j in 0..state.zone.size() {
                let value = state.zone.get_constraint(i, j);
                let rendered = if value == INF_TIME {
                    "inf".to_string()
                } else {
                    value.to_string()
                };
                out.push_str(&format!(" {:<width$}|", rendered, width = cell_width));
            }
            out.push('\n');
        }
        out
    }

    pub fn format_state_dump(&self, state: &StateClass) -> String {
        let mut out = format!("State {}\n", state.id);
        out.push_str(&format!("  Elapsed time: {}\n", state.elapsed_time));
        out.push_str(&format!(
            "  Marking: {}\n",
            Self::format_marking(self.net, &state.marking)
        ));
        out.push_str(&format!(
            "  E_struct: {}\n",
            self.format_transitions(&state.struct_enabled)
        ));
        out.push_str(&format!(
            "  E_pri (active): {}\n",
            self.format_transitions(&state.priority_enabled)
        ));
        out.push_str(&format!(
            "  Suspended: {}\n",
            self.format_transitions(&state.suspended)
        ));
        out.push_str("  Zone:\n");
        out.push_str(&self.format_named_dbm(state));
        out
    }

    /// Exports the reachability graph in Graphviz DOT format.
    pub fn save_to_dot(&self, file_path: &str) -> bool {
        let mut out = String::new();
        out.push_str("digraph StateClassGraph {\n");
        out.push_str("  rankdir=LR;\n");
        out.push_str("  node [shape=box, fontname=\"Helvetica\", color=\"#111111\"];\n");
        out.push_str("  edge [fontname=\"Helvetica\"];\n\n");

        for node in self.graph.node_indices() {
            let state = &self.graph[node];
            out.push_str(&format!(
                "  s{} [label=\"{}\", tooltip=\"{}\"];\n",
                state.id,
                escape_dot(&self.format_state_dump(state)),
                escape_dot(&self.format_state_dump(state))
            ));
        }

        out.push('\n');

        for edge in self.graph.edge_references() {
            let src = self.graph[edge.source()].id;
            let tgt = self.graph[edge.target()].id;
            let fe = edge.weight();
            let window = format!(
                "[{}, {}]",
                fe.firing_min,
                if fe.firing_max == INF_TIME {
                    "inf".to_string()
                } else {
                    fe.firing_max.to_string()
                }
            );
            let dwell = format!(
                "[{}, {}]",
                fe.dwell_min,
                if fe.dwell_max == INF_TIME {
                    "inf".to_string()
                } else {
                    fe.dwell_max.to_string()
                }
            );
            let label = format!(
                "{}\\n@{} dwell={}",
                self.format_transition_label(fe.transition_id as usize),
                window,
                dwell
            );
            out.push_str(&format!(
                "  s{} -> s{} [label=\"{}\"];\n",
                src,
                tgt,
                escape_dot(&label)
            ));
        }

        out.push_str("}\n");

        match std::fs::write(file_path, out) {
            Ok(_) => true,
            Err(_) => false,
        }
    }

    /// Exports the reachability graph as a structured JSON document.
    pub fn save_to_json(&self, file_path: &str) -> bool {
        let mut out = String::from("{\n  \"states\": [\n");

        let mut first_state = true;
        for node in self.graph.node_indices() {
            let state = &self.graph[node];
            if !first_state {
                out.push_str(",\n");
            }
            first_state = false;
            out.push_str("    {\n");
            out.push_str(&format!("      \"id\": {},\n", state.id));
            out.push_str("      \"marking\": [");
            for (i, &v) in state.marking.iter().enumerate() {
                if i > 0 {
                    out.push_str(", ");
                }
                out.push_str(&v.to_string());
            }
            out.push_str("],\n");
            out.push_str(&format!(
                "      \"active\": \"{}\",\n",
                escape_json(&self.format_transitions(&state.priority_enabled))
            ));
            out.push_str(&format!(
                "      \"suspended\": \"{}\",\n",
                escape_json(&self.format_transitions(&state.suspended))
            ));
            out.push_str(&format!(
                "      \"elapsed_time\": {:.2},\n",
                state.elapsed_time
            ));
            out.push_str(&format!(
                "      \"zone\": \"{}\"\n",
                escape_json(&self.format_named_dbm(state))
            ));
            out.push_str("    }");
        }

        out.push_str("\n  ],\n  \"transitions\": [\n");

        let mut first_edge = true;
        for edge in self.graph.edge_references() {
            let src = self.graph[edge.source()].id;
            let tgt = self.graph[edge.target()].id;
            let fe = edge.weight();
            if !first_edge {
                out.push_str(",\n");
            }
            first_edge = false;
            out.push_str("    {\n");
            out.push_str(&format!("      \"source\": {},\n", src));
            out.push_str(&format!("      \"target\": {},\n", tgt));
            out.push_str(&format!(
                "      \"transition_id\": {},\n",
                fe.transition_id
            ));
            out.push_str(&format!(
                "      \"transition_label\": \"{}\",\n",
                escape_json(&self.format_transition_label(fe.transition_id as usize))
            ));
            out.push_str(&format!("      \"firing_min\": {},\n", fe.firing_min));
            out.push_str(&format!(
                "      \"firing_max\": {},\n",
                if fe.firing_max == INF_TIME {
                    "null".to_string()
                } else {
                    fe.firing_max.to_string()
                }
            ));
            out.push_str(&format!("      \"dwell_min\": {},\n", fe.dwell_min));
            out.push_str(&format!(
                "      \"dwell_max\": {}\n",
                if fe.dwell_max == INF_TIME {
                    "null".to_string()
                } else {
                    fe.dwell_max.to_string()
                }
            ));
            out.push_str("    }");
        }

        out.push_str("\n  ],\n  \"statistics\": {\n");
        out.push_str(&format!("    \"total_states\": {},\n", self.stats.total_states));
        out.push_str(&format!(
            "    \"total_transitions\": {},\n",
            self.stats.total_transitions
        ));
        out.push_str(&format!(
            "    \"dedup_hits\": {},\n",
            self.stats.dedup_hits
        ));
        out.push_str(&format!(
            "    \"truncated\": {}\n",
            if self.stats.truncated { "true" } else { "false" }
        ));
        out.push_str("  }\n}\n");

        match std::fs::write(file_path, out) {
            Ok(_) => true,
            Err(_) => false,
        }
    }
}

/// Collects out-edge transition ids for a vertex (test helper).
pub fn out_edge_transitions(graph: &ScGraph, vertex: ScVertex) -> Vec<i32> {
    let mut result: Vec<i32> = graph
        .edges_directed(NodeIndex::new(vertex), Direction::Outgoing)
        .map(|e| e.weight().transition_id)
        .collect();
    result.sort_unstable();
    result
}

/// Collects the set of distinct markings stored in the reachability graph.
pub fn reachable_markings(graph: &ScGraph) -> std::collections::BTreeSet<Vec<i32>> {
    let mut markings = std::collections::BTreeSet::new();
    for node in graph.node_indices() {
        markings.insert(graph[node].marking.clone());
    }
    markings
}
