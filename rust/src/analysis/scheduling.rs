//! Structural and priority enabling (port of `src/analysis/scheduling.cpp`).

use crate::analysis::state_class::{contains, TransitionSet};
use crate::petri::{Marking, PTPN};
use std::collections::HashMap;

pub struct Scheduling;

impl Scheduling {
    /// E_struct(M): transitions whose input places hold enough tokens. Sorted.
    pub fn structural_enabled(net: &PTPN, marking: &Marking) -> TransitionSet {
        let mut enabled = Vec::new();
        for t in 0..net.num_transitions() {
            if PTPN::is_enabled(marking, net, t) {
                enabled.push(t);
            }
        }
        enabled
    }

    /// E_pri(M): within every core group keep the highest-priority structurally
    /// enabled transitions. Bounded cores keep at most `capacity` transitions
    /// (highest priority first, ties by transition index); unbounded groups keep
    /// every transition sharing the maximal priority.
    pub fn filter_priority_per_core(struct_enabled: &TransitionSet, net: &PTPN) -> TransitionSet {
        let mut per_core: HashMap<i32, Vec<usize>> = HashMap::new();
        for &t in struct_enabled {
            per_core.entry(net.get_transition(t).core).or_default().push(t);
        }

        let mut active: TransitionSet = Vec::new();

        for (core, mut group) in per_core {
            let capacity = net.parallelism_of_core(core);

            if capacity <= 0 {
                // Unbounded group: keep every transition sharing the highest priority.
                let max_priority = group
                    .iter()
                    .map(|&t| net.get_transition(t).priority)
                    .max()
                    .unwrap_or(0);
                for &t in &group {
                    if net.get_transition(t).priority == max_priority {
                        active.push(t);
                    }
                }
                continue;
            }

            // Bounded resource: keep the highest-priority ones, ties by index.
            group.sort_by(|&a, &b| {
                let pa = net.get_transition(a).priority;
                let pb = net.get_transition(b).priority;
                pb.cmp(&pa).then(a.cmp(&b))
            });
            let keep = (capacity as usize).min(group.len());
            for &t in &group[..keep] {
                active.push(t);
            }
        }

        active.sort();
        active
    }

    /// Computes E_struct / E_pri / suspended for a marking.
    pub fn compute_sets(net: &PTPN, marking: &Marking) -> (TransitionSet, TransitionSet, TransitionSet) {
        let struct_enabled = Self::structural_enabled(net, marking);
        let priority_enabled = Self::filter_priority_per_core(&struct_enabled, net);

        let mut suspended = Vec::new();
        for &t in &struct_enabled {
            if contains(&priority_enabled, t) {
                continue;
            }
            if net.get_transition(t).suspendable {
                suspended.push(t);
            }
        }
        (struct_enabled, priority_enabled, suspended)
    }
}
