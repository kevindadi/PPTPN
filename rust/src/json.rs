//! TDG JSON parsing and validation (port of `src/json/json.cpp`).

use crate::types::{
    NodeType, PeriodicBinding, SchedulePolicy, StartBinding, TaskNode, TaskType,
};
use serde_json::Value;
use std::collections::{HashMap, HashSet};

#[derive(Debug, Clone, Default)]
pub struct ParseResult {
    pub success: bool,
    pub error_message: String,
    pub error_line: i32,
}

#[derive(Debug, Clone, Default)]
pub struct ValidationResult {
    pub success: bool,
    pub errors: Vec<String>,
    pub warnings: Vec<String>,
}

impl ValidationResult {
    pub fn add_error(&mut self, err: impl Into<String>) {
        self.success = false;
        self.errors.push(err.into());
    }

    pub fn add_warning(&mut self, warn: impl Into<String>) {
        self.warnings.push(warn.into());
    }
}

#[derive(Debug, Clone)]
pub struct JsonNode {
    pub id: String,
    pub r#type: String,
    pub priority: i32,
    pub core: i32,
    pub has_priority: bool,
    pub has_core: bool,
    pub time: Vec<(i32, i32)>,
    pub locks: Vec<String>,
}

impl Default for JsonNode {
    fn default() -> Self {
        JsonNode {
            id: String::new(),
            r#type: String::new(),
            priority: 100,
            core: 0,
            has_priority: false,
            has_core: false,
            time: Vec::new(),
            locks: Vec::new(),
        }
    }
}

impl JsonNode {
    pub fn to_node_type(&self) -> NodeType {
        match self.r#type.as_str() {
            "task" => {
                let mut task = TaskNode {
                    name: self.id.clone(),
                    priority: self.priority,
                    core: self.core,
                    time: self.time.clone(),
                    lock: self.locks.clone(),
                    task_type: TaskType::Normal,
                    ..Default::default()
                };
                task.is_lock = false;
                NodeType::Task(task)
            }
            "fork" => {
                let mut fork = crate::types::ForkTask::new(self.id.clone());
                if !self.time.is_empty() {
                    fork.time = self.time[0];
                }
                fork.core = if self.has_core { self.core } else { -1 };
                fork.priority = if self.has_priority { self.priority } else { 0 };
                NodeType::Fork(fork)
            }
            "join" => {
                let mut join = crate::types::JoinTask::new(self.id.clone());
                if !self.time.is_empty() {
                    join.time = self.time[0];
                }
                join.core = if self.has_core { self.core } else { -1 };
                join.priority = if self.has_priority { self.priority } else { 0 };
                NodeType::Join(join)
            }
            _ => NodeType::Empty(crate::types::EmptyTask { name: self.id.clone() }),
        }
    }
}

#[derive(Debug, Clone)]
pub struct JsonEdge {
    pub source: String,
    pub target: String,
    pub label: String,
    pub style: String,
}

#[derive(Debug, Clone)]
pub struct JsonGraph {
    pub name: String,
    pub num_cpus: i32,
    pub cores_per_cpu: i32,
    pub task_place_capacity: i32,
    pub shared_locks: Vec<String>,
    pub policy: SchedulePolicy,
    pub start_tasks: Vec<StartBinding>,
    pub end_tasks: Vec<String>,
    pub periodic_tasks: Vec<PeriodicBinding>,
    pub nodes: Vec<JsonNode>,
    pub edges: Vec<JsonEdge>,
}

impl Default for JsonGraph {
    fn default() -> Self {
        JsonGraph {
            name: "G".to_string(),
            num_cpus: 1,
            cores_per_cpu: 1,
            task_place_capacity: 1,
            shared_locks: Vec::new(),
            policy: SchedulePolicy::Fixed,
            start_tasks: Vec::new(),
            end_tasks: Vec::new(),
            periodic_tasks: Vec::new(),
            nodes: Vec::new(),
            edges: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LockType {
    Mutex,
    Spin,
    Unknown,
}

pub fn get_lock_type(lock_name: &str) -> LockType {
    if lock_name.starts_with("mutex") {
        LockType::Mutex
    } else if lock_name.starts_with("spin") {
        LockType::Spin
    } else {
        LockType::Unknown
    }
}

pub fn get_lock_type_short(lock_name: &str) -> &'static str {
    match get_lock_type(lock_name) {
        LockType::Mutex => "[M]",
        LockType::Spin => "[S]",
        LockType::Unknown => "[?]",
    }
}

pub fn format_locks_with_type(locks: &[String]) -> String {
    if locks.is_empty() {
        return "none".to_string();
    }
    locks
        .iter()
        .map(|l| format!("{}{}", get_lock_type_short(l), l))
        .collect::<Vec<_>>()
        .join(" ")
}

/// Each lock adds a pre-CS, CS, and post-CS segment: total = 2 * locks + 1.
pub fn calculate_time_interval_count(lock_count: usize) -> usize {
    2 * lock_count + 1
}

pub fn get_time_interval_label(index: usize, locks: &[String]) -> String {
    let lock_count = locks.len();
    if lock_count == 0 {
        return "[Exec]".to_string();
    }
    if lock_count == 1 {
        return match index {
            0 => "[Pre]".to_string(),
            1 => format!("[CS:{}]", locks[0]),
            2 => "[Post]".to_string(),
            _ => "[?]".to_string(),
        };
    }
    // Nested locking: lock1 -> lock2 -> ... -> lockN -> unlockN -> ... -> unlock1.
    if index < lock_count {
        if index == 0 {
            format!("[Pre:{}]", locks[0])
        } else {
            format!("[CS:{}]", locks[index - 1])
        }
    } else if index == lock_count {
        format!("[CS:{}]", locks[lock_count - 1])
    } else {
        let post_index = index - lock_count;
        if post_index == 0 {
            format!("[Post:{}]", locks[lock_count - 1])
        } else {
            format!("[Post{}]", post_index)
        }
    }
}

struct ParserInner {
    graph: JsonGraph,
    original_json: String,
}

/// Parses and validates TDG JSON.
#[derive(Default)]
pub struct Parser {
    inner: ParserInner,
}

impl Default for ParserInner {
    fn default() -> Self {
        ParserInner {
            graph: JsonGraph::default(),
            original_json: String::new(),
        }
    }
}

impl Parser {
    pub fn new() -> Self {
        Parser {
            inner: ParserInner::default(),
        }
    }

    fn parse_graph_object(&mut self, graph_obj: &Value) {
        if let Some(name) = graph_obj.get("name").and_then(Value::as_str) {
            self.inner.graph.name = name.to_string();
        }
    }

    fn parse_start_binding(binding_obj: &Value) -> StartBinding {
        let mut binding = StartBinding::default();
        if let Some(s) = binding_obj.as_str() {
            binding.task = s.to_string();
            return binding;
        }
        if let Some(t) = binding_obj.get("task").and_then(Value::as_str) {
            binding.task = t.to_string();
        }
        if let Some(tokens) = binding_obj.get("tokens").and_then(Value::as_i64) {
            binding.tokens = tokens as i32;
        }
        binding
    }

    fn parse_periodic_binding(binding_obj: &Value) -> PeriodicBinding {
        let mut binding = PeriodicBinding::default();
        if let Some(t) = binding_obj.get("task").and_then(Value::as_str) {
            binding.task = t.to_string();
        }
        if let Some(period) = binding_obj.get("period").and_then(Value::as_i64) {
            binding.period = period as i32;
        }
        binding
    }

    fn parse_configuration_object(&mut self, config: &Value) {
        if let Some(v) = config.get("num_cpus").and_then(Value::as_i64) {
            self.inner.graph.num_cpus = v as i32;
        }
        if let Some(v) = config.get("cores_per_cpu").and_then(Value::as_i64) {
            self.inner.graph.cores_per_cpu = v as i32;
        }
        if let Some(v) = config.get("task_place_capacity").and_then(Value::as_i64) {
            self.inner.graph.task_place_capacity = v as i32;
        }
        if let Some(shared) = config.get("shared_locks") {
            if let Some(arr) = shared.as_array() {
                self.inner.graph.shared_locks = arr
                    .iter()
                    .filter_map(Value::as_str)
                    .map(|s| s.to_string())
                    .collect();
            }
        }
        if let Some(policy) = config.get("policy").and_then(Value::as_str) {
            self.inner.graph.policy = crate::types::parse_schedule_policy(policy);
        }
        if let Some(start) = config.get("start").and_then(Value::as_array) {
            self.inner.graph.start_tasks =
                start.iter().map(Self::parse_start_binding).collect();
        }
        if let Some(end) = config.get("end").and_then(Value::as_array) {
            self.inner.graph.end_tasks = end
                .iter()
                .filter_map(Value::as_str)
                .map(|s| s.to_string())
                .collect();
        }
        if let Some(periodic) = config.get("periodic").and_then(Value::as_array) {
            self.inner.graph.periodic_tasks = periodic
                .iter()
                .map(Self::parse_periodic_binding)
                .collect();
        }
    }

    fn parse_node_object(node_obj: &Value) -> JsonNode {
        let mut node = JsonNode::default();
        if let Some(id) = node_obj.get("id").and_then(Value::as_str) {
            node.id = id.to_string();
        }
        if let Some(t) = node_obj.get("type").and_then(Value::as_str) {
            node.r#type = t.to_string();
        }
        node.has_priority = node_obj.get("priority").is_some();
        node.has_core = node_obj.get("core").is_some();
        if let Some(p) = node_obj.get("priority").and_then(Value::as_i64) {
            node.priority = p as i32;
        }
        if let Some(c) = node_obj.get("core").and_then(Value::as_i64) {
            node.core = c as i32;
        }
        if let Some(time) = node_obj.get("time").and_then(Value::as_array) {
            for tr in time {
                if let Some(interval) = parse_time_interval(tr) {
                    node.time.push(interval);
                }
            }
        }
        if let Some(locks) = node_obj.get("locks").and_then(Value::as_array) {
            node.locks = locks
                .iter()
                .filter_map(Value::as_str)
                .map(|s| s.to_string())
                .collect();
        }
        node
    }

    fn parse_nodes_array(&mut self, nodes_array: &[Value]) {
        for node_obj in nodes_array {
            self.inner.graph.nodes.push(Self::parse_node_object(node_obj));
        }
    }

    fn parse_edges_array(&mut self, edges_array: &[Value]) {
        for edge_obj in edges_array {
            let mut edge = JsonEdge {
                source: String::new(),
                target: String::new(),
                label: String::new(),
                style: String::new(),
            };
            if let Some(s) = edge_obj.get("source").and_then(Value::as_str) {
                edge.source = s.to_string();
            }
            if let Some(t) = edge_obj.get("target").and_then(Value::as_str) {
                edge.target = t.to_string();
            }
            if let Some(l) = edge_obj.get("label").and_then(Value::as_str) {
                edge.label = l.to_string();
            }
            if let Some(s) = edge_obj.get("style").and_then(Value::as_str) {
                edge.style = s.to_string();
            }
            self.inner.graph.edges.push(edge);
        }
    }

    pub fn parse_string(&mut self, json_content: &str) -> ParseResult {
        self.inner.graph = JsonGraph::default();
        self.inner.original_json = json_content.to_string();

        let document: Value = match serde_json::from_str(json_content) {
            Ok(v) => v,
            Err(e) => {
                let line = e
                    .line()
                    .try_into()
                    .unwrap_or_default();
                return ParseResult {
                    success: false,
                    error_message: e.to_string(),
                    error_line: line,
                };
            }
        };

        if let Some(graph) = document.get("graph") {
            self.parse_graph_object(graph);
        }
        if let Some(config) = document.get("configuration") {
            self.parse_configuration_object(config);
        }
        if let Some(nodes) = document.get("nodes").and_then(Value::as_array) {
            self.parse_nodes_array(nodes);
        }
        if let Some(edges) = document.get("edges").and_then(Value::as_array) {
            self.parse_edges_array(edges);
        }

        ParseResult {
            success: true,
            error_message: String::new(),
            error_line: 0,
        }
    }

    pub fn parse_file(&mut self, file_path: &str) -> ParseResult {
        let content = match std::fs::read_to_string(file_path) {
            Ok(c) => c,
            Err(e) => {
                return ParseResult {
                    success: false,
                    error_message: format!("Failed to open file: {} ({})", file_path, e),
                    error_line: 0,
                };
            }
        };
        self.inner.original_json = content.clone();
        self.parse_string(&content)
    }

    pub fn validate(&self) -> ValidationResult {
        let mut result = ValidationResult {
            success: true,
            errors: Vec::new(),
            warnings: Vec::new(),
        };
        let graph = &self.inner.graph;

        if graph.task_place_capacity < 1 {
            result.add_error(format!(
                "task_place_capacity must be >= 1, got {}",
                graph.task_place_capacity
            ));
        }

        let valid_types: HashSet<&str> =
            ["task", "fork", "join", "empty"].into_iter().collect();
        let defined_locks: HashSet<&str> =
            graph.shared_locks.iter().map(|s| s.as_str()).collect();
        let max_core = graph.num_cpus * graph.cores_per_cpu - 1;

        let mut node_ids: HashSet<String> = HashSet::new();
        let mut node_types: HashMap<String, String> = HashMap::new();

        for node in &graph.nodes {
            if !node_ids.insert(node.id.clone()) {
                result.add_error(format!("Duplicate node ID: {}", node.id));
            }
            node_types.insert(node.id.clone(), node.r#type.clone());

            if !valid_types.contains(node.r#type.as_str()) {
                result.add_error(format!(
                    "Unknown node type: {} for node {}",
                    node.r#type, node.id
                ));
            }

            if node.r#type == "task" && (node.core < 0 || node.core > max_core) {
                result.add_error(format!(
                    "Invalid core number for node {}: {} (valid range: 0-{})",
                    node.id, node.core, max_core
                ));
            }

            for lock in &node.locks {
                if !defined_locks.contains(lock.as_str()) {
                    result.add_error(format!("Node {} uses undefined lock: {}", node.id, lock));
                }
                if get_lock_type(lock) == LockType::Unknown {
                    result.add_error(format!(
                        "Node {} uses invalid lock prefix '{}': must start with 'mutex' or 'spin'",
                        node.id, lock
                    ));
                }
            }

            if node.r#type == "task" {
                let expected = calculate_time_interval_count(node.locks.len());
                let actual = node.time.len();
                if actual != expected {
                    result.add_error(format!(
                        "Node {} has {} lock(s) but {} time interval(s) (expected {})",
                        node.id,
                        node.locks.len(),
                        actual,
                        expected
                    ));
                }
            }

            for (lo, hi) in &node.time {
                if lo > hi {
                    result.add_error(format!(
                        "Invalid time interval for node {}: [{}, {}]",
                        node.id, lo, hi
                    ));
                }
            }

            if node.r#type == "fork" || node.r#type == "join" {
                if !node.locks.is_empty() {
                    result.add_warning(format!(
                        "Node {} is {} but declares locks (ignored)",
                        node.id, node.r#type
                    ));
                }
                if node.time.len() > 1 {
                    result.add_warning(format!(
                        "Node {} is {} with multiple time intervals; only the first is used",
                        node.id, node.r#type
                    ));
                }
                if node.has_core
                    && node.core != -1
                    && (node.core < 0 || node.core > max_core)
                {
                    result.add_error(format!(
                        "Invalid core number for {} node {}: {} (valid: -1 for control core, or 0-{})",
                        node.r#type, node.id, node.core, max_core
                    ));
                }
            }
        }

        for edge in &graph.edges {
            if !node_ids.contains(&edge.source) {
                result.add_error(format!("Edge references unknown source node: {}", edge.source));
            }
            if !node_ids.contains(&edge.target) {
                result.add_error(format!("Edge references unknown target node: {}", edge.target));
            }
        }

        for start_task in &graph.start_tasks {
            if !node_ids.contains(&start_task.task) {
                result.add_error(format!("Start task references unknown node: {}", start_task.task));
                continue;
            }
            if node_types.get(&start_task.task).map(|s| s.as_str()) != Some("task") {
                result.add_error(format!(
                    "Start task must reference a task node: {}",
                    start_task.task
                ));
                continue;
            }
            if start_task.tokens < 0 {
                result.add_error(format!(
                    "Start task token count must be non-negative: {}",
                    start_task.task
                ));
            }
            if has_incoming_edge(graph, &start_task.task) {
                result.add_warning(format!(
                    "Start task {} has predecessor edges",
                    start_task.task
                ));
            }
        }

        for end_task in &graph.end_tasks {
            if !node_ids.contains(end_task) {
                result.add_error(format!("End task references unknown node: {}", end_task));
                continue;
            }
            if node_types.get(end_task).map(|s| s.as_str()) != Some("task") {
                result.add_error(format!("End task must reference a task node: {}", end_task));
                continue;
            }
            if has_outgoing_edge(graph, end_task) {
                result.add_warning(format!("End task {} has successor edges", end_task));
            }
        }

        for periodic_task in &graph.periodic_tasks {
            if !node_ids.contains(&periodic_task.task) {
                result.add_error(format!(
                    "Periodic task references unknown node: {}",
                    periodic_task.task
                ));
                continue;
            }
            if node_types.get(&periodic_task.task).map(|s| s.as_str()) != Some("task") {
                result.add_error(format!(
                    "Periodic task must reference a task node: {}",
                    periodic_task.task
                ));
                continue;
            }
            if periodic_task.period <= 0 {
                result.add_error(format!(
                    "Periodic task period must be positive: {}",
                    periodic_task.task
                ));
            }
            if has_self_loop_edge(graph, &periodic_task.task) {
                result.add_warning(format!(
                    "Periodic task {} already has a self-loop release edge",
                    periodic_task.task
                ));
            }
        }

        let has_task_nodes = graph.nodes.iter().any(|n| n.r#type == "task");
        if !has_task_nodes && !graph.nodes.is_empty() {
            result.add_warning("No task nodes found in graph");
        }

        result
    }

    // Getters mirroring the C++ Parser interface.
    pub fn get_graph_name(&self) -> &str {
        &self.inner.graph.name
    }
    pub fn get_num_cpus(&self) -> i32 {
        self.inner.graph.num_cpus
    }
    pub fn get_cores_per_cpu(&self) -> i32 {
        self.inner.graph.cores_per_cpu
    }
    pub fn get_task_place_capacity(&self) -> i32 {
        self.inner.graph.task_place_capacity
    }
    pub fn get_policy(&self) -> SchedulePolicy {
        self.inner.graph.policy
    }
    pub fn get_start_tasks(&self) -> &[StartBinding] {
        &self.inner.graph.start_tasks
    }
    pub fn get_end_tasks(&self) -> &[String] {
        &self.inner.graph.end_tasks
    }
    pub fn get_periodic_tasks(&self) -> &[PeriodicBinding] {
        &self.inner.graph.periodic_tasks
    }
    pub fn get_nodes(&self) -> &[JsonNode] {
        &self.inner.graph.nodes
    }
    pub fn get_edges(&self) -> &[JsonEdge] {
        &self.inner.graph.edges
    }
    pub fn get_original_json(&self) -> &str {
        &self.inner.original_json
    }
}

fn format_range(range: &(i32, i32)) -> String {
    format!("[{}, {}]", range.0, range.1)
}

/// Builds the DOT label of a TDG node (port of `node_to_dot_label`).
pub fn node_to_dot_label(node: &NodeType) -> String {
    match node {
        NodeType::Task(t) => {
            let mut out = format!("{}\\ntask\\nprio={} core={}\\n", t.name, t.priority, t.core);
            let mut parts = Vec::new();
            for (i, interval) in t.time.iter().enumerate() {
                parts.push(format!(
                    "{} {}",
                    get_time_interval_label(i, &t.lock),
                    format_range(interval)
                ));
            }
            out.push_str(&parts.join(", "));
            out.push_str(&format!("\\nlocks={}", format_locks_with_type(&t.lock)));
            out
        }
        NodeType::Fork(f) => format!("{}\\nfork", f.name),
        NodeType::Join(j) => format!("{}\\njoin", j.name),
        NodeType::Empty(e) => format!("{}\\nempty", e.name),
    }
}

fn parse_time_interval(tr: &Value) -> Option<(i32, i32)> {
    let arr = tr.as_array()?;
    if arr.len() != 2 {
        return None;
    }
    let lo = arr[0].as_i64()? as i32;
    let hi = arr[1].as_i64()? as i32;
    Some((lo, hi))
}

fn has_incoming_edge(graph: &JsonGraph, node_id: &str) -> bool {
    graph
        .edges
        .iter()
        .any(|e| e.target == node_id && e.source != node_id)
}

fn has_outgoing_edge(graph: &JsonGraph, node_id: &str) -> bool {
    graph
        .edges
        .iter()
        .any(|e| e.source == node_id && e.target != node_id)
}

fn has_self_loop_edge(graph: &JsonGraph, node_id: &str) -> bool {
    graph.edges.iter().any(|e| e.source == node_id && e.target == node_id)
}
