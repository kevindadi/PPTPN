//! Scheduling metrics derived from the state-class reachability graph.
//!
//! The state-class (DBM) analysis lives in UniPN; this module is the
//! real-time-scheduling metrics layer on top of it (WCET/deadline/utilisation).

pub mod metrics;

pub use metrics::{MetricsAnalyzer, MetricsReport};

// Re-export the core timed-net analysis so PTPN consumers have one import path.
pub use unipn::analysis::timed::{
    CanonicalizationMode, StateClassGraph, StateClassReachabilityGraph, Statistics,
    TimedReachabilityConfig,
};
