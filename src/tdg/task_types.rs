//! TDG 任务类型定义
//!
//! 对应 C++ dag.h 中的结构体

/// 任务类型枚举
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TaskType {
    Normal,
    Period,
    Aperiod,
    Interrupt,
}

/// 非周期任务

#[derive(Debug, Clone)]
pub struct AperiodicTask {
    pub name: String,
    pub core: i32,
    pub priority: i32,
    pub time: Vec<(i32, i32)>,
    pub is_lock: bool,
    pub lock: Vec<String>,
    pub task_type: TaskType,
}

impl Default for AperiodicTask {
    fn default() -> Self {
        Self {
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

/// 周期任务
#[derive(Debug, Clone)]
pub struct PeriodicTask {
    pub name: String,
    pub core: i32,
    pub priority: i32,
    pub time: Vec<(i32, i32)>,
    pub is_lock: bool,
    pub lock: Vec<String>,
    pub task_type: TaskType,
    pub period_time: (i32, i32),
}

/// 分布任务
#[derive(Debug, Clone)]
pub struct DistTask {
    pub name: String,
    pub time: (i32, i32),
}

/// 同步任务
#[derive(Debug, Clone)]
pub struct SyncTask {
    pub name: String,
    pub time: (i32, i32),
}

/// 空任务
#[derive(Debug, Clone)]
pub struct EmptyTask {
    pub name: String,
}

/// 节点类型枚举
#[derive(Debug, Clone)]
pub enum NodeType {
    Periodic(PeriodicTask),
    Aperiodic(AperiodicTask),
    Dist(DistTask),
    Sync(SyncTask),
    Empty(EmptyTask),
}

impl NodeType {
    pub fn name(&self) -> &str {
        match self {
            NodeType::Periodic(t) => &t.name,
            NodeType::Aperiodic(t) => &t.name,
            NodeType::Dist(t) => &t.name,
            NodeType::Sync(t) => &t.name,
            NodeType::Empty(t) => &t.name,
        }
    }
}
