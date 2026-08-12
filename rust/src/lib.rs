//! PTPN — Priority Timed Petri Net analyzer.
//!
//! Rust port of the C++ PTPN tool. Accepts two input modes:
//!   1. TDG mode:  Task Dependency Graph JSON -> TDG -> PTPN -> state-class analysis
//!   2. Direct mode: `.ptpn` domain language -> PTPN -> state-class analysis
//!
//! The timed-net **model** (`TimedNet`) and its **analysis** (DBM/state-class
//! reachability) live in UniPN; this crate keeps the TDG lowering, the `.ptpn`
//! parser, the CLI, and the scheduling metrics.

pub mod analysis;
pub mod json;
pub mod parser;
pub mod petri;
pub mod tdg;
pub mod tdg2pn;
pub mod types;

pub use petri::{PTPN, TaskInfo};
