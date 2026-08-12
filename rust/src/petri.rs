//! PTPN model: the unified [`TimedNet`] plus TDG-lowering metadata.
//!
//! The model itself lives in UniPN (`unipn::TimedNet`); this module re-exports
//! the shared types and keeps the TDG-specific lowering metadata (task info,
//! node/chain maps, core/lock resource indices, parallelism bounds).

use std::collections::HashMap;

use unipn::net::ArcDir;
use unipn::ids::{PlaceId, TransitionId};

pub use unipn::{
    reset_overflow_recording, overflowed_places, CONTROL_TRANSITION_CORE, INF, Marking,
    TimeInterval, TimedNet, TimedPlaceKind, TimedTransitionKind,
};

/// Per-task scheduling metadata attached to the lowered net so the metrics
/// layer can reason about deadlines/periods/utilisation.
#[derive(Debug, Clone, Default)]
pub struct TaskInfo {
    pub core: i32,
    pub priority: i32,
    pub wcet: i32,
    pub bcet: i32,
    pub period: i32,
    pub deadline: i32,
    pub locks: Vec<String>,
}

/// A Priority Timed Petri Net: the unified [`TimedNet`] model plus the metadata
/// the TDG lowering needs (and the metrics layer consumes).
#[derive(Debug, Clone, Default)]
pub struct PTPN {
    pub net: TimedNet,
    pub m0: Marking,
    /// name -> (start, end) indices of each node fragment.
    pub node_start_end_map: HashMap<String, (usize, usize)>,
    /// name -> place/transition chain for each task.
    pub node_pn_map: HashMap<String, Vec<usize>>,
    pub cpus_place: Vec<usize>,
    pub locks_place: HashMap<String, usize>,
    /// Maximum simultaneous transitions per core (parallelism bound).
    pub core_parallelism: HashMap<i32, i32>,
    pub task_info: HashMap<String, TaskInfo>,
    pub node_index: i32,
}

impl PTPN {
    pub fn new() -> Self {
        PTPN::default()
    }

    pub fn add_place(&mut self, name: &str, capacity: i32, saturate: bool) -> usize {
        let id = self.net.add_place(
            name,
            TimedPlaceKind {
                capacity: if capacity == INF {
                    None
                } else {
                    Some(capacity as usize)
                },
                saturate,
            },
        );
        self.m0.set(id, 0);
        id.index()
    }

    pub fn add_transition(
        &mut self,
        name: &str,
        interval: TimeInterval,
        priority: i32,
        core: i32,
        suspendable: bool,
    ) -> usize {
        let id = self.net.add_transition(
            name,
            TimedTransitionKind {
                interval,
                priority,
                core,
                suspendable,
            },
        );
        id.index()
    }

    pub fn set_pre_arc(&mut self, place_idx: usize, trans_idx: usize, weight: i32) {
        self.net
            .add_arc(PlaceId(place_idx), TransitionId(trans_idx), ArcDir::Input, weight as usize, ());
    }

    pub fn set_post_arc(&mut self, trans_idx: usize, place_idx: usize, weight: i32) {
        self.net.add_arc(
            PlaceId(place_idx),
            TransitionId(trans_idx),
            ArcDir::Output,
            weight as usize,
            (),
        );
    }

    pub fn set_initial_marking_vec(&mut self, marking: Vec<i32>) {
        self.m0 = Marking::new(marking.into_iter().map(|t| t as usize).collect());
    }

    pub fn set_initial_marking(&mut self, place_idx: usize, tokens: i32) {
        self.m0.set(PlaceId(place_idx), tokens as usize);
    }

    pub fn num_places(&self) -> usize {
        self.net.num_places()
    }

    pub fn num_transitions(&self) -> usize {
        self.net.num_transitions()
    }

    pub fn get_place(&self, idx: usize) -> &unipn::Place<TimedPlaceKind> {
        &self.net.places[idx]
    }

    pub fn get_transition(&self, idx: usize) -> &unipn::Transition<TimedTransitionKind> {
        &self.net.transitions[idx]
    }

    pub fn get_marking(&self) -> &Marking {
        &self.m0
    }

    pub fn is_enabled(marking: &Marking, net: &PTPN, trans_idx: usize) -> bool {
        net.net.is_enabled(marking, TransitionId(trans_idx))
    }

    pub fn fire(marking: &Marking, net: &PTPN, trans_idx: usize) -> Marking {
        net.net.fire(marking, TransitionId(trans_idx))
    }

    pub fn parallelism_of_core(&self, core_id: i32) -> i32 {
        if core_id < 0 {
            return 0;
        }
        self.core_parallelism.get(&core_id).copied().unwrap_or(0)
    }

    pub fn verify_structure(&self) -> bool {
        self.m0.len() == self.net.num_places()
    }
}
