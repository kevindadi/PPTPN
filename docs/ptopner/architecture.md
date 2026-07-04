# PTPN / PToPNer Architecture

## Core claim

This repository has two main entry points: `ptpn tdg` (JSON TDG → PTPN analysis → optional .ppn export) and `ptpn ptpn` (PTPN → analysis → export). All PTPN semantics — state classes, time, scheduling, and suspension — are implemented in `src/analysis/`. PToPNer export is a separate, constrained lowering path.

## CLI entry

`src/main.cpp` defines the top-level CLI via CLI11. Two subcommands are relevant:

- **`ptpn tdg <json>`** — JSON TDG input
- **`ptpn ptpn <ptpn-source>`** — PTPN source input

Both ultimately route to `run_ptpn_analysis` (for the analysis pipeline) after optional TDG→PTPN lowering.

## TDG → PTPN lowering

```cpp
// src/main.cpp:248
converter::TDG2PN::transform(tdg, ptpn);
```

This is implemented in `src/tdg2pn/tdg2pn.h` and `src/tdg2pn/tdg2pn.cpp`. It converts a task-dependency graph (from JSON) into a PTPN. Full lowering rules are in `docs/rule.md`.

## PTPN core model

`src/petri/petri.h` defines the PTPN net:

```cpp
struct Transition {
  TimeInterval time_interval;  // [earliest, latest]
  int priority;                 // higher value = higher priority
  int core;                     // -1 = control, >= 0 = core id
  bool suspendable;              // whether this transition can be suspended
};
```

Places hold tokens; a `Marking` is a `vector<int>`. Full formal semantics are in `docs/ptpn-formal-semantics.md`.

## State-class reachability analysis

```cpp
// src/main.cpp:136
state_class::StateClassReachabilityGraph reachability_graph(ptpn);
reachability_graph.set_canonicalization_mode(canonicalization);
reachability_graph.build(opts.max_states);
```

`src/analysis/graph.h` defines `StateClassReachabilityGraph`, which drives timed exploration:

- `build()` — construct the reachability graph
- `advance_time()` — push all active clocks forward (min of active upper bounds)
- `fire_with_time()` — fire a transition at a specific time, update marking and clocks
- `recompute_enabled_sets()` — recompute enabled/active/suspended after a marking change

## Scheduling and suspension

`src/analysis/scheduling.h` and `src/analysis/scheduling.cpp` define `SchedulingAlgorithms`:

- `select_active_per_core()` — for each core k≥0, keep all enabled transitions with maximal priority on that core (control transitions, core < 0, are kept as-is)
- `compute_suspended()` — transitions that are enabled but not active and have a higher-priority active transition on the same core are suspended
- `should_suspend()` / `should_restore()` — per-transition suspension judgment

## PToPNer export

```cpp
// src/main.cpp:230
const auto ppn_validation = ptopner_export::validate_for_ptopner(tdg);

// src/main.cpp:116
const auto ppn_export = ptopner_export::export_ptpn_to_ppn_file(ptpn, opts.ppn_file);
```

Validation and export live in `src/tdg2ptopner/validate.cpp` and `src/tdg2ptopner/tdg2ptopner.cpp`. The export is constrained: point intervals only, no locks, and `fixed_prior_with_restart` policy.

## Key file map

| File | Role |
|---|---|
| `src/main.cpp` | CLI entry, pipeline orchestration |
| `src/petri/petri.h` | PTPN net model: Place, Transition, Marking, is_enabled, fire |
| `src/analysis/graph.h` | StateClassReachabilityGraph: build, advance_time, fire_with_time, recompute_enabled_sets |
| `src/analysis/state.h` | StateClass: marking, clocks, zone, enabled, active, suspended |
| `src/analysis/scheduling.h/.cpp` | SchedulingAlgorithms: select_active_per_core, compute_suspended, should_suspend/should_restore |
| `src/tdg2pn/tdg2pn.h/.cpp` | TDG → PTPN lowering |
| `src/tdg2ptopner/validate.cpp` | PToPNer validation: point intervals, no locks, fixed_prior_with_restart |
| `src/tdg2ptopner/tdg2ptopner.cpp` | PTPN → .ppn export |

## Further reading

- Full formal semantics: `docs/ptpn-formal-semantics.md`
- TDG → PTPN lowering rules: `docs/rule.md`
- Input format reference: `docs/json_format.md`
- PToPNer external tool: `tools/PToPNer/README.md`
- Contrast with Roméo timing: `docs/romeo/time-semantics.md`, `docs/romeo/priority-semantics.md`
