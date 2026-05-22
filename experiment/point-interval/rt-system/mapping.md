# Mapping notes

## Known lineage

This experiment starts from the original DAG in [example/common/input.json](../../../example/common/input.json), and then compares two later hand-modeled Petri-net artifacts built around the same real-time dependency motivation:

- [example/common/input.json](../../../example/common/input.json): original DAG-style task model
- [tools/PToPNer/examples/RT-System.ppn](../../../tools/PToPNer/examples/RT-System.ppn): manually modeled PToPNer Petri net
- [romeo/RT-System.xml](romeo/RT-System.xml): author-provided Romeo artifact copied from [hlf-ptopn-100.xml](../../../hlf-ptopn-100.xml)

The important consequence is:

- the original dependency pattern is authoritative on the DAG side
- the `.ppn` and Romeo XML are authoritative on their own tool sides
- the detailed manual modeling steps that connect them were not documented by the author

## Original DAG inventory

### Tasks

| DAG task | priority | core | time | starts with token | periodic |
| --- | --- | --- | --- | --- | --- |
| A | 97 | 0 | [3, 8] | yes | 100 |
| B | 98 | 0 | [3, 5] | no | - |
| C | 99 | 1 | [8, 10] | no | - |
| D | 97 | 1 | [6, 8] | yes | 50 |
| E | 98 | 1 | [15, 18] | no | - |
| F | 96 | 0 | [3, 3] | no | - |

### Dependencies

| source | target | style |
| --- | --- | --- |
| A | B | normal |
| B | C | normal |
| D | E | normal |
| E | C | normal |
| D | F | dashed |

This is the dependency structure that motivated the later Petri-net modeling work.

## Experiment-local JSON mapping

[priority/input.json](priority/input.json) now stays on the DAG side instead of trying to mimic Petri-net internals.

### What is preserved directly from `example/common/input.json`

- task ids `A` through `F`
- dependency edges `A->B->C`, `D->E->C`, and `D->F`
- dashed style on `D->F`
- start tasks `A` and `D`
- end tasks `C` and `F`
- periodic configuration for `A` and `D`
- CPU/core placement
- priority ordering

### Point-interval adaptation

The only intentional semantic reduction in the experiment-local JSON is that each original interval execution time is collapsed to a point interval using the upper bound:

- `A: [3, 8] -> [8, 8]`
- `B: [3, 5] -> [5, 5]`
- `C: [8, 10] -> [10, 10]`
- `D: [6, 8] -> [8, 8]`
- `E: [15, 18] -> [18, 18]`
- `F: [3, 3] -> [3, 3]`

## PToPNer artifact inventory

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

These structures should be read as the author's manual Petri-net refinement of the original DAG motivation, not as something this repository can derive exactly today.

## Suggested structural reading from `A-F` into the `.ppn`

The author did not publish an official `A/B/C/D/E/F -> places/transitions` legend, so the table below is a **best-effort structural reading** of how the original DAG appears to have been expanded in the `.ppn`.

| DAG task | Most plausible `.ppn` region | Why this is the closest reading |
| --- | --- | --- |
| A | `p1 -> t1 -> p2 -> t2 -> p3` | This is one of the seeded start corridors, so it is the closest structural refinement of the `A` source path before the first completed-upstream state at `p3`. |
| B | `p3 -> t3 -> p4` | This is the direct continuation of the `A`-side corridor and looks like the simplest completion path before the later join logic. |
| D | `p5 -> t4 -> p6 -> t5 -> p7` | This is the second seeded start corridor, matching the other original source task on the DAG side. |
| E | `p7 -> t7 -> p8` | This is the direct continuation of the `D`-side corridor before the final join-related region. |
| C | `p9 -> t8 -> p10`, then one of `t9/t10/t11`, then `t12 -> p12` | This region behaves like the final completion side because upstream progress from earlier corridors is combined around `p10/p11` before the terminal step `t12`. |
| F | no isolated one-transition or one-corridor match | The original dashed `D -> F` branch appears to have been absorbed into the Petri-net's extra branch/synchronization structure rather than preserved as a standalone named path. |

### Important caveat on the reading above

This should be used only as a navigation aid for comparing artifacts. It is **not** a claim that:

- `t1` literally means `A`
- `t3` literally means `B`
- `t7` literally means `E`
- `t12` literally means `C`
- `F` has been recovered exactly

Instead, the point is that the original two-source DAG appears to have been manually elaborated into:

- one upstream corridor around `p1/p2/p3/p4`
- one upstream corridor around `p5/p6/p7/p8`
- one final completion corridor around `p9/p10/p11/p12`
- additional synchronization / redirection transitions `t6`, `t10`, and `t11` that do not have direct one-node counterparts in the original DAG

Those extra transitions are precisely where the manual Petri-net modeling goes beyond the plain DAG representation.

## Romeo artifact status

[romeo/RT-System.xml](romeo/RT-System.xml) is the copied author-provided Romeo file, not a hand-written semantic reconstruction of the `.ppn` file.

That means this document does **not** claim a one-to-one decoded mapping such as:

- which Romeo place corresponds to each PToPNer place
- which Romeo transition corresponds to each PToPNer transition
- which exact structural transformation introduced the Romeo `logicalInhibitor` arcs
- how the author's intermediate task-model choices were derived from the original DAG

What can be stated safely is only:

- the Romeo artifact is the authoritative Romeo-side reference for this RT-System experiment
- the Romeo artifact uses explicit Petri-net structure, including `logicalInhibitor` arcs, rather than relying on `scheduling gamma/omega` as the main carrier of priority semantics
- the Romeo artifact should win over any local hand-crafted reconstruction when there is a mismatch

## Current limitation

The repository can preserve the original DAG dependency structure directly, but it cannot currently derive the author's richer Petri-net encoding rules automatically. In particular, the undocumented manual choices around fork/join, branching, resource competition, and suspend-related structure remain outside what this experiment can claim exactly.
