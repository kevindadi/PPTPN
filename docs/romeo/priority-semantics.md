# Roméo Priority Semantics

## Core claim

Roméo does not treat priority as a separate explicit scheduler set. It stores priority on transitions, evaluates it dynamically under the current valuation, and makes lower-priority firings impossible by adding priority-sensitive timing constraints during timed firability checks.

## Where priority lives

- Priority is stored on `Transition` in `tools/romeo-2t/model/transition.hh`.
- The field is `Transition.priority`.
- Its type is `SExpression`, so priority is part of the transition's evaluable semantic payload rather than a fixed metadata integer.

## How priority is evaluated

- `Transition::has_priority_over(...)` in `tools/romeo-2t/model/transition.cc` performs runtime priority comparison.
- `Transition.priority` is an `SExpression`, not a fixed integer field.
- The function evaluates both transitions' priority expressions under the current valuation/state.
- Missing priority behaves like negative infinity via `INT64_MIN`, so any explicit priority dominates a null priority.

## Where priority blocks firing

- `VZone::firable(unsigned i)` in `tools/romeo-2t/domains/vzone.cc` first checks the candidate transition's own firing conditions.
- It then adds the candidate transition's lower-bound constraint to a temporary DBM.
- For each competing enabled transition `tj` with higher dynamic priority, it adds the complement of `tj`'s lower-bound constraint.
- If the resulting DBM is empty, the candidate lower-priority transition is not symbolically firable in that timed state.
- In plain language: once a higher-priority enabled transition is already allowed by the current timing constraints, the lower-priority candidate is excluded from the timed firing set.
- This makes priority effective inside timed symbolic admissibility, not as a separate explicit `X`-style scheduler set.

## Difference from this repository's scheduler semantics

This section describes a scheduler-representation difference, not a claim that the two systems are globally semantically identical or inequivalent.

- Roméo keeps priority handling implicit inside timed symbolic firability logic.
- This repository makes scheduler-facing structure explicit through sets such as `E`, `X`, and `R`.
- In this repository, priority filtering is surfaced as a first-class semantic step over enabled transitions.
- In Roméo, the same effect is largely encoded by dynamic comparison plus constraint-based exclusion inside the timed domain.
