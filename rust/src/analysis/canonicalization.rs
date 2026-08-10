//! State-class canonicalization modes (port of `src/analysis/canonicalization.cpp`).

use crate::analysis::state_class::StateClass;

/// How freshly computed state classes are matched against already discovered
/// ones. EQUALITY keeps the graph exact; the other two apply the zone-inclusion
/// abstraction.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CanonicalizationMode {
    /// Identical marking, layout and DBM matrix.
    Equality,
    /// Zone-inclusion abstraction.
    MaxLowerBound,
    /// Zone-inclusion abstraction.
    Intersection,
}

fn same_layout(a: &StateClass, b: &StateClass) -> bool {
    a.marking == b.marking && a.clock_vars == b.clock_vars
}

/// Exact match: same marking, same variable layout, identical DBM matrix.
pub fn check_equality(a: &StateClass, b: &StateClass) -> bool {
    if !same_layout(a, b) {
        return false;
    }
    a.zone.raw_matrix() == b.zone.raw_matrix()
}

/// Inclusion: same marking and layout, and a's zone is a subset of b's zone.
pub fn check_inclusion(a: &StateClass, b: &StateClass) -> bool {
    if !same_layout(a, b) {
        return false;
    }
    a.zone.included_in(&b.zone)
}

/// True when `candidate` may be merged into the already-discovered `existing`.
pub fn can_merge_into(
    candidate: &StateClass,
    existing: &StateClass,
    mode: CanonicalizationMode,
) -> bool {
    match mode {
        CanonicalizationMode::Equality => check_equality(candidate, existing),
        CanonicalizationMode::MaxLowerBound | CanonicalizationMode::Intersection => {
            check_inclusion(candidate, existing)
        }
    }
}
