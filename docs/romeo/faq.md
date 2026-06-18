# Roméo FAQ

## Where is `place` defined?

The visible `place` definition currently surfaced by this knowledge layer is the tutorial-level alias in `tools/romeo-2t/tutorial.cts`:

```cts
typedef int place;
```

This means `place` is not presented here as a central C++ engine class. It appears as part of the input-language layer used by the tutorial model, so this knowledge layer is not claiming it is a core-engine typedef.

## Where does Roméo really handle priority?

A practical way to answer this is to split priority into three layers:

1. Storage: `Transition.priority` in `tools/romeo-2t/model/transition.hh`
2. Runtime comparison: `Transition::has_priority_over(...)` in `tools/romeo-2t/model/transition.cc`
3. Timed symbolic enforcement: `VZone::firable(...)` in `tools/romeo-2t/domains/vzone.cc`

So priority is not only stored or compared; in the timed path it becomes effective when firability is filtered through symbolic timing constraints.

## Where does time actually advance?

Roméo does not advance one explicit global clock variable. In the timed symbolic path, time progression happens through zone/state-class transformation steps, especially in `tools/romeo-2t/domains/vzone.cc`:

- `remap(...)`
- `reset(...)`
- `future()`
- upper-bound re-constraining through `dbm_upper_bound(...)`

This is why Roméo's time semantics are best understood as symbolic constraint evolution rather than scalar clock ticking.

## Why can `VZone::firable` express priority scheduling?

Because it does more than test whether one transition satisfies its own lower bound.

For a candidate transition `ti`, `VZone::firable(...)`:
- constrains the DBM with `ti`'s own lower-bound condition;
- checks enabled competitors `tj`;
- if `tj` has higher dynamic priority, it adds the complement of `tj`'s lower-bound condition.

If those constraints make the DBM empty, then `ti` is not symbolically firable in that timed state. So higher-priority transitions can exclude lower-priority firings directly inside the timed-domain admissibility test.

## What is the main semantic difference from this repository's PTPN model?

The key difference is where scheduler structure is represented.

In this repository's PTPN semantics, scheduler-relevant structure is explicit in the state:
- `E`: enabled set
- `X`: priority-filtered schedulable set
- `R`: suspended set

In Roméo, priority and time admissibility are largely encoded implicitly inside symbolic timing constraints and firability logic rather than surfaced as first-class state components.

Practical consequence:
- this repository exposes scheduler structure directly for reasoning about suspend/freeze/resume behavior;
- Roméo concentrates that effect inside symbolic exploration machinery.
