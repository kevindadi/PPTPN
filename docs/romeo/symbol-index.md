# Roméo Symbol Index

| Symbol | File | Why it matters |
| --- | --- | --- |
| `Transition.priority` | `tools/romeo-2t/model/transition.hh` | priority stored on each transition as an `SExpression` |
| `Transition::has_priority_over` | `tools/romeo-2t/model/transition.cc` | dynamic priority comparison at runtime |
| `VZone::firable` | `tools/romeo-2t/domains/vzone.cc` | encodes priority-aware firability as DBM constraints in one key timed-domain path |
| `Job::find_cmode` | `tools/romeo-2t/graph/job.cc` | selects the computation mode |
| `typedef int place;` | `tools/romeo-2t/tutorial.cts` | tutorial-level language alias showing `place` is not a central C++ class |

## Where is X defined?

- Priority storage lives in `tools/romeo-2t/model/transition.hh` on `Transition`.
- Runtime priority comparison lives in `tools/romeo-2t/model/transition.cc` as `Transition::has_priority_over`.
- Priority-aware timed firability lives in `tools/romeo-2t/domains/vzone.cc` as `VZone::firable`.
- Computation-mode selection lives in `tools/romeo-2t/graph/job.cc` as `Job::find_cmode`.
- The `place` alias appears in `tools/romeo-2t/tutorial.cts` as tutorial language syntax rather than a core engine type.

## Where does time advance?

- `tools/romeo-2t/domains/vzone.cc` advances symbolic time with `C.future()` in successor construction.
- The same file applies lower and upper timing bounds through `dbm_lower_bound(...)` and `dbm_upper_bound(...)` when checking or rebuilding zones.
- `tools/romeo-2t/graph/job.cc` decides whether exploration is untimed or timed via `Job::find_cmode`, which determines whether timed state machinery is used.

## Where does priority become effective?

In timed symbolic exploration, one key enforcement path is:
- Storage: `tools/romeo-2t/model/transition.hh`
- Runtime comparison: `tools/romeo-2t/model/transition.cc`
- Firability filtering: `tools/romeo-2t/domains/vzone.cc`
