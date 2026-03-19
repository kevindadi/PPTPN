//! 状态类可达图
//!
//! BFS 探索构建 P-PTPN 状态类可达图

use crate::dbm::{Dbm, INF_TIME};
use crate::ptpn::{MatrixPTPN, INF};
use crate::state_class::{StateClass, TransitionEdge};
use petgraph::stable_graph::StableDiGraph;
use petgraph::visit::{EdgeRef, IntoEdgeReferences};
use std::collections::{HashMap, HashSet, VecDeque};
use std::fs;
use std::path::Path;
use tracing::{debug, info};

/// 状态类唯一键 (用于去重)
#[derive(Hash, Eq, PartialEq, Clone)]
struct StateKey {
    marking: Vec<i32>,
    z1_repr: Vec<Vec<i32>>,
    z2_repr: Vec<Vec<i32>>,
}

impl StateKey {
    fn from(state: &StateClass) -> Self {
        let z1_repr = (0..state.z1.size())
            .map(|i| {
                (0..state.z1.size())
                    .map(|j| state.z1.get_constraint(i, j))
                    .collect()
            })
            .collect();
        let z2_repr = (0..state.z2.size())
            .map(|i| {
                (0..state.z2.size())
                    .map(|j| state.z2.get_constraint(i, j))
                    .collect()
            })
            .collect();
        Self {
            marking: state.marking.clone(),
            z1_repr,
            z2_repr,
        }
    }
}

/// 可达图统计
#[derive(Debug, Default, Clone)]
pub struct ReachabilityStats {
    pub total_states: usize,
    pub total_transitions: usize,
    pub enabled_transitions_count: usize,
    pub pruned_states_count: usize,
}

/// 状态类可达图
pub struct StateClassReachabilityGraph {
    ptpn: MatrixPTPN,
    graph: StableDiGraph<StateClass, TransitionEdge>,
    initial_vertex: Option<petgraph::graph::NodeIndex>,
    stats: ReachabilityStats,
    state_to_vertex: HashMap<StateKey, petgraph::graph::NodeIndex>,
    next_state_id: usize,
    pruning_enabled: bool,
}

impl StateClassReachabilityGraph {
    pub fn new(ptpn: MatrixPTPN) -> Self {
        let graph = StableDiGraph::new();
        Self {
            ptpn,
            graph,
            initial_vertex: None,
            stats: ReachabilityStats::default(),
            state_to_vertex: HashMap::new(),
            next_state_id: 0,
            pruning_enabled: false,
        }
    }

    pub fn set_pruning_enabled(&mut self, enabled: bool) {
        self.pruning_enabled = enabled;
    }

    pub fn build(&mut self, max_states: usize) -> usize {
        self.stats = ReachabilityStats::default();
        self.state_to_vertex.clear();

        let s0 = self.create_initial_state_class();
        let canonical_s0 = self.canonicalize(&s0);

        let s0_vertex = self.find_or_add_vertex(&s0);
        self.initial_vertex = Some(s0_vertex);
        self.stats.total_states = 1;

        let key = StateKey::from(&s0);
        self.state_to_vertex.insert(key, s0_vertex);

        let mut queue = VecDeque::new();
        queue.push_back(canonical_s0);

        let mut iteration = 0;
        while let Some(cur) = queue.pop_front() {
            iteration += 1;
            let u = self.find_or_add_vertex(&cur);

            debug!("[状态 {}] 处理", cur.state_id);

            let chosen = self.select_per_core(&cur.enabled);
            self.stats.enabled_transitions_count += chosen.len();

            let mut scheduled = cur.copy();
            self.apply_preemption(&chosen, &mut scheduled);

            let mut dt = 0.0;
            if self.maximal_time_elapse(&mut scheduled, &mut dt) {
                debug!("  执行最大化时间推进: dt = {}", dt);
            }

            if self.pruning_enabled && scheduled.z1.is_empty() {
                debug!("  [剪枝] Z1为空,跳过此状态");
                self.stats.pruned_states_count += 1;
                continue;
            } else if !self.pruning_enabled && scheduled.z1.is_empty() {
                debug!("  [警告] Z1为空,但剪枝已禁用,继续处理");
            }

            let mut fired_count = 0;
            for &t in &chosen {
                if let Some((nxt, tau)) = self.fire_with_dbm(t, &scheduled) {
                    debug!("  T{} -> 后继状态: ID={}", t, nxt.state_id);

                    let key = StateKey::from(&nxt);
                    let v = if let Some(&existing) = self.state_to_vertex.get(&key) {
                        existing
                    } else {
                        let canonical_nxt = self.canonicalize(&nxt);
                        let new_v = self.find_or_add_vertex(&nxt);
                        queue.push_back(canonical_nxt);
                        self.state_to_vertex.insert(key, new_v);
                        self.stats.total_states += 1;
                        debug!("  [新状态] 添加到图和队列");
                        new_v
                    };

                    self.graph.add_edge(u, v, TransitionEdge::new(t as i32, tau));
                    self.stats.total_transitions += 1;
                    fired_count += 1;
                } else {
                    if self.pruning_enabled {
                        self.stats.pruned_states_count += 1;
                    }
                }
            }

            info!(
                "[STATE_CLASS] 状态 {}: 选择了 {} 个候选变迁, 成功触发 {} 个, 队列大小: {}, 总状态数: {}",
                cur.state_id,
                chosen.len(),
                fired_count,
                queue.len(),
                self.stats.total_states
            );

            if self.stats.total_states >= max_states {
                break;
            }
        }

        info!(
            "[STATE_CLASS] 构建完成: 总迭代次数={}, 总状态数={}",
            iteration, self.stats.total_states
        );

        self.stats.total_states
    }

    pub fn get_graph(&self) -> &StableDiGraph<StateClass, TransitionEdge> {
        &self.graph
    }

    pub fn get_initial_vertex(&self) -> Option<petgraph::graph::NodeIndex> {
        self.initial_vertex
    }

    pub fn get_statistics(&self) -> &ReachabilityStats {
        &self.stats
    }

    fn create_initial_state_class(&mut self) -> StateClass {
        let mut initial = StateClass::new();
        initial.marking = self.ptpn.get_marking().clone();
        initial.state_id = self.next_state_id;
        self.next_state_id += 1;
        initial.cumulative_time = 0.0;

        let num_transitions = self.ptpn.num_transitions();
        initial.z1.resize(num_transitions + 1);
        initial.z2.resize(num_transitions + 1);

        self.compute_enabled_and_clocks(&mut initial);

        debug!("[STATE_CLASS] 创建初始状态");
        debug!("  标识: {:?}", initial.marking);
        debug!("  使能变迁: {:?}", initial.enabled);

        initial
    }

    fn select_per_core(&self, enabled: &HashSet<usize>) -> Vec<usize> {
        let mut best_per_core: HashMap<i32, usize> = HashMap::new();

        for &t in enabled {
            if let Some(trans) = self.ptpn.get_transition(t) {
                let core = trans.core;
                let priority = trans.priority;
                let entry = best_per_core.entry(core).or_insert(t);
                if let Some(other) = self.ptpn.get_transition(*entry) {
                    if priority > other.priority {
                        *entry = t;
                    }
                }
            }
        }

        let mut chosen: Vec<usize> = best_per_core.values().copied().collect();
        chosen.sort();
        chosen
    }

    fn apply_preemption(&self, chosen: &[usize], state: &mut StateClass) {
        state.suspended.clear();

        let num_transitions = self.ptpn.num_transitions();
        for u in 0..num_transitions {
            let Some(trans_u) = self.ptpn.get_transition(u) else {
                continue;
            };
            if !trans_u.suspendable {
                continue;
            }

            for &t in chosen {
                let Some(trans_t) = self.ptpn.get_transition(t) else {
                    continue;
                };
                if trans_t.core == trans_u.core && trans_t.priority > trans_u.priority {
                    state.suspended.insert(u);
                    let clock_idx = u + 1;
                    if clock_idx < state.z1.size() && clock_idx < state.z2.size() {
                        state.z1.copy_clock_constraints(clock_idx, &mut state.z2);
                        state.z1.freeze_clock(clock_idx);
                        state.z2.freeze_clock(clock_idx);
                    }
                    debug!("    T{}: 被 T{} 抢占,冻结时钟", u, t);
                    break;
                }
            }
        }
    }

    fn maximal_time_elapse(&self, state: &mut StateClass, dt: &mut f64) -> bool {
        let mut ub_star = INF_TIME;

        let num_transitions = self.ptpn.num_transitions();

        for i in 1..state.z1.size().min(num_transitions + 1) {
            if state.z1.is_frozen(i) {
                continue;
            }
            let ub = state.z1.get_constraint(i, 0);
            if ub != INF_TIME && ub < ub_star {
                ub_star = ub;
            }
        }

        for i in 1..state.z2.size().min(num_transitions + 1) {
            if state.z2.is_frozen(i) {
                continue;
            }
            let ub = state.z2.get_constraint(i, 0);
            if ub != INF_TIME && ub < ub_star {
                ub_star = ub;
            }
        }

        if ub_star == INF_TIME || ub_star <= 0 {
            *dt = 0.0;
            return false;
        }

        state.z1.elapse_time(ub_star);
        state.z2.elapse_time(ub_star);
        state.cumulative_time += ub_star as f64;
        *dt = ub_star as f64;

        debug!("  最大化时间推进: dt = {}", dt);
        true
    }

    fn fire_with_dbm(
        &mut self,
        trans_idx: usize,
        from_state: &StateClass,
    ) -> Option<(StateClass, f64)> {
        let mut to = from_state.copy();
        to.state_id = self.next_state_id;
        self.next_state_id += 1;

        let trans = self.ptpn.get_transition(trans_idx)?;
        let alpha = trans.time_interval.earliest;
        let beta = if trans.time_interval.latest == INF {
            INF_TIME
        } else {
            trans.time_interval.latest
        };

        let target_z = if trans.suspendable { &to.z2 } else { &to.z1 };
        let zcheck = target_z.restrict_for_firing(trans_idx, alpha, beta);

        if self.pruning_enabled {
            if zcheck.is_empty() || !zcheck.is_consistent() {
                debug!("    T{}: 触发窗口检查失败", trans_idx);
                return None;
            }
        } else if zcheck.is_empty() || !zcheck.is_consistent() {
            debug!("    T{}: 触发窗口检查失败,但剪枝已禁用", trans_idx);
        }

        let delta = if alpha == 0 && beta == 0 {
            0
        } else if alpha == beta {
            alpha
        } else {
            alpha
        };

        if delta > 0 {
            to.z1.elapse_time(delta);
            to.z2.elapse_time(delta);
            to.cumulative_time += delta as f64;
        }

        let fire_time = to.cumulative_time;

        to.marking = MatrixPTPN::fire(&to.marking, &self.ptpn, trans_idx).ok()?;
        if to.marking.is_empty() {
            debug!("    T{}: 触发后标识为空", trans_idx);
            return None;
        }

        let clock_idx = trans_idx + 1;
        if trans.suspendable {
            if clock_idx < to.z2.size() {
                to.z2.reset_clock(clock_idx);
            }
            to.suspended.remove(&trans_idx);
        } else {
            if clock_idx < to.z1.size() {
                to.z1.reset_clock(clock_idx);
            }
        }

        self.compute_enabled_and_clocks(&mut to);

        debug!("    T{}: 成功触发, fire_time = {}", trans_idx, fire_time);
        Some((to, fire_time))
    }

    fn compute_enabled_and_clocks(&self, state: &mut StateClass) {
        let num_transitions = self.ptpn.num_transitions();

        let mut new_enabled = HashSet::new();
        for t in 0..num_transitions {
            if MatrixPTPN::is_enabled(&state.marking, &self.ptpn, t) {
                new_enabled.insert(t);
            }
        }

        state.z1.resize(num_transitions + 1);
        state.z2.resize(num_transitions + 1);

        let old_enabled = state.enabled.clone();
        for t in &old_enabled {
            if !new_enabled.contains(t) {
                let clock_idx = t + 1;
                if clock_idx < state.z1.size() {
                    state.z1.reset_clock(clock_idx);
                }
                if clock_idx < state.z2.size() {
                    state.z2.reset_clock(clock_idx);
                }
                state.z1.unfreeze_clock(clock_idx);
                state.z2.unfreeze_clock(clock_idx);
            }
        }

        state.enabled = new_enabled.clone();

        for &trans_idx in &new_enabled {
            let trans = match self.ptpn.get_transition(trans_idx) {
                Some(t) => t,
                None => continue,
            };
            let clock_idx = trans_idx + 1;

            let alpha = trans.time_interval.earliest;
            let beta = if trans.time_interval.latest == INF {
                INF_TIME
            } else {
                trans.time_interval.latest
            };

            if trans.suspendable {
                if alpha > 0 {
                    state.z2.set_constraint(0, clock_idx, -alpha);
                } else {
                    state.z2.set_constraint(0, clock_idx, 0);
                }
                if beta != INF_TIME {
                    state.z2.set_constraint(clock_idx, 0, beta);
                } else {
                    state.z2.set_constraint(clock_idx, 0, INF_TIME);
                }
            } else {
                if alpha > 0 {
                    state.z1.set_constraint(0, clock_idx, -alpha);
                } else {
                    state.z1.set_constraint(0, clock_idx, 0);
                }
                if beta != INF_TIME {
                    state.z1.set_constraint(clock_idx, 0, beta);
                } else {
                    state.z1.set_constraint(clock_idx, 0, INF_TIME);
                }
            }
        }

        self.recompute_suspension(state);
    }

    fn recompute_suspension(&self, state: &mut StateClass) {
        let enabled_vec: Vec<usize> = state.enabled.iter().copied().collect();
        let mut suspended = HashSet::new();

        for &t in &state.enabled {
            if self.is_suspended(t, &enabled_vec) {
                suspended.insert(t);
                if !state.suspended.contains(&t) {
                    let clock_idx = t + 1;
                    state.z1.copy_clock_constraints(clock_idx, &mut state.z2);
                    state.z1.freeze_clock(clock_idx);
                    state.z2.freeze_clock(clock_idx);
                }
            } else if state.suspended.contains(&t) {
                let clock_idx = t + 1;
                state.z2.copy_clock_constraints(clock_idx, &mut state.z1);
                state.z1.unfreeze_clock(clock_idx);
                state.z2.unfreeze_clock(clock_idx);
            }
        }

        state.suspended = suspended;
    }

    fn is_suspended(&self, trans_idx: usize, enabled: &[usize]) -> bool {
        let trans = match self.ptpn.get_transition(trans_idx) {
            Some(t) => t,
            None => return false,
        };
        if !trans.suspendable {
            return false;
        }

        let trans_core = trans.core;
        let trans_priority = trans.priority;

        for &other_t in enabled {
            if other_t == trans_idx {
                continue;
            }
            if let Some(other) = self.ptpn.get_transition(other_t) {
                if other.core == trans_core
                    && !other.suspendable
                    && other.priority > trans_priority
                {
                    return true;
                }
            }
        }
        false
    }

    fn canonicalize(&self, state: &StateClass) -> StateClass {
        let mut canonical = state.copy();
        canonical.z1.minimize();
        canonical.z2.minimize();
        canonical
    }

    fn find_or_add_vertex(&mut self, state: &StateClass) -> petgraph::graph::NodeIndex {
        let key = StateKey::from(state);
        if let Some(&v) = self.state_to_vertex.get(&key) {
            return v;
        }
        let v = self.graph.add_node(state.copy());
        self.state_to_vertex.insert(key, v);
        v
    }

    pub fn save_to_dot(&self, path: impl AsRef<Path>) -> std::io::Result<()> {
        let mut dot = String::from("digraph StateClassGraph {\n");

        for (idx, node) in self.graph.node_weights().enumerate() {
            let label = format!(
                "S{} [M={:?}]",
                node.state_id,
                node.marking
            );
            dot.push_str(&format!("  n{} [label=\"{}\"];\n", idx, label.replace('"', "'")));
        }

        for edge in self.graph.edge_references() {
            let from = edge.source().index();
            let to = edge.target().index();
            let w = edge.weight();
            dot.push_str(&format!(
                "  n{} -> n{} [label=\"T{}@{:.1}\"];\n",
                from, to, w.transition_id, w.firing_time
            ));
        }

        dot.push_str("}\n");
        fs::write(path, dot)
    }

    pub fn save_to_json(&self, path: impl AsRef<Path>) -> std::io::Result<()> {
        let mut nodes = Vec::new();
        for (idx, node) in self.graph.node_weights().enumerate() {
            nodes.push(serde_json::json!({
                "id": idx,
                "state_id": node.state_id,
                "marking": node.marking,
                "cumulative_time": node.cumulative_time,
            }));
        }

        let mut edges = Vec::new();
        for edge in self.graph.edge_references() {
            edges.push(serde_json::json!({
                "from": edge.source().index(),
                "to": edge.target().index(),
                "transition_id": edge.weight().transition_id,
                "firing_time": edge.weight().firing_time,
            }));
        }

        let output = serde_json::json!({
            "nodes": nodes,
            "edges": edges,
            "stats": {
                "total_states": self.stats.total_states,
                "total_transitions": self.stats.total_transitions,
            }
        });

        fs::write(path, serde_json::to_string_pretty(&output)?)
    }
}
