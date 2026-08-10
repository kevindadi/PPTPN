//! Symbolic state class (port of `src/analysis/state_class.h/.cpp`).

use crate::analysis::dbm::{DBM, INF_TIME};

/// Sorted vector of transition ids used as a lightweight ordered set.
pub type TransitionSet = Vec<usize>;

pub fn contains(set: &TransitionSet, value: usize) -> bool {
    set.binary_search(&value).is_ok()
}

/// Every column/row of the joint DBM corresponds to one timed variable.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum ClockKind {
    /// The reference variable x0 (always lives at index 0).
    Zero,
    /// h_t: how long transition t has effectively been running.
    Execution,
    /// w_t: how long suspendable transition t has been suspended.
    Suspension,
}

/// Maps a DBM column index back to the variable it represents.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct ClockVar {
    pub kind: ClockKind,
    pub transition: usize,
}

/// One edge of the state-class graph: which transition fired, plus the feasible
/// firing window of its execution clock h_t at the moment it fires.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FiringEdge {
    pub transition_id: i32,
    pub firing_min: i32,
    pub firing_max: i32,
    /// Global time the net may spend in the source class before this firing.
    pub dwell_min: i32,
    pub dwell_max: i32,
}

impl FiringEdge {
    pub fn new(transition_id: i32, firing_min: i32, firing_max: i32) -> Self {
        FiringEdge {
            transition_id,
            firing_min,
            firing_max,
            dwell_min: 0,
            dwell_max: 0,
        }
    }

    pub fn to_string(&self) -> String {
        format!(
            "T{}@[{}, {}]",
            self.transition_id,
            self.firing_min,
            if self.firing_max == INF_TIME {
                "inf".to_string()
            } else {
                self.firing_max.to_string()
            }
        )
    }
}

/// Symbolic state class C = (M, Omega) together with the scheduler-facing sets.
#[derive(Debug, Clone)]
pub struct StateClass {
    pub marking: Vec<i32>,
    pub zone: DBM,
    /// index -> variable; [0] is Zero.
    pub clock_vars: Vec<ClockVar>,
    /// transition -> h_t index or -1.
    pub exec_clock_of_transition: Vec<i32>,
    /// transition -> w_t index or -1.
    pub susp_clock_of_transition: Vec<i32>,
    pub struct_enabled: TransitionSet,
    pub priority_enabled: TransitionSet,
    pub suspended: TransitionSet,
    /// Auxiliary metadata, excluded from identity.
    pub elapsed_time: f64,
    pub id: usize,
}

impl Default for StateClass {
    fn default() -> Self {
        StateClass {
            marking: Vec::new(),
            zone: DBM::new(0),
            clock_vars: Vec::new(),
            exec_clock_of_transition: Vec::new(),
            susp_clock_of_transition: Vec::new(),
            struct_enabled: Vec::new(),
            priority_enabled: Vec::new(),
            suspended: Vec::new(),
            elapsed_time: 0.0,
            id: 0,
        }
    }
}

impl StateClass {
    pub fn exec_index(&self, transition: usize) -> i32 {
        if transition < self.exec_clock_of_transition.len() {
            self.exec_clock_of_transition[transition]
        } else {
            -1
        }
    }

    pub fn susp_index(&self, transition: usize) -> i32 {
        if transition < self.susp_clock_of_transition.len() {
            self.susp_clock_of_transition[transition]
        } else {
            -1
        }
    }

    pub fn has_exec_clock(&self, transition: usize) -> bool {
        self.exec_index(transition) > 0
    }

    pub fn has_susp_clock(&self, transition: usize) -> bool {
        self.susp_index(transition) > 0
    }
}

/// Hashes the marking alone so candidates that might merge land in one bucket.
pub struct MarkingHasher {
    #[allow(dead_code)]
    seed: u64,
}

impl MarkingHasher {
    pub fn hash_marking(marking: &[i32]) -> u64 {
        let mut seed: u64 = 0;
        for value in marking {
            hash_combine(&mut seed, *value as u64);
        }
        seed
    }
}

pub fn hash_combine(seed: &mut u64, value: u64) {
    *seed ^= value
        .wrapping_add(0x9e3779b97f4a7c15u64)
        .wrapping_add(seed.wrapping_shl(6))
        .wrapping_add(seed.wrapping_shr(2));
}

/// Exact-identity hash over (marking, clock_vars, zone matrix).
pub fn hash_state_class(state: &StateClass) -> u64 {
    let mut seed: u64 = 0;
    for value in &state.marking {
        hash_combine(&mut seed, *value as u64);
    }
    for var in &state.clock_vars {
        hash_combine(&mut seed, var.kind as u64);
        hash_combine(&mut seed, var.transition as u64);
    }
    for value in state.zone.raw_matrix() {
        hash_combine(&mut seed, *value as u64);
    }
    seed
}
