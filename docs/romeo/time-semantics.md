# Roméo Time Semantics

## Core claim

Roméo handles time symbolically rather than by advancing one global clock. In the timed path, it builds and transforms DBM-backed state-class or zone constraints over enabled transitions, and it decides timed firability and successor evolution by updating those constraints after each symbolic firing step.

## Computation mode selection

- `Job::find_cmode()` in `tools/romeo-2t/graph/job.cc` selects the computation mode.
- If `cts.has_time()` is true, the job is pushed onto a timed path rather than a purely untimed one.
- If the net also has parameters, the mode is upgraded toward parametric variants.
- `Job::initial_state()` then chooses the initial symbolic representation:
  - untimed path -> `VSState::init(*this)`
  - timed path -> `VSClass::init(*this)`

## Timed state representation

- Roméo does not attach one scalar timer to the whole net.
- It represents timing constraints symbolically through DBM-backed structures in the timed domain.
- `VSClass` provides the timed state-class entry representation, while `VZone` is one important timed-domain implementation site for successor-side timing operations.
- Transition timing is interpreted through each transition's `TimeInterval`, whose lower and upper bounds are translated into DBM constraints by calls such as `dbm_lower_bound(...)` and `dbm_upper_bound(...)`.

## Successor construction steps

A stable way to read the timed successor path is:

1. Fire a transition and update the marking plus valuations.
2. Recompute or remap the symbolic clock layout for the newly enabled set with `remap(...)`.
3. Reset clocks for newly enabled transitions with `reset(...)`.
4. Apply `future()` so time may elapse symbolically. This is the symbolic time-elapse step itself.
5. Re-intersect the zone with timing invariants and upper bounds using `dbm_upper_bound(...)`-driven constraints.
6. Apply abstraction such as `kxapprox` when that approximation step is enabled.

## Where time constraints are enforced

- Lower-bound admission is encoded with `dbm_lower_bound(...)` constraints during timed firability checks.
- Upper bounds are re-applied after successor construction with `dbm_upper_bound(...)` constraints.
- In `tools/romeo-2t/domains/vzone.cc`, the timed path visibly performs `remap(...)`, `reset(...)`, `future()`, then upper-bound re-constraining.
- The underlying DBM operations are provided by the DBM layer itself, where operations such as `future()`, `reset(...)`, `remap(...)`, and `kxapprox(...)` are implemented.

## Contrast with this repository's PTPN semantics

- Roméo computes timed behavior mainly through symbolic state-class or zone constraints over enabled transitions.
- This repository makes scheduler-facing semantic structure explicit in the state, especially `E`, `X`, and `R`.
- In this repository, `E` is the raw enabled set, `X` is the priority-filtered schedulable set, and `R` is the suspended set.
- Roméo instead hides most scheduling admissibility inside symbolic timing constraints and timed-domain firability logic.
- Practical consequence: Roméo's time semantics are centered on constraint transformation, while this repository surfaces scheduling and suspension structure as first-class semantic state.
