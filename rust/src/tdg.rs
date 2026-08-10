//! Task Dependency Graph model (port of `src/tdg/tdg.cpp`).

use crate::json::Parser;
use crate::types::{
    NodeType, PeriodicBinding, SchedulePolicy, StartBinding, TdgEdge, TdgVertexType, TaskConfig,
    TaskType,
};
use std::collections::{HashMap, HashSet};

#[derive(Debug, Clone, Default)]
pub struct TDG {
    pub num_cpus: i32,
    pub cores_per_cpu: i32,
    pub task_place_capacity: i32,
    pub policy: SchedulePolicy,
    /// All task nodes in insertion order.
    pub all_task: Vec<NodeType>,
    pub start_tasks: Vec<StartBinding>,
    pub end_tasks: Vec<String>,
    pub periodic_tasks: Vec<PeriodicBinding>,
    pub tasks_priority: HashMap<String, i32>,
    pub vertexes_type: HashMap<String, TdgVertexType>,
    pub nodes_type: HashMap<String, NodeType>,
    pub tasks_type: HashMap<String, TaskType>,
    pub lock_set: HashSet<String>,
    pub task_locks_map: HashMap<String, Vec<String>>,
    pub tasks_config: HashMap<String, TaskConfig>,
    pub tdg_edges: Vec<TdgEdge>,
}

impl TDG {
    pub fn new(num_cpus: i32, cores_per_cpu: i32) -> Self {
        TDG {
            num_cpus,
            cores_per_cpu,
            task_place_capacity: 1,
            policy: SchedulePolicy::Fixed,
            ..Default::default()
        }
    }

    pub fn load_from_parser(&mut self, parser: &Parser, log_nodes: bool) {
        self.num_cpus = parser.get_num_cpus();
        self.cores_per_cpu = parser.get_cores_per_cpu();
        self.task_place_capacity = parser.get_task_place_capacity();
        self.policy = parser.get_policy();
        self.start_tasks = parser.get_start_tasks().to_vec();
        self.end_tasks = parser.get_end_tasks().to_vec();
        self.periodic_tasks = parser.get_periodic_tasks().to_vec();

        for json_node in parser.get_nodes() {
            self.register_node(&json_node.to_node_type(), log_nodes);
        }

        self.tdg_edges.clear();
        for edge in parser.get_edges() {
            self.tdg_edges.push(TdgEdge {
                source: edge.source.clone(),
                target: edge.target.clone(),
                label: edge.label.clone(),
                style: edge.style.clone(),
            });
        }
    }

    pub fn register_node(&mut self, node: &NodeType, log_node: bool) {
        match node {
            NodeType::Task(t) => {
                self.all_task.push(node.clone());
                self.tasks_priority.insert(t.name.clone(), t.priority);
                self.nodes_type.insert(t.name.clone(), node.clone());
                self.vertexes_type.insert(t.name.clone(), TdgVertexType::Task);
                self.tasks_type.insert(t.name.clone(), TaskType::Normal);

                for lock in &t.lock {
                    self.lock_set.insert(lock.clone());
                    self.task_locks_map
                        .entry(t.name.clone())
                        .or_default()
                        .push(lock.clone());
                }

                if log_node {
                    // no-op in library; CLI logs
                }
            }
            NodeType::Fork(f) => {
                self.nodes_type.insert(f.name.clone(), node.clone());
                self.vertexes_type.insert(f.name.clone(), TdgVertexType::Fork);
            }
            NodeType::Join(j) => {
                self.nodes_type.insert(j.name.clone(), node.clone());
                self.vertexes_type.insert(j.name.clone(), TdgVertexType::Join);
            }
            NodeType::Empty(e) => {
                self.nodes_type.insert(e.name.clone(), node.clone());
                self.vertexes_type
                    .insert(e.name.clone(), TdgVertexType::Empty);
            }
        }
    }

    /// Parses from a JSON file path (ignoring errors; CLI validates separately).
    pub fn parse_json(&mut self, json_file: &str) {
        let mut parser = Parser::new();
        if !parser.parse_file(json_file).success {
            return;
        }
        self.load_from_parser(&parser, true);
    }

    pub fn parse_json_string(&mut self, json_content: &str) -> Result<(), String> {
        let mut parser = Parser::new();
        let result = parser.parse_string(json_content);
        if !result.success {
            return Err(format!("JSON parsing failed: {}", result.error_message));
        }
        self.load_from_parser(&parser, false);
        Ok(())
    }

    pub fn to_dot_string(&self) -> String {
        let mut out = String::new();
        out.push_str("digraph G {\n");

        // Deterministic ordering (sorted by name) to keep output stable.
        let mut names: Vec<&String> = self.nodes_type.keys().collect();
        names.sort();
        for name in names {
            let node = &self.nodes_type[name];
            out.push_str(&format!(
                "    {} [label = \"{}\";];\n",
                name,
                crate::json::node_to_dot_label(node)
            ));
        }

        for edge in &self.tdg_edges {
            out.push_str(&format!("    {} -> {}", edge.source, edge.target));
            if !edge.label.is_empty() || !edge.style.is_empty() {
                out.push_str(" [");
                if !edge.label.is_empty() {
                    out.push_str(&format!("xlabel = \"{}\"", edge.label));
                }
                if !edge.style.is_empty() {
                    if !edge.label.is_empty() {
                        out.push_str("; ");
                    }
                    out.push_str(&format!("style = \"{}\"", edge.style));
                }
                out.push_str(";]");
            }
            out.push_str(";\n");
        }

        out.push_str("}\n");
        out
    }

    pub fn export_to_dot(&self, output_path: &str) -> bool {
        match std::fs::write(output_path, self.to_dot_string()) {
            Ok(_) => true,
            Err(_) => false,
        }
    }

    /// Core -> tasks sorted by descending priority.
    pub fn classify_priority(&mut self) -> HashMap<i32, Vec<String>> {
        let mut core_task: HashMap<i32, Vec<String>> = HashMap::new();

        for task in &self.all_task {
            if let Some(task_node) = task.as_task() {
                self.tasks_config.insert(
                    task_node.name.clone(),
                    TaskConfig {
                        core: task_node.core,
                        priority: task_node.priority,
                        times: task_node.time.clone(),
                        locks: task_node.lock.clone(),
                    },
                );
                core_task
                    .entry(task_node.core)
                    .or_default()
                    .push(task_node.name.clone());
            }
        }

        for tasks in core_task.values_mut() {
            tasks.sort_by(|left, right| {
                let pl = self.tasks_priority.get(left).copied().unwrap_or(0);
                let pr = self.tasks_priority.get(right).copied().unwrap_or(0);
                pr.cmp(&pl)
            });
        }

        core_task
    }
}
