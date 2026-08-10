//! Core shared types: scheduling policies, task graph node variants, TDG edges.

use std::collections::HashMap;
use std::fmt;

/// Real-time task kinds.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TaskType {
    Normal,
    Period,
    Aperiod,
    Interrupt,
}

/// Real-time scheduling policy.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub enum SchedulePolicy {
    /// Fixed priority (compat alias; handled as resume).
    #[default]
    Fixed,
    /// Fixed priority with restart.
    FixedPriorWithRestart,
    /// Fixed priority with resume.
    FixedPriorWithResume,
    /// Rate Monotonic.
    Rm,
    /// Deadline Monotonic.
    Dm,
    /// Earliest Deadline First.
    Edf,
    /// Least Laxity First.
    Llf,
    /// First In First Out.
    Fifo,
    /// Priority Inheritance Protocol.
    Pip,
    /// Priority Ceiling Protocol.
    Pcp,
    /// Stack Resource Policy.
    Srp,
    Unknown,
}

pub fn parse_schedule_policy(policy: &str) -> SchedulePolicy {
    match policy {
        "fixed" => SchedulePolicy::Fixed,
        "fixed_prior_with_restart" => SchedulePolicy::FixedPriorWithRestart,
        "fixed_prior_with_resume" => SchedulePolicy::FixedPriorWithResume,
        "rm" => SchedulePolicy::Rm,
        "dm" => SchedulePolicy::Dm,
        "edf" => SchedulePolicy::Edf,
        "llf" => SchedulePolicy::Llf,
        "fifo" => SchedulePolicy::Fifo,
        "pip" => SchedulePolicy::Pip,
        "pcp" => SchedulePolicy::Pcp,
        "srp" => SchedulePolicy::Srp,
        _ => SchedulePolicy::Unknown,
    }
}

pub fn schedule_policy_to_string(policy: SchedulePolicy) -> &'static str {
    match policy {
        SchedulePolicy::Fixed => "fixed",
        SchedulePolicy::FixedPriorWithRestart => "fixed_prior_with_restart",
        SchedulePolicy::FixedPriorWithResume => "fixed_prior_with_resume",
        SchedulePolicy::Rm => "rm",
        SchedulePolicy::Dm => "dm",
        SchedulePolicy::Edf => "edf",
        SchedulePolicy::Llf => "llf",
        SchedulePolicy::Fifo => "fifo",
        SchedulePolicy::Pip => "pip",
        SchedulePolicy::Pcp => "pcp",
        SchedulePolicy::Srp => "srp",
        SchedulePolicy::Unknown => "unknown",
    }
}

/// A single task node from the TDG JSON.
#[derive(Debug, Clone)]
pub struct TaskNode {
    pub name: String,
    pub core: i32,
    pub priority: i32,
    /// Execution time intervals, one per segment: (lower, upper).
    pub time: Vec<(i32, i32)>,
    pub is_lock: bool,
    pub lock: Vec<String>,
    pub task_type: TaskType,
}

impl Default for TaskNode {
    fn default() -> Self {
        TaskNode {
            name: String::new(),
            core: 0,
            priority: 100,
            time: Vec::new(),
            is_lock: false,
            lock: Vec::new(),
            task_type: TaskType::Normal,
        }
    }
}

/// Fork/join are modelled as PTPN transitions. Default zero-time control
/// transitions on the control core (-1), overridable from JSON.
#[derive(Debug, Clone)]
pub struct ForkTask {
    pub name: String,
    pub time: (i32, i32),
    pub core: i32,
    pub priority: i32,
}

impl ForkTask {
    pub fn new(name: String) -> Self {
        ForkTask {
            name,
            time: (0, 0),
            core: -1,
            priority: 0,
        }
    }
}

#[derive(Debug, Clone)]
pub struct JoinTask {
    pub name: String,
    pub time: (i32, i32),
    pub core: i32,
    pub priority: i32,
}

impl JoinTask {
    pub fn new(name: String) -> Self {
        JoinTask {
            name,
            time: (0, 0),
            core: -1,
            priority: 0,
        }
    }
}

#[derive(Debug, Clone)]
pub struct EmptyTask {
    pub name: String,
}

/// Any node in the task dependency graph.
#[derive(Debug, Clone)]
pub enum NodeType {
    Task(TaskNode),
    Fork(ForkTask),
    Join(JoinTask),
    Empty(EmptyTask),
}

impl NodeType {
    pub fn name(&self) -> &str {
        match self {
            NodeType::Task(t) => &t.name,
            NodeType::Fork(f) => &f.name,
            NodeType::Join(j) => &j.name,
            NodeType::Empty(e) => &e.name,
        }
    }

    pub fn as_task(&self) -> Option<&TaskNode> {
        match self {
            NodeType::Task(t) => Some(t),
            _ => None,
        }
    }

    pub fn is_fork_or_join(&self) -> bool {
        matches!(self, NodeType::Fork(_) | NodeType::Join(_))
    }

    /// Matches `node_type_to_string` in the C++ source.
    pub fn type_string(&self) -> &'static str {
        match self {
            NodeType::Task(_) => "task",
            NodeType::Fork(_) => "fork",
            NodeType::Join(_) => "join",
            NodeType::Empty(_) => "empty",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TdgVertexType {
    Task,
    Fork,
    Join,
    Empty,
}

#[derive(Debug, Clone)]
pub struct TaskConfig {
    pub core: i32,
    pub priority: i32,
    pub times: Vec<(i32, i32)>,
    pub locks: Vec<String>,
}

#[derive(Debug, Clone)]
pub struct StartBinding {
    pub task: String,
    pub tokens: i32,
}

impl Default for StartBinding {
    fn default() -> Self {
        StartBinding {
            task: String::new(),
            tokens: 1,
        }
    }
}

#[derive(Debug, Clone)]
pub struct PeriodicBinding {
    pub task: String,
    pub period: i32,
}

impl Default for PeriodicBinding {
    fn default() -> Self {
        PeriodicBinding {
            task: String::new(),
            period: 0,
        }
    }
}

/// Directed edge in a task dependency graph, mirroring the JSON edge object.
#[derive(Debug, Clone)]
pub struct TdgEdge {
    pub source: String,
    pub target: String,
    pub label: String,
    pub style: String,
}

impl TdgEdge {
    pub fn is_self_loop(&self) -> bool {
        self.source == self.target
    }

    pub fn is_dashed(&self) -> bool {
        self.style.contains("dashed")
    }

    pub fn leaves(&self, node: &str) -> bool {
        self.source == node && self.target != node
    }

    pub fn enters(&self, node: &str) -> bool {
        self.target == node && self.source != node
    }
}

/// Lookup tables built while loading a TDG.
#[derive(Debug, Clone, Default)]
pub struct TdgLookup {
    pub tasks_priority: HashMap<String, i32>,
    pub vertexes_type: HashMap<String, TdgVertexType>,
    pub nodes_type: HashMap<String, NodeType>,
    pub tasks_type: HashMap<String, TaskType>,
    pub task_locks_map: HashMap<String, Vec<String>>,
}

impl fmt::Display for TaskType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}",
            match self {
                TaskType::Normal => "normal",
                TaskType::Period => "period",
                TaskType::Aperiod => "aperiod",
                TaskType::Interrupt => "interrupt",
            }
        )
    }
}
