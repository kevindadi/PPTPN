# RT-System cross-tool experiment

This experiment starts from the same original real-time DAG motivation as [example/common/input.json](../../../example/common/input.json), then compares how that dependency structure was represented in later hand-modeled Petri-net artifacts.

The known artifact lineage is:

- [example/common/input.json](../../../example/common/input.json): the original DAG-style task-dependency input in this repository
- [tools/PToPNer/examples/RT-System.ppn](../../../tools/PToPNer/examples/RT-System.ppn): the PToPNer author's manually modeled point-interval Petri-net version
- [romeo/RT-System.xml](romeo/RT-System.xml): the author-provided Romeo version copied from [hlf-ptopn-100.xml](../../../hlf-ptopn-100.xml)

The important distinction is that the DAG dependency motivation comes from `example/common/input.json`, while the PToPNer and Romeo files are later manual Petri-net encodings that refine the behavior with explicit fork/join, choice, and resource-style structure. The author's exact intermediate modeling steps were not documented.

## Scope

- compare the same RT-style dependency scenario across DAG / PToPNer / Romeo artifacts
- keep the experiment-local JSON input aligned with the original DAG structure from `example/common/input.json`
- compare only state-space / reachability outputs
- ignore the formula blocks from the original PToPNer example

## Layout

- `ptopner/RT-System.ppn`: trimmed source model with transitions and places only
- `romeo/RT-System.xml`: author-provided Romeo net copied from `hlf-ptopn-100.xml`
- `priority/input.json`: point-interval DAG version derived directly from `example/common/input.json`
- `mapping.md`: lineage notes, known correspondences, and approximations

## Authoritative inputs

For the DAG side, [priority/input.json](priority/input.json) should preserve the original task names, dependency edges, start tasks, end tasks, and periods from [example/common/input.json](../../../example/common/input.json), with only the execution times collapsed to point intervals.

For the Romeo side of the comparison, [romeo/RT-System.xml](romeo/RT-System.xml) is not a hand-written reconstruction. It is the author-provided XML version of the PToPNer RT-System example, preserved as the experiment-local reference file.

For the PToPNer side, [ptopner/RT-System.ppn](ptopner/RT-System.ppn) remains the local baseline for the point-interval Petri-net model with formulas removed.

## How to read the `.ppn` against the original DAG

The `.ppn` should not be read as a literal task-for-task renaming of `A-F`. A more useful reading is:

- the original `A -> B` side is expanded into the upstream corridor around `p1/p2/p3/p4`
- the original `D -> E` side is expanded into the upstream corridor around `p5/p6/p7/p8`
- the original completion around `C` is expanded into the final corridor around `p9/p10/p11/p12`
- extra transitions such as `t6`, `t10`, and `t11` represent manual synchronization / redirection structure that does not exist as standalone nodes in the original DAG

A more explicit best-effort `A-F` to `.ppn` reading is recorded in [mapping.md](mapping.md).

## Known approximations

- The original formula sections are intentionally dropped from the experiment-local `.ppn` copy.
- The semantic mapping from the original DAG into the author's PToPNer / Romeo nets is only partially known. We know the motivation starts from the DAG dependency pattern, but the exact manual modeling choices for fork/join, branching, and resource-like control were not documented.
- The experiment-local `priority/input.json` is therefore the DAG-side baseline, not a reverse-engineered Petri-net encoding.

## Point-interval convention in `priority/input.json`

The original DAG timings in [example/common/input.json](../../../example/common/input.json) are interval-valued. For this experiment, they are collapsed to point intervals by taking the upper bound of each task's execution-time range:

- `A: [3, 8] -> [8, 8]`
- `B: [3, 5] -> [5, 5]`
- `C: [8, 10] -> [10, 10]`
- `D: [6, 8] -> [8, 8]`
- `E: [15, 18] -> [18, 18]`
- `F: [3, 3] -> [3, 3]`

This keeps the original DAG structure intact while making the repository-local input match the experiment's point-interval restriction.
