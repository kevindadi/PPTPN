//! TDG -> PTPN lowering (port of `src/tdg2pn/tdg2pn.cpp`).
//!
//! Five ordered stages:
//!   [1] vertices  every TDG node -> a place/transition fragment
//!   [2] edges     TDG edges wire the fragments together
//!   [3] bindings  start/periodic/end initial tokens, periodic releases, sink consumers
//!   [4] scheduling preemption model (resume: engine-level filter; restart: structural)
//!   [5] resources + metadata: core/lock resource places, task_info

use crate::petri::{CONTROL_TRANSITION_CORE, INF, PTPN, TimeInterval};
use crate::tdg::TDG;
use crate::types::{NodeType, SchedulePolicy, TaskConfig, TdgEdge};

const CONTROL_TRANSITION_PRIORITY: i32 = 0;

/// Indices into the per-task place/transition chain stored in node_pn_map.
struct TaskChainLayout;

impl TaskChainLayout {
    #[allow(dead_code)]
    const K_ENTRY: usize = 0;
    const K_GET_CORE: usize = 1;
    #[allow(dead_code)]
    const K_READY: usize = 2;
    #[allow(dead_code)]
    const K_FIRST_EXEC: usize = 3;
    #[allow(dead_code)]
    const K_FIRST_SEG_DONE: usize = 4;
    const K_MIN_LENGTH: usize = 5;

    fn lock_acquire_transition(lock_index: usize) -> usize {
        5 + 4 * lock_index
    }

    fn lock_release_transition(chain_length: usize, lock_index: usize) -> usize {
        chain_length - 4 - 2 * lock_index
    }
}

fn encode_task_execution_priority(task_priority: i32) -> i32 {
    task_priority
}

/// The resume policy (and the legacy "fixed" alias) no longer relies on the
/// structural CPU-resource place or the preempt/suspended/resume sub-net.
fn is_resume_policy(policy: SchedulePolicy) -> bool {
    matches!(
        policy,
        SchedulePolicy::Fixed | SchedulePolicy::FixedPriorWithResume
    )
}

fn immediate_interval() -> TimeInterval {
    TimeInterval::closed(0, 0)
}

fn trim(text: &str) -> &str {
    text.trim_matches(|c: char| c == ' ' || c == '\t' || c == '\r' || c == '\n')
}

/// Parses a single time bound; accepts a non-negative integer or an unbounded
/// marker (inf / +inf / ∞ / *).
fn parse_time_bound(token: &str) -> Option<i32> {
    let value = trim(token);
    if value.is_empty() {
        return None;
    }
    match value {
        "inf" | "+inf" | "∞" | "*" => return Some(INF),
        _ => {}
    }
    match value.parse::<i64>() {
        Ok(v) if v >= 0 => Some(v as i32),
        _ => None,
    }
}

/// Converts a TDG edge label into the firing interval of the bridge transition.
/// "" -> [0,0]; "a" -> [a,a]; "a,b" -> [a,b]. Malformed labels fall back to [0,0].
fn parse_edge_interval(label: &str, source_name: &str, target_name: &str) -> TimeInterval {
    let mut body = trim(label).to_string();
    if !body.is_empty()
        && (body.starts_with('[') || body.starts_with('('))
        && (body.ends_with(']') || body.ends_with(')'))
    {
        body = trim(&body[1..body.len() - 1]).to_string();
    }
    if body.is_empty() {
        return immediate_interval();
    }

    let (earliest, latest) = match body.find(',') {
        None => {
            let e = parse_time_bound(&body);
            (e, e)
        }
        Some(comma) => (
            parse_time_bound(&body[..comma]),
            parse_time_bound(&body[comma + 1..]),
        ),
    };

    match (earliest, latest) {
        (Some(e), Some(l)) if e != INF && (l == INF || l >= e) => TimeInterval::closed(e, l),
        _ => {
            // Invalid label -> immediate with warning (logged by caller).
            let _ = (source_name, target_name);
            immediate_interval()
        }
    }
}

fn add_control_transition(ptpn: &mut PTPN, name: &str, interval: TimeInterval) -> usize {
    ptpn.add_transition(
        name,
        interval,
        CONTROL_TRANSITION_PRIORITY,
        CONTROL_TRANSITION_CORE,
        false,
    )
}

pub struct TDG2PN;

impl TDG2PN {
    pub fn has_non_self_successor(tdg: &TDG, task_name: &str) -> bool {
        tdg.tdg_edges
            .iter()
            .any(|edge: &TdgEdge| edge.leaves(task_name))
    }

    pub fn has_self_loop_release(tdg: &TDG, task_name: &str) -> bool {
        tdg.tdg_edges
            .iter()
            .any(|edge| edge.source == task_name && edge.is_self_loop())
    }

    fn add_consume_transition(ptpn: &mut PTPN, task_name: &str, end_idx: usize) {
        let consume = add_control_transition(
            ptpn,
            &format!("{}_consume", task_name),
            immediate_interval(),
        );
        ptpn.set_pre_arc(end_idx, consume, 1);
    }

    fn add_start_bindings(ptpn: &mut PTPN, tdg: &TDG) {
        for start_binding in &tdg.start_tasks {
            let Some(&(start, _end)) = ptpn.node_start_end_map.get(&start_binding.task) else {
                continue;
            };
            if start_binding.tokens <= 0 {
                continue;
            }
            ptpn.set_initial_marking(start, start_binding.tokens);
        }
    }

    fn add_end_consumers(ptpn: &mut PTPN, tdg: &TDG) {
        let mut consume_tasks: std::collections::HashSet<String> = std::collections::HashSet::new();

        for (vertex_name, node_type) in &tdg.nodes_type {
            if node_type.as_task().is_some() && !Self::has_non_self_successor(tdg, vertex_name) {
                consume_tasks.insert(vertex_name.clone());
            }
        }

        for end_task in &tdg.end_tasks {
            consume_tasks.insert(end_task.clone());
        }

        for task_name in &consume_tasks {
            let Some(&(_, end)) = ptpn.node_start_end_map.get(task_name) else {
                continue;
            };
            Self::add_consume_transition(ptpn, task_name, end);
        }
    }

    fn add_periodic_release_bindings(ptpn: &mut PTPN, tdg: &TDG) {
        for periodic_task in &tdg.periodic_tasks {
            if Self::has_self_loop_release(tdg, &periodic_task.task) {
                continue;
            }

            let Some(&(start, _end)) = ptpn.node_start_end_map.get(&periodic_task.task) else {
                continue;
            };
            let Some(node_type) = tdg.nodes_type.get(&periodic_task.task) else {
                continue;
            };
            if node_type.as_task().is_none() {
                continue;
            }

            let period_place = ptpn.add_place(&format!("{}_period", periodic_task.task), 1, false);
            let fire = add_control_transition(
                ptpn,
                &format!("{}_fire", periodic_task.task),
                TimeInterval::closed(periodic_task.period, periodic_task.period),
            );

            ptpn.set_initial_marking(period_place, 1);
            ptpn.set_pre_arc(period_place, fire, 1);
            ptpn.set_post_arc(fire, period_place, 1);
            ptpn.set_post_arc(fire, start, 1);
        }
    }

    pub fn transform(tdg: &TDG, ptpn: &mut PTPN) {
        ptpn.node_start_end_map.clear();
        ptpn.node_pn_map.clear();
        ptpn.cpus_place.clear();
        ptpn.core_parallelism.clear();
        ptpn.locks_place.clear();
        ptpn.task_info.clear();
        ptpn.node_index = 0;

        let mut tasks_config: std::collections::HashMap<String, TaskConfig> =
            std::collections::HashMap::new();
        for node in &tdg.all_task {
            if let Some(task) = node.as_task() {
                tasks_config.insert(
                    task.name.clone(),
                    TaskConfig {
                        core: task.core,
                        priority: task.priority,
                        times: task.time.clone(),
                        locks: task.lock.clone(),
                    },
                );
            }
        }

        Self::transform_vertices(ptpn, tdg);
        Self::transform_edges(ptpn, tdg);

        Self::add_start_bindings(ptpn, tdg);
        Self::add_periodic_release_bindings(ptpn, tdg);
        Self::add_end_consumers(ptpn, tdg);

        if is_resume_policy(tdg.policy) {
            // Preemption is expressed by the analysis engine; no sub-net.
        } else if tdg.policy == SchedulePolicy::FixedPriorWithRestart {
            let core_task = Self::classify_tdg_priority(tdg);
            Self::fixed_prior_with_restart(ptpn, &core_task, &tasks_config, &tdg.nodes_type);
        }

        Self::add_resources_and_bindings(ptpn, tdg);
        Self::populate_task_info(ptpn, tdg);

        let _ = ptpn.verify_structure();
    }

    pub fn populate_task_info(ptpn: &mut PTPN, tdg: &TDG) {
        let mut period_of: std::collections::HashMap<String, i32> =
            std::collections::HashMap::new();
        for binding in &tdg.periodic_tasks {
            period_of.insert(binding.task.clone(), binding.period);
        }
        for edge in &tdg.tdg_edges {
            if !edge.is_self_loop() || period_of.contains_key(&edge.source) {
                continue;
            }
            if let Ok(v) = edge.label.parse::<i32>() {
                period_of.insert(edge.source.clone(), v);
            }
        }

        for node in &tdg.all_task {
            let Some(task) = node.as_task() else {
                continue;
            };
            let mut info = crate::petri::TaskInfo {
                core: task.core,
                priority: task.priority,
                wcet: 0,
                bcet: 0,
                ..Default::default()
            };
            for (lo, hi) in &task.time {
                info.bcet += lo;
                info.wcet += hi;
            }
            info.period = period_of.get(&task.name).copied().unwrap_or(0);
            info.deadline = info.period; // implicit deadline = period
            info.locks = tdg
                .task_locks_map
                .get(&task.name)
                .cloned()
                .unwrap_or_else(|| task.lock.clone());
            ptpn.task_info.insert(task.name.clone(), info);
        }
    }

    pub fn classify_tdg_priority(tdg: &TDG) -> std::collections::HashMap<i32, Vec<String>> {
        let mut core_task: std::collections::HashMap<i32, Vec<String>> =
            std::collections::HashMap::new();

        for node in &tdg.all_task {
            if let Some(task) = node.as_task() {
                core_task
                    .entry(task.core)
                    .or_default()
                    .push(task.name.clone());
            }
        }

        for tasks in core_task.values_mut() {
            tasks.sort_by(|left, right| {
                let pl = tdg.tasks_priority.get(left).copied().unwrap_or(0);
                let pr = tdg.tasks_priority.get(right).copied().unwrap_or(0);
                pr.cmp(&pl)
            });
        }

        core_task
    }

    fn build_preempt_priorities(
        tasks: &[String],
        tc: &std::collections::HashMap<String, TaskConfig>,
        aggressor_priority: i32,
    ) -> std::collections::HashMap<String, i32> {
        let mut priorities = std::collections::HashMap::new();
        for task_name in tasks {
            let Some(cfg) = tc.get(task_name) else {
                continue;
            };
            if cfg.priority >= aggressor_priority {
                continue;
            }
            priorities.insert(task_name.clone(), aggressor_priority);
        }
        priorities
    }

    fn transform_vertices(ptpn: &mut PTPN, tdg: &TDG) {
        let resume_mode = is_resume_policy(tdg.policy);

        // Deterministic ordering: sort vertex names.
        let mut names: Vec<&String> = tdg.nodes_type.keys().collect();
        names.sort();
        for vertex_name in names {
            let node_type = &tdg.nodes_type[vertex_name];
            let (start_idx, end_idx) =
                Self::add_node_matrix(ptpn, node_type, resume_mode, tdg.task_place_capacity);
            ptpn.node_start_end_map
                .insert(vertex_name.clone(), (start_idx, end_idx));
        }
    }

    fn transform_edges(ptpn: &mut PTPN, tdg: &TDG) {
        for edge in &tdg.tdg_edges {
            if edge.is_self_loop() {
                Self::handle_self_loop_edge(ptpn, &edge.label, &edge.source);
                continue;
            }
            if edge.is_dashed() {
                // Dashed edges are handled by periodic release bindings.
                continue;
            }
            Self::handle_normal_edge(ptpn, tdg, &edge.source, &edge.target, &edge.label);
        }
    }

    fn handle_self_loop_edge(ptpn: &mut PTPN, label: &str, source_name: &str) {
        let task_period_time: i32 = label.parse().expect("self-loop label must be numeric");
        let (start, end) = *ptpn
            .node_start_end_map
            .get(source_name)
            .unwrap_or_else(|| panic!("Start/end nodes not found for: {}", source_name));
        Self::add_monitor(ptpn, source_name, task_period_time, start, end);
    }

    fn handle_normal_edge(
        ptpn: &mut PTPN,
        tdg: &TDG,
        source_name: &str,
        target_name: &str,
        label: &str,
    ) {
        let (_, source_exit) = *ptpn
            .node_start_end_map
            .get(source_name)
            .unwrap_or_else(|| panic!("Node mapping not found for edge: {}", source_name));
        let (target_entry, _) = *ptpn
            .node_start_end_map
            .get(target_name)
            .unwrap_or_else(|| panic!("Node mapping not found for edge: {}", target_name));

        let source_is_control = tdg
            .nodes_type
            .get(source_name)
            .map(|n| n.is_fork_or_join())
            .unwrap_or(false);
        let target_is_control = tdg
            .nodes_type
            .get(target_name)
            .map(|n| n.is_fork_or_join())
            .unwrap_or(false);

        assert!(
            !(source_is_control && target_is_control),
            "Invalid TDG edge between transition nodes: {} -> {}",
            source_name,
            target_name
        );

        if source_is_control {
            ptpn.set_post_arc(source_exit, target_entry, 1);
            return;
        }

        if target_is_control {
            ptpn.set_pre_arc(source_exit, target_entry, 1);
            return;
        }

        let interval = parse_edge_interval(label, source_name, target_name);
        let bridge = add_control_transition(
            ptpn,
            &format!("{}_to_{}", source_name, target_name),
            interval,
        );
        ptpn.set_pre_arc(source_exit, bridge, 1);
        ptpn.set_post_arc(bridge, target_entry, 1);
    }

    fn add_resources_and_bindings(ptpn: &mut PTPN, tdg: &TDG) {
        if !is_resume_policy(tdg.policy) {
            Self::add_cpu_resource(ptpn, tdg.num_cpus, tdg.cores_per_cpu);
        } else {
            // One task per core matching the physical model.
            for cpu in 0..tdg.num_cpus {
                ptpn.core_parallelism.insert(cpu, 1);
            }
        }
        Self::add_lock_resource(ptpn, &tdg.lock_set);
        if !is_resume_policy(tdg.policy) {
            Self::task_bind_cpu_resource(ptpn, &tdg.all_task);
        }
        Self::task_bind_lock_resource(ptpn, &tdg.all_task, &tdg.task_locks_map);
    }

    fn add_cpu_resource(ptpn: &mut PTPN, cpus: i32, cores_per_cpu: i32) {
        for core in 0..cpus {
            let core_name = format!("core{}", core);
            let core_place = ptpn.add_place(&core_name, cores_per_cpu, false);
            ptpn.cpus_place.push(core_place);
            ptpn.set_initial_marking(core_place, cores_per_cpu);
        }
    }

    fn add_lock_resource(ptpn: &mut PTPN, locks_name: &std::collections::HashSet<String>) {
        for lock_name in locks_name {
            let lock_place = ptpn.add_place(lock_name, 1, false);
            ptpn.locks_place.insert(lock_name.clone(), lock_place);
            ptpn.set_initial_marking(lock_place, 1);
        }
    }

    fn task_bind_cpu_resource(ptpn: &mut PTPN, all_task: &[NodeType]) {
        for node in all_task {
            let Some(task) = node.as_task() else {
                continue;
            };
            let Some(chain_owned) = ptpn.node_pn_map.get(&task.name).cloned() else {
                continue;
            };
            let chain = &chain_owned;
            if chain.len() < TaskChainLayout::K_MIN_LENGTH {
                continue;
            }
            let core_idx = task.core as usize;
            if core_idx >= ptpn.cpus_place.len() {
                continue;
            }
            let get_core = chain[TaskChainLayout::K_GET_CORE];
            let release = chain[chain.len() - 2];
            ptpn.set_pre_arc(ptpn.cpus_place[core_idx], get_core, 1);
            ptpn.set_post_arc(release, ptpn.cpus_place[core_idx], 1);
        }
    }

    fn task_bind_lock_resource(
        ptpn: &mut PTPN,
        all_task: &[NodeType],
        task_locks: &std::collections::HashMap<String, Vec<String>>,
    ) {
        for node in all_task {
            let Some(task) = node.as_task() else {
                continue;
            };
            let Some(chain_owned) = ptpn.node_pn_map.get(&task.name).cloned() else {
                continue;
            };
            Self::bind_task_locks(ptpn, &task.name, &task.lock, &chain_owned, task_locks);
        }
    }

    fn bind_task_locks(
        ptpn: &mut PTPN,
        task_name: &str,
        lock_types: &[String],
        task_pt_chain: &[usize],
        task_locks: &std::collections::HashMap<String, Vec<String>>,
    ) {
        if task_pt_chain.len() < TaskChainLayout::K_MIN_LENGTH {
            return;
        }
        let Some(locks) = task_locks.get(task_name) else {
            return;
        };

        let lock_count = locks.len();
        for lock_index in 0..lock_count {
            let lock_type = &lock_types[lock_index];
            let acquire_chain_index = TaskChainLayout::lock_acquire_transition(lock_index);
            let release_chain_index =
                TaskChainLayout::lock_release_transition(task_pt_chain.len(), lock_index);

            if acquire_chain_index >= task_pt_chain.len()
                || release_chain_index >= task_pt_chain.len()
            {
                panic!(
                    "Task chain layout does not match lock structure for: {}",
                    task_name
                );
            }

            let acquire_transition = task_pt_chain[acquire_chain_index];
            let release_transition = task_pt_chain[release_chain_index];

            let Some(&lock_place) = ptpn.locks_place.get(lock_type) else {
                panic!("Lock place not found: {}", lock_type);
            };

            ptpn.set_pre_arc(lock_place, acquire_transition, 1);
            ptpn.set_post_arc(release_transition, lock_place, 1);
        }
    }

    fn add_node_matrix(
        ptpn: &mut PTPN,
        node_type: &NodeType,
        resume_mode: bool,
        task_place_capacity: i32,
    ) -> (usize, usize) {
        match node_type {
            NodeType::Task(task) => {
                Self::add_task_node(ptpn, task, resume_mode, task_place_capacity)
            }
            NodeType::Join(join) => {
                let interval = TimeInterval::closed(join.time.0, join.time.1);
                let t = ptpn.add_transition(
                    &format!("Join{}", ptpn.node_index),
                    interval,
                    join.priority,
                    join.core,
                    false,
                );
                ptpn.node_index += 1;
                (t, t)
            }
            NodeType::Fork(fork) => {
                let interval = TimeInterval::closed(fork.time.0, fork.time.1);
                let t = ptpn.add_transition(
                    &format!("Fork{}", ptpn.node_index),
                    interval,
                    fork.priority,
                    fork.core,
                    false,
                );
                ptpn.node_index += 1;
                (t, t)
            }
            NodeType::Empty(_) => {
                let p = ptpn.add_place(&format!("Empty{}", ptpn.node_index), 1, false);
                ptpn.node_index += 1;
                (p, p)
            }
        }
    }

    fn add_execution_chain(
        ptpn: &mut PTPN,
        task_name: &str,
        times: &[(i32, i32)],
        locks: &[String],
        priority: i32,
        core: i32,
        resume_mode: bool,
        task_place_capacity: i32,
    ) -> Vec<usize> {
        assert!(
            !times.is_empty(),
            "Task has no execution segments: {}",
            task_name
        );

        let capacity = task_place_capacity;
        let saturate = true;

        let mut chain: Vec<usize> = Vec::new();

        let entry = ptpn.add_place(&format!("{}entry", task_name), capacity, saturate);
        let encoded_priority = encode_task_execution_priority(priority);
        let get_core = ptpn.add_transition(
            &format!("{}get_core", task_name),
            immediate_interval(),
            encoded_priority,
            core,
            false,
        );
        let ready = ptpn.add_place(&format!("{}ready", task_name), capacity, saturate);

        ptpn.set_pre_arc(entry, get_core, 1);
        ptpn.set_post_arc(get_core, ready, 1);
        chain.extend([entry, get_core, ready]);

        let mut current_place = ready;

        for (segment_index, &(start, end)) in times.iter().enumerate() {
            let exec_name = if times.len() == 1 {
                format!("{}exec", task_name)
            } else {
                format!("{}_exec_{}", task_name, segment_index + 1)
            };
            let segment_holds_spin_lock =
                segment_index < locks.len() && locks[segment_index].contains("spin");
            let exec_suspendable = resume_mode && !segment_holds_spin_lock;
            let exec = ptpn.add_transition(
                &exec_name,
                TimeInterval::closed(start, end),
                encoded_priority,
                core,
                exec_suspendable,
            );

            let is_last_segment = segment_index + 1 == times.len();
            let next_place_name = if is_last_segment {
                format!("{}exit", task_name)
            } else {
                format!("{}_seg_{}_done", task_name, segment_index + 1)
            };
            let next_place = ptpn.add_place(&next_place_name, capacity, saturate);

            ptpn.set_pre_arc(current_place, exec, 1);
            ptpn.set_post_arc(exec, next_place, 1);
            chain.extend([exec, next_place]);
            current_place = next_place;

            if segment_index < locks.len() {
                let lock_name = format!("{}_lock_{}", task_name, segment_index + 1);
                let lock_transition = ptpn.add_transition(
                    &lock_name,
                    immediate_interval(),
                    encoded_priority,
                    core,
                    false,
                );
                let hold_place = ptpn.add_place(
                    &format!("{}_hold_{}", task_name, segment_index + 1),
                    capacity,
                    saturate,
                );

                ptpn.set_pre_arc(current_place, lock_transition, 1);
                ptpn.set_post_arc(lock_transition, hold_place, 1);
                chain.extend([lock_transition, hold_place]);
                current_place = hold_place;
            }
        }

        chain
    }

    fn add_task_node(
        ptpn: &mut PTPN,
        task: &crate::types::TaskNode,
        resume_mode: bool,
        task_place_capacity: i32,
    ) -> (usize, usize) {
        let chain = Self::add_execution_chain(
            ptpn,
            &task.name,
            &task.time,
            &task.lock,
            task.priority,
            task.core,
            resume_mode,
            task_place_capacity,
        );
        ptpn.node_pn_map.insert(task.name.clone(), chain.clone());
        (*chain.first().unwrap(), *chain.last().unwrap())
    }

    fn add_monitor(
        ptpn: &mut PTPN,
        task_name: &str,
        task_period_time: i32,
        start: usize,
        end: usize,
    ) {
        let deadline = ptpn.add_place(&format!("{}deadline", task_name), 1, false);
        let timeout = ptpn.add_place(&format!("{}timeout", task_name), 1, false);
        let ok = ptpn.add_place(&format!("{}ok", task_name), 1, false);
        let end_place = ptpn.add_place(&format!("{}end", task_name), 1, false);

        let timed = add_control_transition(
            ptpn,
            &format!("{}timed", task_name),
            TimeInterval::closed(task_period_time, task_period_time),
        );
        let ending =
            add_control_transition(ptpn, &format!("{}ending", task_name), immediate_interval());
        let complete = add_control_transition(
            ptpn,
            &format!("{}complete", task_name),
            immediate_interval(),
        );
        let timeout_transition =
            add_control_transition(ptpn, &format!("{}out", task_name), immediate_interval());

        ptpn.set_post_arc(ending, end_place, 1);
        ptpn.set_pre_arc(end_place, complete, 1);
        ptpn.set_post_arc(complete, ok, 1);
        ptpn.set_pre_arc(deadline, ok, 1);
        ptpn.set_pre_arc(deadline, timeout_transition, 1);
        ptpn.set_post_arc(timeout_transition, timeout, 1);

        if end < ptpn.net.places.len() {
            ptpn.set_pre_arc(end, ending, 1);
        }
        if start < ptpn.net.places.len() {
            ptpn.set_pre_arc(start, timed, 1);
            ptpn.set_post_arc(timed, deadline, 1);
        }
    }

    fn fixed_prior_with_restart(
        ptpn: &mut PTPN,
        core_task: &std::collections::HashMap<i32, Vec<String>>,
        tc: &std::collections::HashMap<String, TaskConfig>,
        nodes_type: &std::collections::HashMap<String, NodeType>,
    ) {
        let _ = nodes_type; // kept for API parity with the C++ implementation
        for (_core_id, tasks) in core_task {
            for (i, h_t_name) in tasks.iter().enumerate() {
                let Some(h_tc) = tc.get(h_t_name) else {
                    continue;
                };
                let preempt_priorities = Self::build_preempt_priorities(tasks, tc, h_tc.priority);

                for l_t_name in tasks.iter().skip(i + 1) {
                    let Some(l_tc) = tc.get(l_t_name) else {
                        continue;
                    };
                    let Some(&preempt_priority) = preempt_priorities.get(l_t_name) else {
                        continue;
                    };
                    if l_tc.priority == h_tc.priority {
                        continue;
                    }

                    let Some(l_t_pn_owned) = ptpn.node_pn_map.get(l_t_name).cloned() else {
                        continue;
                    };
                    let Some(h_t_pn_owned) = ptpn.node_pn_map.get(h_t_name).cloned() else {
                        continue;
                    };
                    let l_t_pn = &l_t_pn_owned;
                    let h_t_pn = &h_t_pn_owned;

                    if l_t_pn.len() < 5 || h_t_pn.len() < 5 {
                        continue;
                    }

                    let l_exec = l_t_pn[3];
                    if l_exec < ptpn.net.transitions.len() {
                        ptpn.net.transitions[l_exec].kind.suspendable = true;
                    }

                    let l_entry = l_t_pn[0];
                    let l_preempt_place = l_t_pn[2];
                    let h_entry = h_t_pn[0];
                    let h_ready = h_t_pn[2];

                    let preempt_name = format!(
                        "{}_restart_preempt_{}_{}",
                        h_t_name, l_t_name, ptpn.node_index
                    );
                    let preempt_trans = ptpn.add_transition(
                        &preempt_name,
                        immediate_interval(),
                        preempt_priority,
                        h_tc.core,
                        false,
                    );
                    ptpn.node_index += 1;

                    ptpn.set_pre_arc(h_entry, preempt_trans, 1);
                    ptpn.set_pre_arc(l_preempt_place, preempt_trans, 1);
                    ptpn.set_post_arc(preempt_trans, h_ready, 1);
                    ptpn.set_post_arc(preempt_trans, l_entry, 1);

                    if !l_tc.locks.is_empty() {
                        const MIN_CHAIN_LENGTH: usize = 9;
                        if l_t_pn.len() < MIN_CHAIN_LENGTH {
                            continue;
                        }

                        for i in 0..l_tc.locks.len() {
                            if l_tc.locks[i].contains("spin") {
                                break;
                            }
                            let idx = l_t_pn.len() - 2 - 2 * (i + 1);
                            if idx < ptpn.net.transitions.len() {
                                ptpn.net.transitions[idx].kind.suspendable = true;
                            }

                            let lock_preempt_place = l_t_pn[idx - 1];
                            let lock_preempt_name = format!(
                                "{}_restart_lock_preempt_{}_{}",
                                h_t_name, l_t_name, ptpn.node_index
                            );
                            let lock_preempt_trans = ptpn.add_transition(
                                &lock_preempt_name,
                                immediate_interval(),
                                preempt_priority,
                                h_tc.core,
                                false,
                            );
                            ptpn.node_index += 1;

                            ptpn.set_pre_arc(h_entry, lock_preempt_trans, 1);
                            ptpn.set_pre_arc(lock_preempt_place, lock_preempt_trans, 1);
                            ptpn.set_post_arc(lock_preempt_trans, h_ready, 1);
                            ptpn.set_post_arc(lock_preempt_trans, l_entry, 1);
                        }
                    }
                }
            }
        }
    }
}
