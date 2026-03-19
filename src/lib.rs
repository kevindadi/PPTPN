//! 优先级时间 Petri 网 (P-PTPN)
//!
//! 支持 TDG 解析、矩阵/图形式 PTPN、DBM、状态类可达图

pub mod dbm;
pub mod error;
pub mod ptpn;
pub mod reachability;
pub mod state_class;
pub mod tdg;

pub use dbm::Dbm;
pub use error::{DbmError, PtpnError, PtpnErrorKind, ReachabilityError, TdgParseError};
pub use ptpn::{matrix_to_graph, save_to_dot, MatrixPTPN, PtpnGraph, TimeInterval, INF};
pub use reachability::{ReachabilityStats, StateClassReachabilityGraph};
pub use state_class::{StateClass, TransitionEdge};
pub use tdg::{parse_dot_file, Tdg};
