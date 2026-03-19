//! 状态类
//!
//! P-PTPN 中的状态类，含双 DBM (Z1/Z2)

use crate::dbm::{Dbm, INF_TIME};
use crate::ptpn::INF;
use serde::Serialize;
use std::collections::{BTreeSet, HashSet};
use std::fmt;

/// 状态类
#[derive(Clone)]
pub struct StateClass {
    pub marking: Vec<i32>,
    pub z1: Dbm,
    pub z2: Dbm,
    pub state_id: usize,
    pub cumulative_time: f64,
    pub enabled: HashSet<usize>,
    pub suspended: HashSet<usize>,
}

impl StateClass {
    pub fn new() -> Self {
        Self {
            marking: Vec::new(),
            z1: Dbm::new(0),
            z2: Dbm::new(0),
            state_id: 0,
            cumulative_time: 0.0,
            enabled: HashSet::new(),
            suspended: HashSet::new(),
        }
    }

    pub fn copy(&self) -> Self {
        Self {
            marking: self.marking.clone(),
            z1: self.z1.clone(),
            z2: self.z2.clone(),
            state_id: self.state_id,
            cumulative_time: self.cumulative_time,
            enabled: self.enabled.clone(),
            suspended: self.suspended.clone(),
        }
    }
}

impl Default for StateClass {
    fn default() -> Self {
        Self::new()
    }
}

impl PartialEq for StateClass {
    fn eq(&self, other: &Self) -> bool {
        self.marking == other.marking
            && self.z1 == other.z1
            && self.z2 == other.z2
            && self.enabled == other.enabled
            && self.suspended == other.suspended
    }
}

impl Eq for StateClass {}

impl PartialOrd for StateClass {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for StateClass {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.marking
            .cmp(&other.marking)
            .then_with(|| self.z1.cmp(&other.z1))
            .then_with(|| self.z2.cmp(&other.z2))
            .then_with(|| {
                let a: BTreeSet<_> = self.enabled.iter().collect();
                let b: BTreeSet<_> = other.enabled.iter().collect();
                a.cmp(&b)
            })
            .then_with(|| {
                let a: BTreeSet<_> = self.suspended.iter().collect();
                let b: BTreeSet<_> = other.suspended.iter().collect();
                a.cmp(&b)
            })
    }
}

impl std::hash::Hash for StateClass {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.marking.hash(state);
        for i in 0..self.z1.size() {
            for j in 0..self.z1.size() {
                self.z1.get_constraint(i, j).hash(state);
            }
        }
        for i in 0..self.z2.size() {
            for j in 0..self.z2.size() {
                self.z2.get_constraint(i, j).hash(state);
            }
        }
        let mut enabled: Vec<_> = self.enabled.iter().collect();
        enabled.sort();
        enabled.hash(state);
        let mut suspended: Vec<_> = self.suspended.iter().collect();
        suspended.sort();
        suspended.hash(state);
    }
}

impl fmt::Debug for StateClass {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "StateClass(id={}, time={})",
            self.state_id, self.cumulative_time
        )
    }
}

/// 变迁边
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct TransitionEdge {
    pub transition_id: i32,
    pub firing_time: f64,
}

impl TransitionEdge {
    pub fn new(transition_id: i32, firing_time: f64) -> Self {
        Self {
            transition_id,
            firing_time,
        }
    }
}
