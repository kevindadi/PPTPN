//! 矩阵形式 PTPN
//!
//! Place/Transition 网，Pre/Post 矩阵表示

use crate::error::PtpnErrorKind;
use crate::ptpn::time_interval::{TimeInterval, INF};
use crate::tdg::{
    classify_priority, AperiodicTask, NodeType, PeriodicTask, Tdg, TdgVertexType,
};
use std::collections::HashMap;
use tracing::{debug, info};

/// 库所
#[derive(Debug, Clone)]
pub struct Place {
    pub id: String,
    pub name: String,
    pub capacity: i32,
}

impl Place {
    pub fn new(id: impl Into<String>, name: impl Into<String>, capacity: i32) -> Self {
        Self {
            id: id.into(),
            name: name.into(),
            capacity,
        }
    }
}

/// 变迁
#[derive(Debug, Clone)]
pub struct Transition {
    pub id: String,
    pub name: String,
    pub time_interval: TimeInterval,
    pub priority: i32,
    pub core: i32,
    pub suspendable: bool,
}

impl Transition {
    pub fn new(
        id: impl Into<String>,
        name: impl Into<String>,
        time_interval: TimeInterval,
        priority: i32,
        core: i32,
        suspendable: bool,
    ) -> Self {
        Self {
            id: id.into(),
            name: name.into(),
            time_interval,
            priority,
            core,
            suspendable,
        }
    }
}

/// 标识向量 M[p] 表示库所 p 的 token 数
pub type Marking = Vec<i32>;

/// 矩阵形式 PTPN
#[derive(Debug, Clone)]
pub struct MatrixPTPN {
    pub places: Vec<Place>,
    pub transitions: Vec<Transition>,
    /// Pre: |P| x |T|
    pub pre: Vec<Vec<i32>>,
    /// Post: |T| x |P|
    pub post: Vec<Vec<i32>>,
    pub m0: Marking,
    node_start_end_map: HashMap<String, (usize, usize)>,
    node_pn_map: HashMap<String, Vec<usize>>,
    cpus_place: Vec<usize>,
    locks_place: HashMap<String, usize>,
    node_index: usize,
}

impl MatrixPTPN {
    pub fn new() -> Self {
        Self {
            places: Vec::new(),
            transitions: Vec::new(),
            pre: Vec::new(),
            post: Vec::new(),
            m0: Vec::new(),
            node_start_end_map: HashMap::new(),
            node_pn_map: HashMap::new(),
            cpus_place: Vec::new(),
            locks_place: HashMap::new(),
            node_index: 0,
        }
    }

    pub fn add_place(&mut self, name: impl Into<String>, capacity: i32) -> usize {
        let id = self.places.len().to_string();
        let name = name.into();
        self.places.push(Place::new(id.clone(), name, capacity));

        self.pre.push(vec![0; self.transitions.len()]);
        self.m0.push(0);

        for row in &mut self.post {
            row.push(0);
        }

        self.places.len() - 1
    }

    pub fn add_transition(
        &mut self,
        name: impl Into<String>,
        interval: TimeInterval,
        priority: i32,
        core: i32,
        suspendable: bool,
    ) -> usize {
        let id = self.transitions.len().to_string();
        let name = name.into();
        self.transitions.push(Transition::new(
            id, name, interval, priority, core, suspendable,
        ));

        for row in &mut self.pre {
            row.push(0);
        }
        self.post.push(vec![0; self.places.len()]);

        self.transitions.len() - 1
    }

    pub fn set_pre_arc(&mut self, place_idx: usize, trans_idx: usize, weight: i32) {
        if place_idx >= self.pre.len() || trans_idx >= self.transitions.len() {
            return;
        }
        self.pre[place_idx][trans_idx] = weight;
    }

    pub fn set_post_arc(&mut self, trans_idx: usize, place_idx: usize, weight: i32) {
        if trans_idx >= self.post.len() || place_idx >= self.places.len() {
            return;
        }
        self.post[trans_idx][place_idx] = weight;
    }

    pub fn set_initial_marking(&mut self, place_idx: usize, tokens: i32) {
        if place_idx < self.m0.len() && tokens >= 0 {
            self.m0[place_idx] = tokens;
        }
    }

    pub fn num_places(&self) -> usize {
        self.places.len()
    }

    pub fn num_transitions(&self) -> usize {
        self.transitions.len()
    }

    pub fn get_place(&self, idx: usize) -> Option<&Place> {
        self.places.get(idx)
    }

    pub fn get_transition(&self, idx: usize) -> Option<&Transition> {
        self.transitions.get(idx)
    }

    pub fn get_marking(&self) -> &Marking {
        &self.m0
    }

    pub fn get_pre_matrix(&self) -> &[Vec<i32>] {
        &self.pre
    }

    pub fn get_post_matrix(&self) -> &[Vec<i32>] {
        &self.post
    }

    /// 检查变迁是否使能
    pub fn is_enabled(marking: &[i32], net: &MatrixPTPN, trans_idx: usize) -> bool {
        if trans_idx >= net.transitions.len() || marking.len() != net.places.len() {
            return false;
        }
        for p in 0..net.places.len() {
            if net.pre[p][trans_idx] > 0 && marking[p] < net.pre[p][trans_idx] {
                return false;
            }
        }
        true
    }

    /// 触发变迁，返回新标识
    pub fn fire(marking: &[i32], net: &MatrixPTPN, trans_idx: usize) -> Result<Marking, PtpnErrorKind> {
        if !Self::is_enabled(marking, net, trans_idx) {
            return Err(PtpnErrorKind::TransitionNotEnabled { trans_idx });
        }

        let mut new_marking = marking.to_vec();

        for p in 0..net.places.len() {
            new_marking[p] -= net.pre[p][trans_idx];
        }
        for p in 0..net.places.len() {
            new_marking[p] += net.post[trans_idx][p];
            if let Some(place) = net.places.get(p) {
                if place.capacity != INF && new_marking[p] > place.capacity {
                    new_marking[p] = place.capacity;
                }
            }
        }

        Ok(new_marking)
    }

    pub fn get_enabled_transitions(&self) -> Vec<usize> {
        (0..self.transitions.len())
            .filter(|&t| Self::is_enabled(&self.m0, self, t))
            .collect()
    }

    pub fn filter_by_core_and_priority(&self, enabled: &[usize]) -> Vec<usize> {
        if enabled.is_empty() {
            return Vec::new();
        }

        let mut by_core: HashMap<i32, Vec<usize>> = HashMap::new();
        for &t in enabled {
            if let Some(trans) = self.get_transition(t) {
                by_core.entry(trans.core).or_default().push(t);
            }
        }

        let mut result = Vec::new();
        for (_core, trans_list) in by_core {
            let best = trans_list
                .iter()
                .max_by_key(|&&t| self.get_transition(t).map(|tr| tr.priority).unwrap_or(0));
            if let Some(&t) = best {
                result.push(t);
            }
        }
        result
    }

    /// 从 TDG 转换到矩阵 PTPN
    pub fn transform_tdg_to_matrix_ptpn(&mut self, tdg: &mut Tdg) {
        info!("[MATRIX_PTPN] 开始从 TDG 转换到矩阵形式 PTPN...");

        classify_priority(tdg);

        self.transform_vertices_from_tdg(tdg);
        self.transform_edges_from_tdg(tdg);
        self.add_resources_and_bindings(tdg);

        info!(
            "[MATRIX_PTPN] TDG 转换完成: {} 个库所, {} 个变迁",
            self.places.len(),
            self.transitions.len()
        );
    }

    fn transform_vertices_from_tdg(&mut self, tdg: &Tdg) {
        for (name, node_type) in &tdg.nodes_type {
            debug!("[MATRIX_PTPN] 处理顶点: {}", name);

            let (start_idx, end_idx) = self.add_node_matrix(node_type);
            self.node_start_end_map.insert(name.clone(), (start_idx, end_idx));

            if let NodeType::Aperiodic(_) = node_type {
                if !tdg.edges.iter().any(|(s, _, _)| s == name) {
                    let interval = TimeInterval::zero();
                    let consume_trans = self.add_transition(
                        format!("{}_consume", name),
                        interval,
                        411,
                        411,
                        false,
                    );
                    self.set_pre_arc(end_idx, consume_trans, 1);
                }
            }
        }
    }

    fn add_node_matrix(&mut self, node_type: &NodeType) -> (usize, usize) {
        match node_type {
            NodeType::Periodic(p) => self.add_p_node_matrix(p.clone()),
            NodeType::Aperiodic(ap) => self.add_ap_node_matrix(ap.clone()),
            NodeType::Sync(s) => {
                let interval = TimeInterval::zero();
                let trans = self.add_transition(
                    format!("Sync{}", self.node_index),
                    interval,
                    411,
                    411,
                    false,
                );
                self.node_index += 1;
                (trans, trans)
            }
            NodeType::Dist(d) => {
                let interval = TimeInterval::zero();
                let trans = self.add_transition(
                    format!("Dist{}", self.node_index),
                    interval,
                    411,
                    411,
                    false,
                );
                self.node_index += 1;
                (trans, trans)
            }
            NodeType::Empty(e) => {
                let place = self.add_place(format!("Empty{}", self.node_index), 1);
                self.node_index += 1;
                (place, place)
            }
        }
    }

    fn add_p_node_matrix(&mut self, p_task: PeriodicTask) -> (usize, usize) {
        let exec_time = p_task.time.first().copied().unwrap_or((0, 0));
        let exec_interval = TimeInterval {
            earliest: exec_time.0,
            latest: if exec_time.1 >= INF { INF } else { exec_time.1 },
        };

        let entry = self.add_place(format!("{}entry", p_task.name), 1);
        let get_core = self.add_transition(
            format!("{}get_core", p_task.name),
            TimeInterval::zero(),
            p_task.priority,
            p_task.core,
            false,
        );
        let ready = self.add_place(format!("{}ready", p_task.name), 1);
        let exec = self.add_transition(
            format!("{}exec", p_task.name),
            exec_interval,
            p_task.priority,
            p_task.core,
            false,
        );
        let exit = self.add_place(format!("{}exit", p_task.name), 1);

        let random = self.add_place(format!("{}random", p_task.name), 1);
        let fire_interval = TimeInterval {
            earliest: p_task.period_time.0,
            latest: if p_task.period_time.1 >= INF {
                INF
            } else {
                p_task.period_time.1
            },
        };
        let fire = self.add_transition(
            format!("{}fire", p_task.name),
            fire_interval,
            411,
            411,
            false,
        );

        self.set_initial_marking(random, 1);
        self.set_pre_arc(random, fire, 1);
        self.set_post_arc(fire, random, 1);
        self.set_post_arc(fire, entry, 1);
        self.set_pre_arc(entry, get_core, 1);
        self.set_post_arc(get_core, ready, 1);
        self.set_pre_arc(ready, exec, 1);
        self.set_post_arc(exec, exit, 1);

        let chain = vec![entry, get_core, ready, exec, exit];
        self.node_pn_map.insert(p_task.name.clone(), chain);

        (entry, exit)
    }

    fn add_ap_node_matrix(&mut self, ap_task: AperiodicTask) -> (usize, usize) {
        let exec_time = ap_task.time.first().copied().unwrap_or((0, 0));
        let exec_interval = TimeInterval {
            earliest: exec_time.0,
            latest: if exec_time.1 >= INF { INF } else { exec_time.1 },
        };

        let entry = self.add_place(format!("{}entry", ap_task.name), 1);
        let get_core = self.add_transition(
            format!("{}get_core", ap_task.name),
            TimeInterval::zero(),
            ap_task.priority,
            ap_task.core,
            false,
        );
        let ready = self.add_place(format!("{}ready", ap_task.name), 1);
        let exec = self.add_transition(
            format!("{}exec", ap_task.name),
            exec_interval,
            ap_task.priority,
            ap_task.core,
            false,
        );
        let exit = self.add_place(format!("{}exit", ap_task.name), 1);

        self.set_pre_arc(entry, get_core, 1);
        self.set_post_arc(get_core, ready, 1);
        self.set_pre_arc(ready, exec, 1);
        self.set_post_arc(exec, exit, 1);

        let chain = vec![entry, get_core, ready, exec, exit];
        self.node_pn_map.insert(ap_task.name.clone(), chain);

        (entry, exit)
    }

    fn transform_edges_from_tdg(&mut self, tdg: &Tdg) {
        for (source_name, target_name, edge_info) in &tdg.edges {
            if source_name == target_name {
                if let Some(&(start, end)) = self.node_start_end_map.get(source_name) {
                    if let Ok(period) = edge_info.label.parse::<i32>() {
                        self.add_monitor_matrix(source_name, period, start, end);
                    }
                }
                continue;
            }
            if edge_info.style.contains("dashed") {
                continue;
            }
            self.handle_normal_edge(source_name, target_name);
        }
    }

    fn add_monitor_matrix(&mut self, task_name: &str, period_time: i32, start: usize, end: usize) {
        let deadline = self.add_place(format!("{}deadline", task_name), 1);
        let timeout = self.add_place(format!("{}timeout", task_name), 1);
        let ok = self.add_place(format!("{}ok", task_name), 1);
        let t_end = self.add_place(format!("{}end", task_name), 1);

        let interval = TimeInterval {
            earliest: period_time,
            latest: period_time,
        };
        let timed = self.add_transition(format!("{}timed", task_name), interval, 411, 411, false);

        self.set_initial_marking(deadline, 1);
        self.set_pre_arc(end, timed, 1);
        self.set_post_arc(timed, start, 1);
        self.set_pre_arc(deadline, timed, 1);
        self.set_post_arc(timed, ok, 1);
    }

    fn handle_normal_edge(&mut self, source_name: &str, target_name: &str) {
        let Some(&(_, source_end)) = self.node_start_end_map.get(source_name) else {
            return;
        };
        let Some(&(target_start, _)) = self.node_start_end_map.get(target_name) else {
            return;
        };

        let trans_name = format!("{}_to_{}", source_name, target_name);
        let interval = TimeInterval::zero();
        let middle_trans = self.add_transition(trans_name, interval, 411, 411, false);

        if source_end < self.places.len() && target_start < self.places.len() {
            self.set_pre_arc(source_end, middle_trans, 1);
            self.set_post_arc(middle_trans, target_start, 1);
        }
    }

    fn add_resources_and_bindings(&mut self, tdg: &Tdg) {
        self.add_cpu_resource(tdg.num_cpus, tdg.cores_per_cpu);
        self.add_lock_resource(&tdg.lock_set);
        self.task_bind_cpu(&tdg.all_task);
    }

    fn add_cpu_resource(&mut self, cpus: i32, cores_per_cpu: i32) {
        for i in 0..cpus {
            let c = self.add_place(format!("core{}", i), cores_per_cpu);
            self.cpus_place.push(c);
            self.set_initial_marking(c, cores_per_cpu);
        }
    }

    fn add_lock_resource(&mut self, locks: &std::collections::HashSet<String>) {
        for lock_name in locks {
            let l = self.add_place(lock_name.clone(), 1);
            self.locks_place.insert(lock_name.clone(), l);
            self.set_initial_marking(l, 1);
        }
    }

    fn task_bind_cpu(&mut self, all_task: &[NodeType]) {
        for task in all_task {
            let (name, core, chain) = match task {
                NodeType::Aperiodic(ap) => {
                    let chain = self.node_pn_map.get(&ap.name).cloned().unwrap_or_default();
                    (ap.name.clone(), ap.core, chain)
                }
                NodeType::Periodic(p) => {
                    let chain = self.node_pn_map.get(&p.name).cloned().unwrap_or_default();
                    (p.name.clone(), p.core, chain)
                }
                _ => continue,
            };

            let cpu_index = core as usize;
            if cpu_index >= self.cpus_place.len() {
                continue;
            }
            let cpu_place = self.cpus_place[cpu_index];
            if chain.len() >= 2 {
                self.set_pre_arc(cpu_place, chain[1], 1);
                if chain.len() >= 5 {
                    self.set_post_arc(chain[chain.len() - 2], cpu_place, 1);
                }
            }
        }
    }
}

impl Default for MatrixPTPN {
    fn default() -> Self {
        Self::new()
    }
}
