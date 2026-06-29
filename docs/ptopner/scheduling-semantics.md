# PTPN Scheduling Semantics

## Core claim

PTPN scheduling semantics are explicit: every state class carries the
structurally enabled set (`struct_enabled`, E), the priority-filtered active set
(`priority_enabled`, X / E_pri), and the suspended set (`suspended`, R) as
first-class components. Priority filtering and suspension are operational set
operations, not encoded as implicit timed constraints.

## Formal sets

```
E_struct = { t | t is structurally enabled under marking M }
E_pri    = { t ∈ E_struct | pi(t) = max_{u ∈ E_struct, core(u)=core(t)} pi(u) }
R        = { t ∈ E_struct \ E_pri | suspendable(t) }
```

where `pi(t)` is the priority of transition `t` and `core(t)` is its core
attribute (taken straight from the JSON / `.ptpn` input).

### Per-core priority filtering (including the control core)

`E_pri` keeps, within every core group, only the structurally enabled
transitions with the maximal priority on that core (ties allowed). **The control
core (`core = -1`) is treated like any other group** — control transitions are
filtered by priority too, they are no longer "always kept".

Ordinary control transitions all share priority `0`, so they never spuriously
suppress one another. (The `fixed_prior_with_resume` policy no longer emits any
structural resume transition; preemption and resume are handled natively by the
engine, see below.)

### Suspension

A transition is *suspended* when it is structurally enabled, filtered out by the
priority comparison, and marked `suspendable`. Suspended transitions freeze their
execution clock (`h`) and advance a suspension clock (`w`). Control transitions
are never suspendable, so a filtered-out control transition is simply blocked for
that instant (neither active nor suspended) and becomes active again in the next
state class.

## Implementation

`src/analysis/scheduling.h` / `scheduling.cpp`:

```cpp
// E_struct(M)
static std::set<size_t> structural_enabled(const petri::PTPN& net,
                                           const petri::Marking& marking);

// E_pri(M): per-core max-priority filtering over every core group, -1 included
static std::set<size_t> filter_priority_per_core(
    const std::set<size_t>& struct_enabled, const petri::PTPN& net);
```

`filter_priority_per_core`:
1. Compute the max priority per core group keyed by `transition.core`
   (the `-1` group is not special-cased).
2. Keep every transition whose priority equals the max of its own core group.

## When these sets are recomputed

`StateClassReachabilityGraph::recompute_sets` (`src/analysis/ptpn_analysis.cpp`)
is called whenever the marking changes (in `compute_initial_class` and after each
`fire`). It:
1. Recomputes `struct_enabled` from the new marking (`Scheduling::structural_enabled`).
2. Computes `priority_enabled` via `Scheduling::filter_priority_per_core`.
3. Derives `suspended = { t ∈ struct_enabled \ priority_enabled | suspendable(t) }`.

`time_elapse` does NOT change these sets — it only pushes the symbolic clock zone
forward. Suspension state is recomputed only when the marking changes.

## Resume policy: engine-native preemption

The `fixed_prior_with_resume` policy (and the legacy `fixed` alias) relies on the
sets above to model preemption directly, instead of any structural encoding:

- It does NOT create the CPU-resource place. Per-core mutual exclusion and
  fixed-priority arbitration come from `filter_priority_per_core` alone.
- It does NOT generate the preempt/suspended/resume sub-net. A preempted
  execution segment is marked `suspendable`, so it lands in the suspended set,
  freezes its `h` clock, and resumes from the frozen value once the higher-
  priority task on its core finishes (`build_successor_zone` preserves the
  surviving clock). This permits mid-segment preemption.

The `fixed_prior_with_restart` policy and the PToPNer export path keep the
structural CPU place and structural preemption sub-net unchanged.

Known limitations of the resume model: same-core equal-priority transitions are
both kept (ties not mutually excluded), and spin-lock "hold the CPU" behavior is
only approximated by keeping spin-lock execution segments non-suspendable.

## Key files

- `src/analysis/scheduling.cpp` — `structural_enabled`, `filter_priority_per_core`
- `src/analysis/ptpn_analysis.cpp` — `recompute_sets`, `time_elapse`, `is_firable`, `fire`, `build`, `build_successor_zone`
- `src/tdg2pn/tdg2pn.cpp` — `is_resume_policy`, `add_resources_and_bindings_matrix`, `add_execution_chain`, `fixed_prior_with_restart`

## Contrast with Roméo

- Roméo keeps priority handling implicit inside timed symbolic firability checks
  in `VZone::firable`. A lower-priority transition is excluded from the symbolic
  firing set by DBM constraints.
- This repository makes the same effect explicit as set operations on the state
  class, enabling direct reasoning about which transitions are active, suspended,
  or blocked, and why.
- See `docs/romeo/priority-semantics.md` for the Roméo equivalent.

## Further reading

- Formal semantics: `unconfirmed/ptpn-formal-semantics.tex`
- State-class structure: `docs/ptopner/time-and-state-class.md`
- Roméo contrast: `docs/romeo/priority-semantics.md`
