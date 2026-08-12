//! Performance metrics derived from the reachability graph (port of
//! `src/analysis/metrics.cpp`).

use unipn::analysis::timed::INF_TIME;
use unipn::analysis::timed::StateClassGraph;
use unipn::analysis::timed::contains;
use crate::petri::PTPN;
use std::collections::{BTreeSet, HashMap};

/// A possibly-unbounded non-negative time value.
#[derive(Debug, Clone, Default)]
pub struct TimeValue {
    pub value: i64,
    pub infinite: bool,
}

impl TimeValue {
    pub fn to_string(&self) -> String {
        if self.infinite {
            "inf".to_string()
        } else {
            self.value.to_string()
        }
    }
}

/// Per-task performance metrics.
#[derive(Debug, Clone, Default)]
pub struct TaskMetrics {
    pub name: String,
    pub core: i32,
    pub priority: i32,
    pub wcet: i32,
    pub bcet: i32,
    pub period: i32,
    pub deadline: i32,
    pub observed: bool,
    pub activations: i32,
    pub wcrt: TimeValue,
    pub bcrt: TimeValue,
    pub jitter: TimeValue,
    pub worst_interference: TimeValue,
    pub worst_blocking: TimeValue,
    pub max_preemptions: i32,
    pub max_in_flight: i32,
    pub has_deadline: bool,
    pub slack: TimeValue,
    pub deadline_missed: bool,
    pub jobs_per_hyperperiod: i32,
}

#[derive(Debug, Clone, Default)]
pub struct LockMetrics {
    pub name: String,
    pub worst_hold: TimeValue,
    pub total_hold: TimeValue,
    pub total_wait: TimeValue,
}

#[derive(Debug, Clone, Default)]
pub struct CoreMetrics {
    pub core: i32,
    pub util_min: f64,
    pub util_max: f64,
    pub graph_busy_fraction: f64,
}

#[derive(Debug, Clone, Default)]
pub struct MetricsReport {
    pub exact: bool,
    pub states: usize,
    pub transitions: usize,
    pub truncated: bool,
    pub bounded: bool,
    pub max_tokens_per_place: Vec<usize>,
    pub overflow_places: Vec<String>,
    pub deadlock_states: Vec<usize>,
    pub schedulable: bool,
    pub deadline_miss_witness: Vec<usize>,
    pub tasks: Vec<TaskMetrics>,
    pub locks: Vec<LockMetrics>,
    pub cores: Vec<CoreMetrics>,
    pub has_steady_cycle: bool,
    pub recurrent_scc_size: usize,
    pub hyperperiod: i64,
}

fn is_inf(v: i32) -> bool {
    v == INF_TIME
}

fn lcm_ll(a: i64, b: i64) -> i64 {
    if a == 0 || b == 0 {
        return 0;
    }
    let g = gcd_ll(a, b);
    (a / g) * b
}

fn gcd_ll(a: i64, b: i64) -> i64 {
    let mut a = a.abs();
    let mut b = b.abs();
    while b != 0 {
        let t = a % b;
        a = b;
        b = t;
    }
    a
}

#[derive(Debug, Clone, Default)]
struct DPResult {
    reachable: bool,
    infinite: bool,
    value: i64,
}

fn combine_max(a: &DPResult, b: &DPResult) -> DPResult {
    if !a.reachable {
        return b.clone();
    }
    if !b.reachable {
        return a.clone();
    }
    let mut r = DPResult::default();
    r.reachable = true;
    if a.infinite || b.infinite {
        r.infinite = true;
    } else {
        r.value = a.value.max(b.value);
    }
    r
}

fn combine_min(a: &DPResult, b: &DPResult) -> DPResult {
    if !a.reachable {
        return b.clone();
    }
    if !b.reachable {
        return a.clone();
    }
    let mut r = DPResult::default();
    r.reachable = true;
    if a.infinite && b.infinite {
        r.infinite = true;
    } else if a.infinite {
        r = b.clone();
    } else if b.infinite {
        r = a.clone();
    } else {
        r.value = a.value.min(b.value);
    }
    r
}

fn to_time(r: &DPResult) -> TimeValue {
    let mut t = TimeValue::default();
    t.infinite = r.infinite;
    t.value = r.value;
    t
}

/// Flattened view of one task's static topology in the net.
struct TaskTopology {
    name: String,
    core: i32,
    priority: i32,
    chain_places: Vec<usize>,
    chain_transitions: Vec<usize>,
    exec_transitions: Vec<usize>,
    entry_place: usize,
    end_place: usize,
    has_end: bool,
    timeout_place: usize,
    has_timeout: bool,
}

#[derive(Clone)]
struct Edge {
    target: usize,
    #[allow(dead_code)]
    transition_id: usize,
    dwell_min: i32,
    dwell_max: i32,
}

pub struct MetricsAnalyzer<'a> {
    graph: &'a StateClassGraph,
    net: &'a PTPN,
    initial: usize,
    exact: bool,
    num_vertices: usize,
    out_edges: Vec<Vec<Edge>>,
    state_of: Vec<usize>,
    tasks: Vec<TaskTopology>,
    transition_task: Vec<i32>,
    transition_is_exec: Vec<bool>,
    place_lock: Vec<String>,
}

impl<'a> MetricsAnalyzer<'a> {
    pub fn new(graph: &'a StateClassGraph, net: &'a PTPN, initial: usize, exact: bool) -> Self {
        let mut analyzer = MetricsAnalyzer {
            graph,
            net,
            initial,
            exact,
            num_vertices: 0,
            out_edges: Vec::new(),
            state_of: Vec::new(),
            tasks: Vec::new(),
            transition_task: Vec::new(),
            transition_is_exec: Vec::new(),
            place_lock: Vec::new(),
        };
        analyzer.flatten_graph();
        analyzer.build_topology();
        analyzer
    }

    fn flatten_graph(&mut self) {
        self.num_vertices = self.graph.states.len();
        self.out_edges = vec![Vec::new(); self.num_vertices];
        self.state_of = Vec::with_capacity(self.num_vertices);
        for idx in 0..self.num_vertices {
            self.state_of.push(self.graph.states[idx].id);
        }

        for &(source, target, ref fe) in &self.graph.edges {
            let sid = self.graph.states[source].id;
            let tid = self.graph.states[target].id;
            self.out_edges[sid].push(Edge {
                target: tid,
                transition_id: fe.transition_id,
                dwell_min: fe.dwell_min,
                dwell_max: fe.dwell_max,
            });
        }
    }

    fn build_topology(&mut self) {
        self.transition_task = vec![-1; self.net.num_transitions()];
        self.transition_is_exec = vec![false; self.net.num_transitions()];
        self.place_lock = vec![String::new(); self.net.num_places()];
        for (lock_name, &place_idx) in &self.net.locks_place {
            if place_idx < self.place_lock.len() {
                self.place_lock[place_idx] = lock_name.clone();
            }
        }

        let mut place_by_name: HashMap<String, usize> = HashMap::new();
        for (p, place) in self.net.net.places.iter().enumerate() {
            place_by_name.insert(place.name.clone(), p);
        }

        let mut tasks: Vec<TaskTopology> = Vec::new();
        let mut names: Vec<&String> = self.net.task_info.keys().collect();
        names.sort();
        for name in names {
            let info = &self.net.task_info[name];
            let Some(chain) = self.net.node_pn_map.get(name) else {
                continue;
            };
            if chain.is_empty() {
                continue;
            }

            let mut topo = TaskTopology {
                name: name.clone(),
                core: info.core,
                priority: info.priority,
                chain_places: Vec::new(),
                chain_transitions: Vec::new(),
                exec_transitions: Vec::new(),
                entry_place: chain[0],
                end_place: *chain.last().unwrap(),
                has_end: true,
                timeout_place: 0,
                has_timeout: false,
            };

            let task_index = tasks.len();
            for (i, &t) in chain.iter().enumerate() {
                if i % 2 == 0 {
                    topo.chain_places.push(t);
                } else {
                    topo.chain_transitions.push(t);
                    if t < self.transition_task.len() {
                        self.transition_task[t] = task_index as i32;
                    }
                    if t < self.net.num_transitions()
                        && self.net.get_transition(t).name.contains("exec")
                    {
                        topo.exec_transitions.push(t);
                        if t < self.transition_is_exec.len() {
                            self.transition_is_exec[t] = true;
                        }
                    }
                }
            }

            if let Some(&to_place) = place_by_name.get(&format!("{}timeout", name)) {
                topo.timeout_place = to_place;
                topo.has_timeout = true;
            }

            tasks.push(topo);
        }
        self.tasks = tasks;
    }

    fn task_in_flight(&self, task: &TaskTopology, v: usize) -> bool {
        let m = &self.graph.states[v].marking;
        task.chain_places.iter().any(|&p| p < m.len() && m[p] > 0)
    }

    fn task_active(&self, task: &TaskTopology, v: usize) -> bool {
        let active = &self.graph.states[v].priority_enabled;
        task.chain_transitions.iter().any(|&t| contains(active, t))
    }

    fn task_suspended(&self, task: &TaskTopology, v: usize) -> bool {
        let susp = &self.graph.states[v].suspended;
        task.exec_transitions.iter().any(|&t| contains(susp, t))
    }

    fn core_busy(&self, core: i32, v: usize) -> bool {
        if core < 0 {
            return false;
        }
        let state = &self.graph.states[v];
        state.priority_enabled.iter().any(|&t| {
            t < self.transition_is_exec.len()
                && self.transition_is_exec[t]
                && self.net.get_transition(t).kind.core == core
        })
    }

    fn task_blocked_by_lower(&self, task: &TaskTopology, v: usize) -> bool {
        if task.core < 0 {
            return false;
        }
        if !self.task_in_flight(task, v) || self.task_active(task, v) {
            return false;
        }
        let state = &self.graph.states[v];
        state.priority_enabled.iter().any(|&t| {
            t < self.transition_is_exec.len()
                && self.transition_is_exec[t]
                && self.net.get_transition(t).kind.core == task.core
                && self.net.get_transition(t).kind.priority < task.priority
        })
    }

    fn compute_structural(&self, report: &mut MetricsReport) {
        report.max_tokens_per_place = vec![0; self.net.num_places()];
        for v in 0..self.num_vertices {
            let m = &self.graph.states[v].marking;
            for (p, &tokens) in m.0.iter().enumerate() {
                if p < report.max_tokens_per_place.len() {
                    report.max_tokens_per_place[p] = report.max_tokens_per_place[p].max(tokens);
                }
            }
        }

        report.bounded = true;
        for p in crate::petri::overflowed_places() {
            if p < self.net.num_places() {
                report.bounded = false;
                report.overflow_places.push(self.net.get_place(p).name.clone());
            }
        }

        // Illegitimate sinks: no successor while work remains.
        for v in 0..self.num_vertices {
            if !self.out_edges[v].is_empty() {
                continue;
            }
            let mut work_remaining = false;
            for task in &self.tasks {
                if self.task_in_flight(task, v) {
                    work_remaining = true;
                    break;
                }
                if task.has_timeout
                    && self.graph.states[v].marking[task.timeout_place] > 0
                {
                    work_remaining = true;
                    break;
                }
            }
            if work_remaining {
                report.deadlock_states.push(v);
            }
        }
    }

    fn compute_schedulability(&self, report: &mut MetricsReport) {
        let mut parent: Vec<i64> = vec![-1; self.num_vertices];
        let mut seen: Vec<bool> = vec![false; self.num_vertices];
        let mut queue: std::collections::VecDeque<usize> = std::collections::VecDeque::new();
        queue.push_back(self.initial);
        seen[self.initial] = true;
        let mut miss_state: i64 = -1;

        let is_miss = |v: usize| -> bool {
            self.tasks.iter().any(|task| {
                task.has_timeout && self.graph.states[v].marking[task.timeout_place] > 0
            })
        };

        if is_miss(self.initial) {
            miss_state = self.initial as i64;
        }
        while !queue.is_empty() && miss_state < 0 {
            let u = queue.pop_front().unwrap();
            for e in &self.out_edges[u] {
                if seen[e.target] {
                    continue;
                }
                seen[e.target] = true;
                parent[e.target] = u as i64;
                if is_miss(e.target) {
                    miss_state = e.target as i64;
                    break;
                }
                queue.push_back(e.target);
            }
        }

        if miss_state >= 0 {
            let mut path: Vec<usize> = Vec::new();
            let mut v = miss_state;
            while v >= 0 {
                path.push(v as usize);
                v = parent[v as usize];
            }
            path.reverse();
            report.deadline_miss_witness = path;
        }

        report.schedulable = (miss_state < 0) && report.deadlock_states.is_empty();
    }

    fn compute_task_timing(&self, report: &mut MetricsReport) {
        // Generic DP: accumulate weight along in-flight paths of a task.
        let run_dp = |task: &TaskTopology,
                      weight: &dyn Fn(usize, &Edge) -> i32,
                      maximize: bool,
                      starts: &[usize]|
         -> DPResult {
            let n = self.num_vertices;
            let mut memo: Vec<DPResult> = vec![DPResult::default(); n];
            let mut color: Vec<u8> = vec![0; n]; // 0 white, 1 gray, 2 black

            fn dfs(
                v: usize,
                task: &TaskTopology,
                out_edges: &[Vec<Edge>],
                state_of: &StateClassGraph,
                color: &mut [u8],
                memo: &mut [DPResult],
                weight: &dyn Fn(usize, &Edge) -> i32,
                maximize: bool,
            ) -> DPResult {
                if color[v] == 2 {
                    return memo[v].clone();
                }
                if color[v] == 1 {
                    let mut r = DPResult::default();
                    if maximize {
                        r.reachable = true;
                        r.infinite = true; // cycle => unbounded
                    }
                    return r;
                }
                color[v] = 1;
                let mut best = DPResult::default();
                for e in &out_edges[v] {
                    let w = weight(v, e);
                    let completes = task.has_end
                        && state_of.states[e.target].marking[task.end_place]
                            > state_of.states[v].marking[task.end_place];
                    if completes {
                        let mut cand = DPResult::default();
                        cand.reachable = true;
                        if is_inf(w) {
                            cand.infinite = true;
                        } else {
                            cand.value = w as i64;
                        }
                        best = if maximize {
                            combine_max(&best, &cand)
                        } else {
                            combine_min(&best, &cand)
                        };
                    } else if task_in_flight_of(task, &state_of, e.target) {
                        let sub = dfs(
                            e.target,
                            task,
                            out_edges,
                            state_of,
                            color,
                            memo,
                            weight,
                            maximize,
                        );
                        if !sub.reachable {
                            continue;
                        }
                        let mut cand = DPResult::default();
                        cand.reachable = true;
                        if is_inf(w) || sub.infinite {
                            cand.infinite = true;
                        } else {
                            cand.value = w as i64 + sub.value;
                        }
                        best = if maximize {
                            combine_max(&best, &cand)
                        } else {
                            combine_min(&best, &cand)
                        };
                    }
                }
                color[v] = 2;
                memo[v] = best.clone();
                best
            }

            let mut overall = DPResult::default();
            for &s in starts {
                let r = dfs(
                    s,
                    task,
                    &self.out_edges,
                    self.graph,
                    &mut color,
                    &mut memo,
                    weight,
                    maximize,
                );
                if !r.reachable {
                    continue;
                }
                overall = if maximize {
                    combine_max(&overall, &r)
                } else {
                    combine_min(&overall, &r)
                };
            }
            overall
        };

        for task in &self.tasks {
            let mut tm = TaskMetrics {
                name: task.name.clone(),
                core: task.core,
                priority: task.priority,
                ..Default::default()
            };
            if let Some(info) = self.net.task_info.get(&task.name) {
                tm.wcet = info.wcet;
                tm.bcet = info.bcet;
                tm.period = info.period;
                tm.deadline = info.deadline;
                tm.has_deadline = info.deadline > 0;
            }

            // Release states: edges that deposit a token into the entry place.
            let mut uniq: BTreeSet<usize> = BTreeSet::new();
            if self.graph.states[self.initial].marking[task.entry_place] > 0 {
                uniq.insert(self.initial);
                tm.activations += 1;
            }
            for v in 0..self.num_vertices {
                for e in &self.out_edges[v] {
                    if self.graph.states[e.target].marking[task.entry_place]
                        > self.graph.states[v].marking[task.entry_place]
                    {
                        uniq.insert(e.target);
                        tm.activations += 1;
                    }
                }
            }
            let release_states: Vec<usize> = uniq.iter().copied().collect();
            tm.observed = !release_states.is_empty();

            for v in 0..self.num_vertices {
                let m = &self.graph.states[v].marking;
                let sum: i32 = task.chain_places.iter().map(|&p| m[p] as i32).sum();
                tm.max_in_flight = tm.max_in_flight.max(sum);
            }

            if task.has_timeout {
                for v in 0..self.num_vertices {
                    if self.graph.states[v].marking[task.timeout_place] > 0 {
                        tm.deadline_missed = true;
                        break;
                    }
                }
            }

            if tm.observed {
                let w_dwell_max = |_: usize, e: &Edge| e.dwell_max;
                let w_dwell_min = |_: usize, e: &Edge| e.dwell_min;
                let w_interf = |v: usize, e: &Edge| {
                    if self.task_suspended(task, v) {
                        e.dwell_max
                    } else {
                        0
                    }
                };
                let w_block = |v: usize, e: &Edge| {
                    if self.task_blocked_by_lower(task, v) {
                        e.dwell_max
                    } else {
                        0
                    }
                };
                let w_preempt = |v: usize, e: &Edge| {
                    if self.task_active(task, v) && self.task_suspended(task, e.target) {
                        1
                    } else {
                        0
                    }
                };

                tm.wcrt = to_time(&run_dp(task, &w_dwell_max, true, &release_states));
                tm.bcrt = to_time(&run_dp(task, &w_dwell_min, false, &release_states));
                if !tm.bcrt.infinite && tm.bcet > tm.bcrt.value as i32 {
                    tm.bcrt.value = tm.bcet as i64;
                }
                tm.jitter.infinite = tm.wcrt.infinite;
                if !tm.wcrt.infinite && !tm.bcrt.infinite {
                    tm.jitter.value = 0.max(tm.wcrt.value - tm.bcrt.value);
                }
                tm.worst_interference = to_time(&run_dp(task, &w_interf, true, &release_states));
                tm.worst_blocking = to_time(&run_dp(task, &w_block, true, &release_states));
                let preempt = run_dp(task, &w_preempt, true, &release_states);
                tm.max_preemptions = if preempt.infinite {
                    -1
                } else {
                    preempt.value as i32
                };

                if tm.has_deadline {
                    tm.slack.infinite = tm.wcrt.infinite;
                    if !tm.wcrt.infinite {
                        tm.slack.value = tm.deadline as i64 - tm.wcrt.value;
                    }
                }
            }

            report.tasks.push(tm);
        }
    }

    fn compute_locks(&self, report: &mut MetricsReport) {
        let mut lock_names: Vec<&String> = self.net.locks_place.keys().collect();
        lock_names.sort();
        for lock_name in lock_names {
            let &lock_place = self.net.locks_place.get(lock_name).unwrap();
            let mut lm = LockMetrics {
                name: lock_name.clone(),
                ..Default::default()
            };
            let mut total_hold: i64 = 0;
            let mut total_wait: i64 = 0;
            let mut hold_inf = false;
            let mut wait_inf = false;

            for v in 0..self.num_vertices {
                if lock_place >= self.graph.states[v].marking.len() {
                    continue;
                }
                let held = self.graph.states[v].marking[lock_place] == 0;
                if !held {
                    continue;
                }
                let mut state_dwell: i32 = 0;
                for e in &self.out_edges[v] {
                    if is_inf(e.dwell_max) {
                        state_dwell = INF_TIME;
                        break;
                    }
                    state_dwell = state_dwell.max(e.dwell_max);
                }
                if is_inf(state_dwell) {
                    lm.worst_hold.infinite = true;
                    hold_inf = true;
                } else {
                    lm.worst_hold.value = lm.worst_hold.value.max(state_dwell as i64);
                    total_hold += state_dwell as i64;
                }

                let mut contended = false;
                for task in &self.tasks {
                    let Some(info) = self.net.task_info.get(&task.name) else {
                        continue;
                    };
                    if !info.locks.iter().any(|l| l == lock_name) {
                        continue;
                    }
                    if self.task_in_flight(task, v) && !self.task_active(task, v) {
                        contended = true;
                        break;
                    }
                }
                if contended && !is_inf(state_dwell) {
                    total_wait += state_dwell as i64;
                } else if contended {
                    wait_inf = true;
                }
            }

            lm.total_hold.infinite = hold_inf;
            lm.total_hold.value = total_hold;
            lm.total_wait.infinite = wait_inf;
            lm.total_wait.value = total_wait;
            report.locks.push(lm);
        }
    }

    fn compute_utilisation(&self, report: &mut MetricsReport) {
        // Hyperperiod = lcm of task periods.
        let mut hyper: i64 = 0;
        for info in self.net.task_info.values() {
            if info.period > 0 {
                hyper = if hyper == 0 {
                    info.period as i64
                } else {
                    lcm_ll(hyper, info.period as i64)
                };
            }
        }
        report.hyperperiod = hyper;
        for tm in report.tasks.iter_mut() {
            if tm.period > 0 && hyper > 0 {
                tm.jobs_per_hyperperiod = (hyper / tm.period as i64) as i32;
            }
        }

        // Tarjan SCC (iterative) to find the recurrent steady region.
        let n = self.num_vertices;
        let mut index: Vec<i64> = vec![-1; n];
        let mut lowlink: Vec<i64> = vec![0; n];
        let mut on_stack: Vec<bool> = vec![false; n];
        let mut scc_id: Vec<i64> = vec![-1; n];
        let mut stack: Vec<usize> = Vec::new();
        let mut next_index: i64 = 0;
        let mut scc_count: i64 = 0;

        // Explicit DFS stack of (vertex, next-edge-offset) frames.
        let mut dfs_stack: Vec<(usize, usize)> = Vec::new();
        for start in 0..n {
            if index[start] >= 0 {
                continue;
            }
            index[start] = next_index;
            lowlink[start] = next_index;
            next_index += 1;
            stack.push(start);
            on_stack[start] = true;
            dfs_stack.push((start, 0));

            while let Some((v, epos)) = dfs_stack.last().copied() {
                if epos < self.out_edges[v].len() {
                    let target = self.out_edges[v][epos].target;
                    dfs_stack.last_mut().unwrap().1 += 1;
                    if index[target] < 0 {
                        index[target] = next_index;
                        lowlink[target] = next_index;
                        next_index += 1;
                        stack.push(target);
                        on_stack[target] = true;
                        dfs_stack.push((target, 0));
                    } else if on_stack[target] {
                        lowlink[v] = lowlink[v].min(index[target]);
                    }
                } else {
                    dfs_stack.pop();
                    if lowlink[v] == index[v] {
                        loop {
                            let w = stack.pop().unwrap();
                            on_stack[w] = false;
                            scc_id[w] = scc_count;
                            if w == v {
                                break;
                            }
                        }
                        scc_count += 1;
                    }
                    if let Some((parent, _)) = dfs_stack.last().copied() {
                        lowlink[parent] = lowlink[parent].min(lowlink[v]);
                    }
                }
            }
        }

        let mut scc_size: Vec<usize> = vec![0; scc_count as usize];
        for v in 0..n {
            scc_size[scc_id[v] as usize] += 1;
        }
        let mut has_self: Vec<bool> = vec![false; scc_count as usize];
        for v in 0..n {
            for e in &self.out_edges[v] {
                if scc_id[e.target] == scc_id[v] {
                    has_self[scc_id[v] as usize] = true;
                }
            }
        }
        let mut best_scc: i64 = -1;
        for c in 0..scc_count {
            let recurrent = scc_size[c as usize] > 1 || has_self[c as usize];
            if !recurrent {
                continue;
            }
            if best_scc < 0 || scc_size[c as usize] > scc_size[best_scc as usize] {
                best_scc = c;
            }
        }
        report.has_steady_cycle = best_scc >= 0;
        report.recurrent_scc_size = if best_scc >= 0 {
            scc_size[best_scc as usize]
        } else {
            0
        };

        let mut region: Vec<usize> = Vec::new();
        for v in 0..n {
            if best_scc < 0 || scc_id[v] == best_scc {
                region.push(v);
            }
        }

        let mut cores: BTreeSet<i32> = BTreeSet::new();
        for info in self.net.task_info.values() {
            if info.core >= 0 {
                cores.insert(info.core);
            }
        }

        for core in cores {
            let mut cm = CoreMetrics {
                core,
                ..Default::default()
            };
            let mut umin = 0.0;
            let mut umax = 0.0;
            for info in self.net.task_info.values() {
                if info.core != core || info.period <= 0 {
                    continue;
                }
                umin += info.bcet as f64 / info.period as f64;
                umax += info.wcet as f64 / info.period as f64;
            }
            cm.util_min = umin;
            cm.util_max = umax;

            let mut busy = 0.0;
            let mut total = 0.0;
            for &v in &region {
                let busy_here = self.core_busy(core, v);
                for e in &self.out_edges[v] {
                    let mid = if is_inf(e.dwell_max) {
                        e.dwell_min as f64
                    } else {
                        0.5 * (e.dwell_min as f64 + e.dwell_max as f64)
                    };
                    total += mid;
                    if busy_here {
                        busy += mid;
                    }
                }
            }
            cm.graph_busy_fraction = if total > 0.0 { busy / total } else { 0.0 };
            report.cores.push(cm);
        }
        report.cores.sort_by_key(|c| c.core);
    }

    pub fn analyze(&self) -> MetricsReport {
        let mut report = MetricsReport::default();
        report.exact = self.exact;
        report.states = self.num_vertices;
        report.transitions = self.out_edges.iter().map(|e| e.len()).sum();

        self.compute_structural(&mut report);
        self.compute_schedulability(&mut report);
        self.compute_task_timing(&mut report);
        self.compute_locks(&mut report);
        self.compute_utilisation(&mut report);
        report
    }

    pub fn save_to_json(report: &MetricsReport, file_path: &str) -> bool {
        let time_json = |t: &TimeValue| -> String {
            if t.infinite {
                "null".to_string()
            } else {
                t.value.to_string()
            }
        };

        let mut out = String::new();
        out.push_str("{\n");
        out.push_str(&format!("  \"exact\": {},\n", if report.exact { "true" } else { "false" }));
        out.push_str(&format!("  \"states\": {},\n", report.states));
        out.push_str(&format!("  \"transitions\": {},\n", report.transitions));
        out.push_str(&format!("  \"bounded\": {},\n", if report.bounded { "true" } else { "false" }));
        out.push_str(&format!(
            "  \"schedulable\": {},\n",
            if report.schedulable { "true" } else { "false" }
        ));
        out.push_str(&format!(
            "  \"has_steady_cycle\": {},\n",
            if report.has_steady_cycle { "true" } else { "false" }
        ));
        out.push_str(&format!(
            "  \"recurrent_scc_size\": {},\n",
            report.recurrent_scc_size
        ));
        out.push_str(&format!("  \"hyperperiod\": {},\n", report.hyperperiod));

        out.push_str("  \"deadlock_states\": [");
        for (i, v) in report.deadlock_states.iter().enumerate() {
            if i > 0 {
                out.push_str(", ");
            }
            out.push_str(&v.to_string());
        }
        out.push_str("],\n");

        out.push_str("  \"deadline_miss_witness\": [");
        for (i, v) in report.deadline_miss_witness.iter().enumerate() {
            if i > 0 {
                out.push_str(", ");
            }
            out.push_str(&v.to_string());
        }
        out.push_str("],\n");

        out.push_str("  \"tasks\": [\n");
        for (i, t) in report.tasks.iter().enumerate() {
            out.push_str("    {\n");
            out.push_str(&format!("      \"name\": \"{}\",\n", t.name));
            out.push_str(&format!("      \"core\": {},\n", t.core));
            out.push_str(&format!("      \"priority\": {},\n", t.priority));
            out.push_str(&format!("      \"wcet\": {},\n", t.wcet));
            out.push_str(&format!("      \"bcet\": {},\n", t.bcet));
            out.push_str(&format!("      \"period\": {},\n", t.period));
            out.push_str(&format!("      \"deadline\": {},\n", t.deadline));
            out.push_str(&format!("      \"observed\": {},\n", if t.observed { "true" } else { "false" }));
            out.push_str(&format!("      \"activations\": {},\n", t.activations));
            out.push_str(&format!("      \"wcrt\": {},\n", time_json(&t.wcrt)));
            out.push_str(&format!("      \"bcrt\": {},\n", time_json(&t.bcrt)));
            out.push_str(&format!("      \"jitter\": {},\n", time_json(&t.jitter)));
            out.push_str(&format!(
                "      \"worst_interference\": {},\n",
                time_json(&t.worst_interference)
            ));
            out.push_str(&format!(
                "      \"worst_blocking\": {},\n",
                time_json(&t.worst_blocking)
            ));
            out.push_str(&format!("      \"max_preemptions\": {},\n", t.max_preemptions));
            out.push_str(&format!("      \"max_in_flight\": {},\n", t.max_in_flight));
            out.push_str(&format!(
                "      \"slack\": {},\n",
                if t.has_deadline {
                    time_json(&t.slack)
                } else {
                    "null".to_string()
                }
            ));
            out.push_str(&format!(
                "      \"deadline_missed\": {},\n",
                if t.deadline_missed { "true" } else { "false" }
            ));
            out.push_str(&format!("      \"jobs_per_hyperperiod\": {}\n", t.jobs_per_hyperperiod));
            out.push_str("    }");
            if i + 1 < report.tasks.len() {
                out.push(',');
            }
            out.push('\n');
        }
        out.push_str("  ],\n");

        out.push_str("  \"locks\": [\n");
        for (i, l) in report.locks.iter().enumerate() {
            out.push_str("    {\n");
            out.push_str(&format!("      \"name\": \"{}\",\n", l.name));
            out.push_str(&format!("      \"worst_hold\": {},\n", time_json(&l.worst_hold)));
            out.push_str(&format!("      \"total_hold\": {},\n", time_json(&l.total_hold)));
            out.push_str(&format!("      \"total_wait\": {}\n", time_json(&l.total_wait)));
            out.push_str("    }");
            if i + 1 < report.locks.len() {
                out.push(',');
            }
            out.push('\n');
        }
        out.push_str("  ],\n");

        out.push_str("  \"cores\": [\n");
        for (i, c) in report.cores.iter().enumerate() {
            out.push_str("    {\n");
            out.push_str(&format!("      \"core\": {},\n", c.core));
            out.push_str(&format!("      \"util_min\": {},\n", c.util_min));
            out.push_str(&format!("      \"util_max\": {},\n", c.util_max));
            out.push_str(&format!("      \"graph_busy_fraction\": {}\n", c.graph_busy_fraction));
            out.push_str("    }");
            if i + 1 < report.cores.len() {
                out.push(',');
            }
            out.push('\n');
        }
        out.push_str("  ]\n");
        out.push_str("}\n");

        match std::fs::write(file_path, out) {
            Ok(_) => true,
            Err(_) => false,
        }
    }
}

// Free helper so the nested `dfs` closure can reference it without borrowing self.
fn task_in_flight_of(task: &TaskTopology, graph: &StateClassGraph, v: usize) -> bool {
    let m = &graph.states[v].marking;
    task.chain_places.iter().any(|&p| p < m.len() && m[p] > 0)
}
