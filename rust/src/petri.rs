//! PTPN (Priority Timed Petri Net) core data structures.

use std::cell::RefCell;
use std::collections::HashMap;
use std::fmt;

/// Sentinel used for "no upper bound" (+infinity) inside DBM matrices.
pub const INF: i32 = i32::MAX;

/// Core group for control transitions (the "control core").
pub const CONTROL_TRANSITION_CORE: i32 = -1;

/// Overflow recording: `fire` clamps every overflowing place to capacity, but a
/// NON-saturating place being clamped is an invalid behavior and is recorded so
/// the metrics layer can report it. Reset at the start of each build.
thread_local! {
    static OVERFLOW: RefCell<std::collections::BTreeSet<usize>> =
        RefCell::new(std::collections::BTreeSet::new());
}

pub fn reset_overflow_recording() {
    OVERFLOW.with(|o| o.borrow_mut().clear());
}

pub fn overflowed_places() -> Vec<usize> {
    OVERFLOW.with(|o| o.borrow().iter().copied().collect())
}

/// A time interval with optional open endpoints.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TimeInterval {
    pub earliest: i32,
    pub latest: i32,
    pub left_open: bool,
    pub right_open: bool,
}

impl TimeInterval {
    pub fn new(
        earliest: i32,
        latest: i32,
        left_open: bool,
        right_open: bool,
    ) -> Result<Self, String> {
        if earliest < 0 {
            return Err("earliest time must be non-negative".to_string());
        }
        if latest != INF && latest < earliest {
            return Err("latest time must be >= earliest time".to_string());
        }
        Ok(TimeInterval {
            earliest,
            latest,
            left_open,
            right_open,
        })
    }

    pub fn closed(earliest: i32, latest: i32) -> Self {
        TimeInterval {
            earliest,
            latest,
            left_open: false,
            right_open: false,
        }
    }

    pub fn effective_earliest(&self) -> i32 {
        if self.left_open {
            self.earliest + 1
        } else {
            self.earliest
        }
    }

    pub fn effective_latest(&self) -> i32 {
        if self.latest == INF {
            return INF;
        }
        if self.right_open {
            self.latest - 1
        } else {
            self.latest
        }
    }

    pub fn has_non_empty_integer_domain(&self) -> bool {
        self.effective_latest() == INF || self.effective_earliest() <= self.effective_latest()
    }

    pub fn is_valid(&self) -> bool {
        self.earliest >= 0
            && (self.latest == INF || self.latest >= self.earliest)
            && self.has_non_empty_integer_domain()
    }

    pub fn contains(&self, time: i32) -> bool {
        time >= self.effective_earliest()
            && (self.effective_latest() == INF || time <= self.effective_latest())
    }
}

impl fmt::Display for TimeInterval {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{}, {}{}",
            if self.left_open { "(" } else { "[" },
            self.earliest,
            if self.latest == INF {
                "∞".to_string()
            } else {
                self.latest.to_string()
            },
            if self.right_open { ")" } else { "]" }
        )
    }
}

#[derive(Debug, Clone)]
pub struct Place {
    pub id: String,
    pub name: String,
    pub capacity: i32,
    /// Saturating places absorb overflow: transitions producing into a full
    /// saturating place stay enabled, and the token count is clamped at
    /// capacity on firing.
    pub saturate: bool,
}

impl Place {
    pub fn new(id: String, name: String, capacity: i32, saturate: bool) -> Self {
        Place {
            id,
            name,
            capacity,
            saturate,
        }
    }
}

#[derive(Debug, Clone)]
pub struct Transition {
    pub id: String,
    pub name: String,
    pub time_interval: TimeInterval,
    pub priority: i32,
    pub core: i32,
    pub suspendable: bool,
}

/// A marking: token count per place.
pub type Marking = Vec<i32>;

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

/// A Priority Timed Petri Net.
///
/// Pre/Post are dense matrices with places in rows / transitions in columns;
/// sparse arc lists are kept in sync for fast enabling checks.
#[derive(Debug, Clone, Default)]
pub struct PTPN {
    pub places: Vec<Place>,
    pub transitions: Vec<Transition>,
    /// Pre[p][t] = weight of arc place p -> transition t.
    pub pre: Vec<Vec<i32>>,
    /// Post[t][p] = weight of arc transition t -> place p.
    pub post: Vec<Vec<i32>>,
    /// Sparse input arcs per transition: (place index, weight).
    pub pre_arcs: Vec<Vec<(usize, i32)>>,
    /// Sparse output arcs per transition: (place index, weight).
    pub post_arcs: Vec<Vec<(usize, i32)>>,
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
        let idx = self.places.len();
        self.places
            .push(Place::new(idx.to_string(), name.to_string(), capacity, saturate));
        self.pre.push(vec![0; self.transitions.len()]);
        self.m0.push(0);
        for row in self.post.iter_mut() {
            row.push(0);
        }
        idx
    }

    pub fn add_transition(
        &mut self,
        name: &str,
        interval: TimeInterval,
        priority: i32,
        core: i32,
        suspendable: bool,
    ) -> usize {
        let idx = self.transitions.len();
        self.transitions.push(Transition {
            id: idx.to_string(),
            name: name.to_string(),
            time_interval: interval,
            priority,
            core,
            suspendable,
        });
        for row in self.pre.iter_mut() {
            row.push(0);
        }
        self.post.push(vec![0; self.places.len()]);
        self.pre_arcs.push(Vec::new());
        self.post_arcs.push(Vec::new());
        idx
    }

    pub fn set_pre_arc(&mut self, place_idx: usize, trans_idx: usize, weight: i32) {
        assert!(
            place_idx < self.pre.len() && trans_idx < self.transitions.len(),
            "Invalid place or transition index"
        );
        self.pre[place_idx][trans_idx] = weight;
        self.rebuild_sparse_arcs();
    }

    pub fn set_post_arc(&mut self, trans_idx: usize, place_idx: usize, weight: i32) {
        assert!(
            trans_idx < self.post.len() && place_idx < self.places.len(),
            "Invalid transition or place index"
        );
        self.post[trans_idx][place_idx] = weight;
        self.rebuild_sparse_arcs();
    }

    pub fn set_initial_marking_vec(&mut self, marking: Marking) {
        assert!(
            marking.len() == self.places.len(),
            "Marking size must match number of places"
        );
        self.m0 = marking;
    }

    pub fn set_initial_marking(&mut self, place_idx: usize, tokens: i32) {
        assert!(place_idx < self.places.len(), "Invalid place index");
        assert!(tokens >= 0, "Token count cannot be negative");
        self.m0[place_idx] = tokens;
    }

    pub fn num_places(&self) -> usize {
        self.places.len()
    }

    pub fn num_transitions(&self) -> usize {
        self.transitions.len()
    }

    pub fn get_place(&self, idx: usize) -> &Place {
        &self.places[idx]
    }

    pub fn get_transition(&self, idx: usize) -> &Transition {
        &self.transitions[idx]
    }

    pub fn get_marking(&self) -> &Marking {
        &self.m0
    }

    pub fn is_enabled_static(&self, marking: &Marking, trans_idx: usize) -> bool {
        PTPN::is_enabled(marking, self, trans_idx)
    }

    /// Structural enabling: input-driven only (classic TPN semantics). A
    /// successor place never gates the transition; overflow on non-saturating
    /// places is clamped on firing and reported by the metrics layer.
    pub fn is_enabled(marking: &Marking, net: &PTPN, trans_idx: usize) -> bool {
        assert!(trans_idx < net.transitions.len(), "Invalid transition index");
        assert!(marking.len() == net.places.len(), "Marking size mismatch");

        for &(place_idx, weight) in &net.pre_arcs[trans_idx] {
            if marking[place_idx] < weight {
                return false;
            }
        }
        true
    }

    /// Fires a transition: consumes input tokens, produces output tokens with
    /// saturation clamping on saturating places.
    pub fn fire(marking: &Marking, net: &PTPN, trans_idx: usize) -> Marking {
        assert!(
            PTPN::is_enabled(marking, net, trans_idx),
            "Transition is not enabled"
        );
        let mut new_marking = marking.clone();

        for &(place_idx, weight) in &net.pre_arcs[trans_idx] {
            new_marking[place_idx] -= weight;
        }

        for &(place_idx, weight) in &net.post_arcs[trans_idx] {
            new_marking[place_idx] += weight;
            let place = &net.places[place_idx];
            if place.capacity != INF && new_marking[place_idx] > place.capacity {
                // Firing always happens (enabling is input-driven). Overflow is
                // clamped to capacity; a non-saturating place being clamped is
                // recorded as an invalid behavior by the metrics layer.
                if !place.saturate {
                    OVERFLOW.with(|o| o.borrow_mut().insert(place_idx));
                }
                new_marking[place_idx] = place.capacity;
            }
        }
        new_marking
    }

    pub fn fire_transition(&mut self, trans_idx: usize) {
        self.m0 = PTPN::fire(&self.m0, self, trans_idx);
    }

    pub fn get_enabled_transitions(&self) -> Vec<usize> {
        (0..self.transitions.len())
            .filter(|&t| self.is_enabled_static(&self.m0, t))
            .collect()
    }

    /// Parallelism bound of a core: 0 means "no bound" (control core / legacy).
    pub fn parallelism_of_core(&self, core_id: i32) -> i32 {
        if core_id < 0 {
            return 0;
        }
        self.core_parallelism.get(&core_id).copied().unwrap_or(0)
    }

    pub fn get_enabled_transitions_by_core(&self, core_id: i32) -> Vec<usize> {
        (0..self.transitions.len())
            .filter(|&t| self.transitions[t].core == core_id && self.is_enabled_static(&self.m0, t))
            .collect()
    }

    pub fn rebuild_sparse_arcs(&mut self) {
        self.pre_arcs = vec![Vec::new(); self.transitions.len()];
        self.post_arcs = vec![Vec::new(); self.transitions.len()];

        for (p, row) in self.pre.iter().enumerate() {
            for (t, &w) in row.iter().enumerate() {
                if w > 0 {
                    self.pre_arcs[t].push((p, w));
                }
            }
        }

        for (t, row) in self.post.iter().enumerate() {
            for (p, &w) in row.iter().enumerate() {
                if w > 0 {
                    self.post_arcs[t].push((p, w));
                }
            }
        }
    }

    pub fn verify_structure(&self) -> bool {
        let mut is_valid = true;

        if !self.pre.is_empty() && !self.transitions.is_empty() {
            let expected = self.transitions.len();
            for row in &self.pre {
                if row.len() != expected {
                    is_valid = false;
                }
            }
        }

        if !self.post.is_empty() && !self.places.is_empty() {
            let expected = self.places.len();
            for row in &self.post {
                if row.len() != expected {
                    is_valid = false;
                }
            }
        }

        if self.m0.len() != self.places.len() {
            is_valid = false;
        }

        for t in &self.transitions {
            if !t.time_interval.is_valid() {
                is_valid = false;
            }
        }

        is_valid
    }
}

impl fmt::Display for PTPN {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "=== PTPN ===")?;
        writeln!(f, "Places ({}):", self.places.len())?;
        for (i, p) in self.places.iter().enumerate() {
            writeln!(
                f,
                "  P{}: {} [capacity={}, tokens={}]",
                i,
                p.name,
                if p.capacity == INF {
                    "∞".to_string()
                } else {
                    p.capacity.to_string()
                },
                self.m0[i]
            )?;
        }
        writeln!(f, "\nTransitions ({}):", self.transitions.len())?;
        for (i, t) in self.transitions.iter().enumerate() {
            writeln!(
                f,
                "  T{}: {} [time={}, priority={}, core={}, suspendable={}]",
                i,
                t.name,
                t.time_interval,
                t.priority,
                t.core,
                if t.suspendable { "yes" } else { "no" }
            )?;
        }
        Ok(())
    }
}
