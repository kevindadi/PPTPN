# PTPN JSON Input Format

This document defines the JSON format used to describe a task dependency graph (TDG) for PTPN lowering and analysis.

## Top-level structure

```json
{
  "graph": {
    "name": "TaskGraph"
  },
  "configuration": {
    "num_cpus": 2,
    "cores_per_cpu": 4,
    "shared_locks": ["mutex1", "spin1"],
    "policy": "fixed",
    "start": [{"task": "A", "tokens": 1}],
    "end": ["C"],
    "periodic": [{"task": "A", "period": 100}]
  },
  "nodes": [],
  "edges": []
}
```

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `graph` | object | no | Graph metadata |
| `graph.name` | string | no | Graph name |
| `configuration` | object | yes | System-level configuration |
| `nodes` | array | yes | Node list |
| `edges` | array | yes | Edge list |

## `configuration`

```json
"configuration": {
  "num_cpus": 2,
  "cores_per_cpu": 4,
  "shared_locks": ["mutex1", "spin1"],
  "policy": "fifo",
  "start": [{"task": "A", "tokens": 1}],
  "end": ["C"],
  "periodic": [{"task": "A", "period": 100}]
}
```

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `num_cpus` | integer | yes | Number of CPUs |
| `cores_per_cpu` | integer | yes | Number of cores per CPU |
| `shared_locks` | array | yes | Global lock names used by task nodes |
| `policy` | string | no | Scheduling policy, default is `fixed` |
| `start` | array | no | Start task bindings; each item is either a task name or `{ "task", "tokens" }` |
| `end` | array | no | End task names; a zero-time consume transition is added after task completion |
| `periodic` | array | no | Configuration-driven periodic release bindings; each item is `{ "task", "period" }` |

### Scheduling policies

Supported values:

- `fixed`
- `rm`
- `dm`
- `edf`
- `llf`
- `fifo`
- `pip`
- `pcp`
- `srp`

## `nodes`

Supported node types:

| Type | Description | Required fields |
| --- | --- | --- |
| `task` | Executable task node | `id`, `type`, `priority`, `core`, `time`, `locks` |
| `fork` | Fork node | `id`, `type` |
| `join` | Join node | `id`, `type` |
| `empty` | Empty node | `id`, `type` |

### Task fields

| Field | Type | Description |
| --- | --- | --- |
| `id` | string | Unique node identifier |
| `type` | string | Node type |
| `priority` | integer | Task priority |
| `core` | integer | Assigned core id, starting from `0` |
| `time` | array | Execution time segments |
| `locks` | array | Lock list |

## Lock naming

Locks are distinguished by prefix:

| Prefix | Meaning | Examples |
| --- | --- | --- |
| `mutex` | Mutex lock | `mutex1`, `mutex_global` |
| `spin` | Spin lock | `spin1`, `spin_irq` |

Every lock name must start with `mutex` or `spin`.

## Time segment rule

The number of time segments must satisfy:

```text
segment_count = 2 * lock_count + 1
```

Examples:

| Lock count | Segment count |
| --- | --- |
| 0 | 1 |
| 1 | 3 |
| 2 | 5 |
| n | 2n + 1 |

This assumes nested locking order.

## `edges`

```json
"edges": [
  {"source": "TaskA", "target": "TaskB"},
  {"source": "TaskA", "target": "TaskA", "label": "100"},
  {"source": "TaskA", "target": "TaskC", "label": "50", "style": "dashed"}
]
```

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `source` | string | yes | Source node id |
| `target` | string | yes | Target node id |
| `label` | string | no | Optional edge label |
| `style` | string | no | Optional edge style |

## Example

```json
{
  "graph": {
    "name": "RealTimeTaskSystem"
  },
  "configuration": {
    "num_cpus": 2,
    "cores_per_cpu": 4,
    "shared_locks": ["mutex1", "spin1"],
    "policy": "fifo",
    "start": [{"task": "TaskA", "tokens": 1}],
    "end": ["TaskC"],
    "periodic": [{"task": "TaskA", "period": 100}]
  },
  "nodes": [
    {
      "id": "TaskA",
      "type": "task",
      "priority": 97,
      "core": 0,
      "time": [[0, 10], [10, 15], [15, 30]],
      "locks": ["mutex1"]
    },
    {
      "id": "TaskB",
      "type": "task",
      "priority": 98,
      "core": 1,
      "time": [[0, 5]],
      "locks": []
    },
    {
      "id": "TaskC",
      "type": "task",
      "priority": 99,
      "core": 0,
      "time": [[0, 8]],
      "locks": []
    }
  ],
  "edges": [
    {"source": "TaskA", "target": "TaskB"},
    {"source": "TaskB", "target": "TaskC"}
  ]
}
```

## Validation rules

Errors:

- Duplicate node ids
- Unknown node types
- Invalid core indices
- Edges referencing unknown nodes
- Undefined locks
- Invalid lock prefixes
- Invalid time segment counts
- Invalid time ranges
- Unknown or non-task references in `start`, `end`, or `periodic`
- Non-positive `periodic[*].period`
- Negative token counts in `start`

Warnings:

- `start` task has predecessor edges
- `end` task has successor edges
- `periodic` task already has a self-loop release edge
- No task nodes in the graph
- `fork` or `join` nodes still carry task attributes

## Core index range

Valid core ids are in:

```text
0 .. num_cpus * cores_per_cpu - 1
```
