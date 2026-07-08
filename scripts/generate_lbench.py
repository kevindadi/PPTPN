#!/usr/bin/env python3
"""Generate multi-lane l-bench TDG JSON benchmarks (analyzable at scale).

Topology per case:

  lane i (2 cores, ~10 tasks):
    source -> F_i -> (branchA, branchB) -> J_i -> chain ... -> J_GLOBAL
  J_GLOBAL -> global sink task

Design rules that keep the state-class graph tractable:
  * globally unique task priorities (no same-priority branching),
  * mostly deterministic execution times ([c, c]); only 2-3 designated tasks
    per case carry narrow intervals (width <= 2),
  * per-lane duration offsets desynchronise lanes after the common release,
  * all periodic sources share one harmonic period sized above the worst lane
    makespan, relying on saturating task places (task_place_capacity) so busy
    releases merge instead of blocking the release clock,
  * every mutex is shared by exactly two tasks (cross-lane where possible).

Writes files under example/l-bench/ and a manifest.json with case metadata.
Use --validate for structural checks and optional TDG DOT export via ptpn.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
LBENCH_DIR = ROOT / "example" / "l-bench"
DEFAULT_PTPN = ROOT / "build" / "ptpn"

# Dense scale: 10..100 tasks step 10, cores = tasks / 5, lanes = cores / 2.
STANDARD_CASES: list[tuple[int, int]] = [(tasks, tasks // 5) for tasks in range(10, 101, 10)]
LOCK_CASE = (40, 8, 5)  # tasks, cpus, lock_count (reviewer scenario)

RELEASE_PERIOD = 60
TASK_PLACE_CAPACITY = 1


@dataclass
class CaseSpec:
    case_id: str
    filename: str
    num_tasks: int
    num_cpus: int
    num_locks: int
    num_lanes: int
    periodic_tasks: int
    period: int = RELEASE_PERIOD
    reviewer_case: bool = False

    @property
    def graph_name(self) -> str:
        return f"MultiLanePipeline{self.num_tasks}T{self.num_cpus}C{self.num_locks}L"


def case_filename(num_tasks: int, num_cpus: int, num_locks: int, *, reviewer: bool) -> str:
    if reviewer:
        return f"pipeline-{num_tasks}t-{num_cpus}c-{num_locks}l.json"
    return f"pipeline-{num_tasks}t-{num_cpus}c.json"


def task_name(index: int) -> str:
    return f"T{index:02d}"


def default_lock_count(num_tasks: int) -> int:
    return min(5, max(1, num_tasks // 20))


def periodic_lane_count(num_tasks: int, num_lanes: int) -> int:
    if num_tasks < 30:
        wanted = 1
    elif num_tasks < 70:
        wanted = 2
    else:
        wanted = 3
    return min(wanted, num_lanes)


@dataclass
class LaneLayout:
    lane: int
    source: int          # global task index of the lane source
    branch_a: int
    branch_b: int
    chain: list[int]     # serial chain after the join (may be empty)

    @property
    def all_tasks(self) -> list[int]:
        return [self.source, self.branch_a, self.branch_b, *self.chain]


def split_lane_sizes(lane_task_total: int, num_lanes: int) -> list[int]:
    base = lane_task_total // num_lanes
    remainder = lane_task_total % num_lanes
    return [base + (1 if lane < remainder else 0) for lane in range(num_lanes)]


def build_lanes(num_tasks: int, num_lanes: int) -> list[LaneLayout]:
    # One task is reserved for the global sink.
    sizes = split_lane_sizes(num_tasks - 1, num_lanes)
    lanes: list[LaneLayout] = []
    next_index = 0
    for lane, size in enumerate(sizes):
        if size < 4:
            raise ValueError(f"lane {lane} needs >= 4 tasks, got {size}")
        source = next_index
        branch_a = next_index + 1
        branch_b = next_index + 2
        chain = list(range(next_index + 3, next_index + size))
        lanes.append(LaneLayout(lane, source, branch_a, branch_b, chain))
        next_index += size
    return lanes


def deterministic_duration(lane: int, position: int) -> int:
    """Small deterministic duration; the lane offset desynchronises lanes."""
    return 2 + (lane + position) % 3


def assign_locks(lanes: list[LaneLayout], num_locks: int) -> dict[int, str]:
    """Give each mutex exactly two holder tasks (cross-lane where possible)."""
    candidates: list[int] = []
    for offset in (0, 1, 2):  # branch_a first, then early chain tasks
        for lane in lanes:
            if offset == 0:
                candidates.append(lane.branch_a)
            elif offset - 1 < len(lane.chain):
                candidates.append(lane.chain[offset - 1])
    if len(candidates) < 2 * num_locks:
        raise ValueError("not enough lock-holder candidates")

    lock_of: dict[int, str] = {}
    for lock_index in range(num_locks):
        for holder in (candidates[2 * lock_index], candidates[2 * lock_index + 1]):
            lock_of[holder] = f"mutex{lock_index}"
    return lock_of


def interval_tasks(lanes: list[LaneLayout]) -> dict[int, list[list[int]]]:
    """2-3 designated tasks with narrow intervals (width <= 2)."""
    chosen: dict[int, list[list[int]]] = {lanes[0].branch_b: [[3, 4]]}
    if len(lanes) >= 2 and lanes[1].chain:
        chosen[lanes[1].chain[0]] = [[2, 4]]
    if len(lanes) >= 3 and len(lanes[2].chain) >= 2:
        chosen[lanes[2].chain[1]] = [[3, 4]]
    return chosen


def build_case(spec: CaseSpec) -> dict[str, Any]:
    lanes = build_lanes(spec.num_tasks, spec.num_lanes)
    lock_of = assign_locks(lanes, spec.num_locks)
    intervals = interval_tasks(lanes)
    sink_index = spec.num_tasks - 1

    def core_of(lane: LaneLayout, task_index: int) -> int:
        # source / branch_a / even chain positions on the lane's first core,
        # branch_b / odd chain positions on the second core.
        first = 2 * lane.lane
        second = 2 * lane.lane + 1
        if task_index == lane.source or task_index == lane.branch_a:
            return first
        if task_index == lane.branch_b:
            return second
        position = lane.chain.index(task_index)
        return second if position % 2 == 0 else first

    def time_of(lane: LaneLayout, task_index: int, position: int) -> list[list[int]]:
        if task_index in lock_of:
            return [[1, 1], [2, 2], [1, 1]]
        if task_index in intervals:
            return intervals[task_index]
        return [[deterministic_duration(lane.lane, position)] * 2]

    nodes: list[dict[str, Any]] = []
    edges: list[dict[str, str]] = []

    for lane in lanes:
        for position, task_index in enumerate(lane.all_tasks):
            locks = [lock_of[task_index]] if task_index in lock_of else []
            nodes.append(
                {
                    "id": task_name(task_index),
                    "type": "task",
                    "priority": 10 + task_index,  # globally unique
                    "core": core_of(lane, task_index),
                    "time": time_of(lane, task_index, position),
                    "locks": locks,
                }
            )

        fork_id = f"F_L{lane.lane:02d}"
        join_id = f"J_L{lane.lane:02d}"
        nodes.append({"id": fork_id, "type": "fork"})
        nodes.append({"id": join_id, "type": "join"})

        edges.append({"source": task_name(lane.source), "target": fork_id})
        edges.append({"source": fork_id, "target": task_name(lane.branch_a)})
        edges.append({"source": fork_id, "target": task_name(lane.branch_b)})
        edges.append({"source": task_name(lane.branch_a), "target": join_id})
        edges.append({"source": task_name(lane.branch_b), "target": join_id})

        previous = join_id
        for task_index in lane.chain:
            edges.append({"source": previous, "target": task_name(task_index)})
            previous = task_name(task_index)
        edges.append({"source": previous, "target": "J_GLOBAL"})

    nodes.append({"id": "J_GLOBAL", "type": "join"})
    nodes.append(
        {
            "id": task_name(sink_index),
            "type": "task",
            "priority": 10 + sink_index,
            "core": spec.num_cpus - 1,
            "time": [[2, 2]],
            "locks": [],
        }
    )
    edges.append({"source": "J_GLOBAL", "target": task_name(sink_index)})

    periodic_lanes = lanes[: spec.periodic_tasks]
    return {
        "graph": {"name": spec.graph_name},
        "configuration": {
            "num_cpus": spec.num_cpus,
            "cores_per_cpu": 1,
            "shared_locks": [f"mutex{i}" for i in range(spec.num_locks)],
            "policy": "fixed",
            "task_place_capacity": TASK_PLACE_CAPACITY,
            "start": [{"task": task_name(lane.source), "tokens": 1} for lane in lanes],
            "end": [task_name(sink_index)],
            "periodic": [
                {"task": task_name(lane.source), "period": spec.period}
                for lane in periodic_lanes
            ],
        },
        "nodes": nodes,
        "edges": edges,
    }


def all_case_specs() -> list[CaseSpec]:
    specs: list[CaseSpec] = []
    for tasks, cpus in STANDARD_CASES:
        lanes = cpus // 2
        locks = default_lock_count(tasks)
        specs.append(
            CaseSpec(
                case_id=f"{tasks}t-{cpus}c",
                filename=case_filename(tasks, cpus, locks, reviewer=False),
                num_tasks=tasks,
                num_cpus=cpus,
                num_locks=locks,
                num_lanes=lanes,
                periodic_tasks=periodic_lane_count(tasks, lanes),
            )
        )
    tasks, cpus, locks = LOCK_CASE
    lanes = cpus // 2
    specs.append(
        CaseSpec(
            case_id=f"{tasks}t-{cpus}c-{locks}l",
            filename=case_filename(tasks, cpus, locks, reviewer=True),
            num_tasks=tasks,
            num_cpus=cpus,
            num_locks=locks,
            num_lanes=lanes,
            periodic_tasks=periodic_lane_count(tasks, lanes),
            reviewer_case=True,
        )
    )
    return specs


def validate_structure(doc: dict[str, Any], spec: CaseSpec) -> list[str]:
    errors: list[str] = []
    nodes = doc.get("nodes", [])
    edges = doc.get("edges", [])
    cfg = doc.get("configuration", {})

    tasks = [n for n in nodes if n.get("type") == "task"]
    forks = [n for n in nodes if n.get("type") == "fork"]
    joins = [n for n in nodes if n.get("type") == "join"]

    if len(tasks) != spec.num_tasks:
        errors.append(f"expected {spec.num_tasks} tasks, got {len(tasks)}")
    if len(forks) != spec.num_lanes:
        errors.append(f"expected {spec.num_lanes} fork nodes, got {len(forks)}")
    if len(joins) != spec.num_lanes + 1:
        errors.append(f"expected {spec.num_lanes + 1} join nodes, got {len(joins)}")

    if cfg.get("num_cpus") != spec.num_cpus:
        errors.append(f"expected num_cpus={spec.num_cpus}, got {cfg.get('num_cpus')}")
    if cfg.get("task_place_capacity", 1) < 1:
        errors.append("task_place_capacity must be >= 1")

    priorities = [t["priority"] for t in tasks]
    if len(set(priorities)) != len(priorities):
        errors.append("task priorities are not globally unique")

    max_core = spec.num_cpus * cfg.get("cores_per_cpu", 1) - 1
    for t in tasks:
        core = t.get("core")
        if core is None or core < 0 or core > max_core:
            errors.append(f"task {t.get('id')} has invalid core {core}")

    shared = cfg.get("shared_locks", [])
    if len(shared) != spec.num_locks:
        errors.append(f"expected {spec.num_locks} shared locks, got {len(shared)}")

    defined_locks = set(shared)
    holders_per_lock: dict[str, int] = {}
    wide_interval_tasks = 0
    for t in tasks:
        locks = t.get("locks", [])
        expected_segments = 2 * len(locks) + 1
        time_segs = t.get("time", [])
        if len(time_segs) != expected_segments:
            errors.append(
                f"task {t.get('id')}: {len(locks)} lock(s) need "
                f"{expected_segments} time segments, got {len(time_segs)}"
            )
        for lock in locks:
            if lock not in defined_locks:
                errors.append(f"task {t.get('id')} uses undefined lock {lock}")
            holders_per_lock[lock] = holders_per_lock.get(lock, 0) + 1
        for lo, hi in time_segs:
            if hi < lo:
                errors.append(f"task {t.get('id')} has invalid interval [{lo}, {hi}]")
            if hi - lo > 2:
                errors.append(f"task {t.get('id')} interval [{lo}, {hi}] wider than 2")
            if hi > lo:
                wide_interval_tasks += 1

    for lock, holders in holders_per_lock.items():
        if holders != 2:
            errors.append(f"lock {lock} must have exactly 2 holders, got {holders}")
    if wide_interval_tasks > 3:
        errors.append(f"more than 3 nondeterministic tasks: {wide_interval_tasks}")

    node_ids = {n["id"] for n in nodes}
    in_degree: dict[str, int] = {}
    out_degree: dict[str, int] = {}
    adj: dict[str, list[str]] = {nid: [] for nid in node_ids}
    for e in edges:
        if e["source"] not in node_ids:
            errors.append(f"edge source missing node: {e['source']}")
            continue
        if e["target"] not in node_ids:
            errors.append(f"edge target missing node: {e['target']}")
            continue
        out_degree[e["source"]] = out_degree.get(e["source"], 0) + 1
        in_degree[e["target"]] = in_degree.get(e["target"], 0) + 1
        adj[e["source"]].append(e["target"])

    for f in forks:
        if out_degree.get(f["id"], 0) != 2:
            errors.append(f"fork {f['id']} must have 2 outgoing edges")
    for j in joins:
        expected_in = spec.num_lanes if j["id"] == "J_GLOBAL" else 2
        if in_degree.get(j["id"], 0) != expected_in:
            errors.append(f"join {j['id']} must have {expected_in} incoming edges")

    periodic = cfg.get("periodic", [])
    if len(periodic) != spec.periodic_tasks:
        errors.append(f"expected {spec.periodic_tasks} periodic bindings, got {len(periodic)}")
    for binding in periodic:
        if binding.get("period") != spec.period:
            errors.append(f"periodic task {binding.get('task')} period != {spec.period}")

    end_tasks = cfg.get("end", [])
    if end_tasks != [task_name(spec.num_tasks - 1)]:
        errors.append(f"unexpected end tasks: {end_tasks}")

    # Every lane source must reach the global sink.
    sink = task_name(spec.num_tasks - 1)
    for binding in cfg.get("start", []):
        start = binding["task"] if isinstance(binding, dict) else binding
        seen = {start}
        stack = [start]
        while stack:
            u = stack.pop()
            for v in adj.get(u, []):
                if v not in seen:
                    seen.add(v)
                    stack.append(v)
        if sink not in seen:
            errors.append(f"sink {sink} not reachable from start task {start}")

    return errors


def write_case(spec: CaseSpec, out_dir: Path) -> Path:
    doc = build_case(spec)
    path = out_dir / spec.filename
    path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    return path


def write_manifest(specs: list[CaseSpec], out_dir: Path) -> Path:
    manifest = {
        "description": "Multi-lane parallel pipeline l-bench TDG benchmarks",
        "topology": (
            "Per-lane source -> fork -> dual branches -> join -> serial chain; "
            "lanes converge at J_GLOBAL into one global sink task"
        ),
        "policy": "fixed",
        "task_place_capacity": TASK_PLACE_CAPACITY,
        "cases": [
            {
                "filename": s.filename,
                "num_tasks": s.num_tasks,
                "num_cpus": s.num_cpus,
                "num_locks": s.num_locks,
                "num_lanes": s.num_lanes,
                "periodic_tasks": s.periodic_tasks,
                "period": s.period,
                "end": task_name(s.num_tasks - 1),
                "reviewer_case": s.reviewer_case,
            }
            for s in specs
        ],
    }
    path = out_dir / "manifest.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return path


def validate_tdg_dot(ptpn_bin: Path, json_path: Path, dot_path: Path) -> tuple[bool, str]:
    cmd = [
        str(ptpn_bin),
        "tdg",
        "-f",
        str(json_path),
        "--export-tdg",
        str(dot_path),
        "--no-analysis",
    ]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    except OSError as exc:
        return False, str(exc)

    log = proc.stdout + proc.stderr
    if proc.returncode != 0:
        return False, log.strip()[:500]
    if not dot_path.is_file():
        return False, f"missing TDG DOT: {dot_path}"
    return True, ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--all", action="store_true", help="generate all pipeline cases")
    parser.add_argument(
        "--case",
        help="generate one case by id, e.g. 40t-8c or 40t-8c-5l",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=LBENCH_DIR,
        help=f"output directory (default: {LBENCH_DIR})",
    )
    parser.add_argument(
        "--validate",
        action="store_true",
        help="validate generated JSON (structure + optional TDG DOT export)",
    )
    parser.add_argument(
        "--ptpn",
        type=Path,
        default=DEFAULT_PTPN,
        help=f"path to ptpn binary for TDG DOT validation (default: {DEFAULT_PTPN})",
    )
    parser.add_argument(
        "--skip-tdg-dot",
        action="store_true",
        help="skip ptpn TDG DOT export during --validate",
    )
    args = parser.parse_args()

    specs = all_case_specs()
    spec_by_id = {s.case_id: s for s in specs}

    if args.all:
        selected = specs
    elif args.case:
        if args.case not in spec_by_id:
            print(f"error: unknown case {args.case!r}", file=sys.stderr)
            print(f"available: {', '.join(spec_by_id)}", file=sys.stderr)
            return 1
        selected = [spec_by_id[args.case]]
    elif args.validate:
        selected = specs
    else:
        parser.print_help()
        return 1

    out_dir = args.out_dir.expanduser()
    if not out_dir.is_absolute():
        out_dir = (ROOT / out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    if (args.all or args.case) and not args.validate:
        for spec in selected:
            path = write_case(spec, out_dir)
            print(f" wrote {path.relative_to(ROOT)}")
        if args.all:
            manifest_path = write_manifest(specs, out_dir)
            print(f" wrote {manifest_path.relative_to(ROOT)}")

    if args.validate:
        validate_targets = specs if args.all else selected
        ptpn_bin = args.ptpn.expanduser()
        if not ptpn_bin.is_absolute():
            ptpn_bin = (ROOT / ptpn_bin).resolve()
        use_ptpn = ptpn_bin.is_file() and not args.skip_tdg_dot

        failed = 0
        for spec in validate_targets:
            path = out_dir / spec.filename
            if not path.is_file():
                print(f" skip missing {path.name}", file=sys.stderr)
                failed += 1
                continue

            doc = json.loads(path.read_text(encoding="utf-8"))
            errors = validate_structure(doc, spec)
            if errors:
                print(f" FAIL {spec.filename}:", file=sys.stderr)
                for err in errors:
                    print(f"   - {err}", file=sys.stderr)
                failed += 1
                continue

            print(f" ok  {spec.filename} (structure)")

            if use_ptpn:
                dot_path = out_dir / ".validate" / f"{path.stem}.tdg.dot"
                dot_path.parent.mkdir(parents=True, exist_ok=True)
                ok, err = validate_tdg_dot(ptpn_bin, path, dot_path)
                if ok:
                    print(f" ok  {spec.filename} (tdg dot -> {dot_path.relative_to(ROOT)})")
                else:
                    print(f" FAIL {spec.filename} TDG DOT: {err}", file=sys.stderr)
                    failed += 1
            elif not args.skip_tdg_dot:
                print(
                    f" note: {ptpn_bin} not found; skipped TDG DOT export",
                    file=sys.stderr,
                )

        return 1 if failed else 0

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
