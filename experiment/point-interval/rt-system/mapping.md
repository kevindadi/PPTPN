# Mapping notes

## Source net inventory

### Places

| PToPNer id | name | initial tokens |
| --- | --- | --- |
| 0 | c1 | 2 |
| 1 | p1 | 1 |
| 2 | p2 | 0 |
| 3 | p3 | 0 |
| 4 | p4 | 0 |
| 5 | p5 | 1 |
| 6 | p6 | 0 |
| 7 | p7 | 0 |
| 8 | p8 | 0 |
| 9 | p9 | 1 |
| 10 | p10 | 0 |
| 11 | p11 | 0 |
| 12 | p12 | 0 |

### Transitions

| transition | preset | postset | time | prior | is_suspend |
| --- | --- | --- | --- | --- | --- |
| t1 | p1 | p2 | 10 | 0 | 0 |
| t2 | c1,p2 | p3 | 0 | 1 | 0 |
| t3 | p3 | c1,p4 | 30 | 0 | 1 |
| t4 | p5 | p6 | 20 | 0 | 0 |
| t5 | c1,p6 | p7 | 0 | 2.1 | 0 |
| t6 | p3,p6 | p2,p7 | 0 | 2.0 | 0 |
| t7 | p7 | c1,p8 | 25 | 0 | 1 |
| t8 | p9 | p10 | 30 | 0 | 0 |
| t9 | c1,p10 | p11 | 0 | 3.2 | 0 |
| t10 | p3,p10 | p2,p11 | 0 | 3.1 | 0 |
| t11 | p7,p10 | p6,p11 | 0 | 3.0 | 0 |
| t12 | p11 | c1,p12 | 20 | 0 | 0 |

## Romeo mapping

- Places and transitions keep the same names.
- Every transition uses `eft == lft == time`.
- Every Petri-net arc is copied directly.
- The file keeps `c1` as a normal place with initial marking `2`.
- No claim is made that Romeo natively captures `is_suspend`; this experiment treats that as a documented caveat.

## Priority JSON mapping

The repository input is a TDG, not a direct Petri net, so the JSON file is structural approximation.

### Conventions

- Every PToPNer transition becomes one task node with the same id.
- Priorities are scaled to integers:
  - `0` -> `1`
  - `1` -> `10`
  - `2.0` -> `20`
  - `2.1` -> `21`
  - `3.0` -> `30`
  - `3.1` -> `31`
  - `3.2` -> `32`
- Fixed times become `[[x, x]]`.
- All tasks are placed on core `0` so the approximation keeps priority competition visible.
- Initial tokens on `p1`, `p5`, `p9` become `configuration.start` bindings for `t1`, `t4`, `t8`.
- Multi-input transitions are approximated with multiple predecessor edges.
- Token-return behavior through places like `p2`, `p6`, and `c1` is approximated by feedback edges rather than explicit place markings.

### Important limitation

The JSON approximation cannot strictly represent:

- explicit place capacities and token counts
- transition-level suspend/resume semantics
- the exact enabling interplay created by `c1`

It should therefore be read as a state-space comparison scaffold for this repository's pipeline, not as a mathematically exact encoding of the original `.ppn` net.
