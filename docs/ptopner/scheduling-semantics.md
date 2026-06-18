# PTPN Scheduling Semantics

## Core claim

PTPN scheduling semantics are explicit: every state carries the raw enabled set (E), the priority-filtered active set (X), and the suspended set (R) as first-class components. The formal state is `S = (M, C, E, X, R, Θ)` where M is the marking, C is the DBM clock zone, and Θ is cumulative elapsed time. Priority filtering and suspension are operational steps over set operations, not encoded as implicit timed constraints.

## Formal state

See `docs/ptpn-formal-semantics.md` for the full definition. The key semantic invariant is:

```
E = { t | t is enabled under marking M }
X = { t ∈ F | pi(t) = max_{u ∈ F_k} pi(u) }   (per core k ≥ 0; control kept as-is)
R = { t ∈ E \ X | suspendable(t) ∧ ∃u ∈ X with same_core(u,t) ∧ pi(u) > pi(t) }
```

where F is the firing set (time-filtered enabled transitions) and pi(t) is the priority of transition t.

## Priority filtering: E → X

`src/analysis/scheduling.h:26` — `SchedulingAlgorithms::select_active_per_core`:

```cpp
// For each core k ≥ 0, keep all enabled transitions with maximal priority on that core.
// Control transitions (core < 0) are always kept.
static std::set<size_t> select_active_per_core(const std::set<size_t>& enabled,
                                                const petri::PTPN& ptpn);
```

Implementation (`src/analysis/scheduling.cpp:7`):
1. Compute `per_core_max_priority` — for each core, the maximum priority among enabled transitions.
2. Return all enabled transitions t where `t.core < 0` (control) OR `t.priority == per_core_max_priority[t.core]`.

The "highest priority per core" may return multiple transitions (ties allowed).

## Suspension: E \ X → R

`src/analysis/scheduling.h:49` — `SchedulingAlgorithms::compute_suspended`:

```cpp
static std::set<size_t> compute_suspended(const std::set<size_t>& enabled,
                                         const std::set<size_t>& active,
                                         const petri::PTPN& ptpn);
```

`should_suspend` (`src/analysis/scheduling.cpp:78`): transition t suspends if:
1. `t.suspendable == true`
2. `t.core >= 0` (on a real core, not a control transition)
3. There exists `u ∈ active` on the same core with higher priority

`should_restore` (`src/analysis/scheduling.cpp:91`): a suspended transition resumes when there is no higher-priority active transition on the same core.

## When these sets are recomputed

`src/analysis/graph.h:118` — `StateClassReachabilityGraph::recompute_enabled_sets`:

Called after every marking change (firing). It:
1. Recomputes E from the new marking (using `PTPN::is_enabled`)
2. Calls `select_active_per_core` to get X
3. Calls `compute_suspended` to get R

Time advancement (`advance_time`) does NOT change E/X/R — it only pushes clocks forward. Suspension state is recomputed only when the marking changes.

## Key files

- `src/analysis/scheduling.cpp:7` — `select_active_per_core` implementation
- `src/analysis/scheduling.cpp:60` — `compute_suspended` implementation
- `src/analysis/scheduling.cpp:78` — `should_suspend` / `should_restore`
- `src/analysis/graph.h:138` — `select_active_per_core` public declaration
- `src/analysis/graph.h:154` — `compute_suspended` public declaration

## Contrast with Roméo

- Roméo keeps priority handling implicit inside timed symbolic firability checks in `VZone::firable`. A lower-priority transition is excluded from the symbolic firing set by DBM constraints.
- This repository makes the same effect available as explicit set operations on `StateClass`, enabling direct reasoning about which transitions are active, which are suspended, and why.
- See `docs/romeo/priority-semantics.md` for the Roméo equivalent.

## Further reading

- Full formal semantics: `docs/ptpn-formal-semantics.md` — especially the "先筛选 F, 再在 F 上按 §5 取每核最高优先级集合（可并列）得到 X'" rule
- State-class structure: `docs/ptopner/time-and-state-class.md`
- Roméo contrast: `docs/romeo/priority-semantics.md`
