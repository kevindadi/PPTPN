//! PTPN 核心模块
//!
//! 矩阵形式和图形式的优先级时间 Petri 网

mod graph;
mod matrix;
mod time_interval;

pub use graph::{matrix_to_graph, save_to_dot, Edge, PtpnGraph, Vertex};
pub use matrix::{Marking, MatrixPTPN, Place, Transition};
pub use time_interval::{TimeInterval, INF};
