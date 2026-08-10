//! PTPN — Priority Timed Petri Net analyzer.
//!
//! Rust port of the C++ PTPN tool. Accepts two input modes:
//!   1. TDG mode:  Task Dependency Graph JSON -> TDG -> PTPN -> state-class analysis
//!   2. Direct mode: `.ptpn` domain language -> PTPN -> state-class analysis
//!
//! The analysis builds a state-class reachability graph over DBM (Difference
//! Bound Matrix) clock zones with configurable canonicalization.

// The port keeps the C++ index-computation style (`0 * n + i`) verbatim for
// auditable parity; these clippy lints are stylistic only.
#![allow(clippy::identity_op, clippy::erasing_op)]

pub mod analysis;
pub mod json;
pub mod parser;
pub mod petri;
pub mod tdg;
pub mod tdg2pn;
pub mod types;
