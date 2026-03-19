//! TDG (Task Dependency Graph) 解析模块
//!
//! 从 DOT 格式解析任务依赖图，支持周期任务、非周期任务等

mod dot_parser;
mod task_types;

pub use dot_parser::{
    classify_priority, parse_dot_content, parse_dot_file, parse_time_vec, parse_vertex_label,
    EdgeInfo, Tdg, TdgVertexType, TaskConfig,
};
pub use task_types::{
    AperiodicTask, DistTask, EmptyTask, NodeType, PeriodicTask, SyncTask, TaskType,
};
