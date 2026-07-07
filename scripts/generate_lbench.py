#!/usr/bin/env python3
"""Generate progressive l-bench TDG JSON benchmarks (fork/join pipelines).

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

# Dense scale: 10..100 step 10, cores = tasks / 5
STANDARD_CASES: list[tuple[int, int]] = [
    (tasks, tasks // 5) for tasks in range(10, 101, 10)
]
LOCK_CASE = (40, 8, 5)  # tasks, cpus, lock_count


@dataclass
class CaseSpec:
    case_id: str
    filename: str
    num_tasks: int
    num_cpus: int
    num_locks: int = 0
    periodic: int = 0

    @property
    def graph_name(self) -> str:
        if self.num_locks:
            return f"EmbeddedPipeline{self.num_tasks}T{self.num_cpus}C{self.num_locks}Locks"
        return f"EmbeddedPipeline{self.num_tasks}T{self.num_cpus}C"


def case_filename(num_tasks: int, num_cpus: int, num_locks: int = 0) -> str:
    if num_locks:
        return f"pipeline-{num_tasks}t-{num_cpus}c-{num_locks}l.json"
    return f"pipeline-{num_tasks}t-{num_cpus}c.json"


def task_name(index: int) -> str:
    return f"T{index:02d}"


LOCK_CASE = (40, 8, 5)  # tasks, cpus, lock_count (reviewer scenario)


def default_lock_count(num_tasks: int) -> int:
    """Scale shared mutex count with pipeline size (1..5), no periodic overhead."""
    return min(5, max(1, (num_tasks + 9) // 20))


def task_role(index: int) -> str:
    if index == 0:
        return "entry"
    rem = index % 3
    if rem == 0:
        return "fuse"
    if rem == 1:
        return "branch_a"
    return "branch_b"


def task_locks(index: int, num_locks: int, *, heavy: bool = False) -> list[str]:
    if num_locks <= 0:
        return []

    role = task_role(index)

    if heavy:
        # Reviewer 40t/8c/5locks: both parallel branches contend, occasional fuse CS.
        if role in ("branch_a", "branch_b"):
            return [f"mutex{index % num_locks}"]
        if role == "fuse" and index > 0 and (index // 3) % 2 == 1:
            return [f"mutex{(index // 3) % num_locks}"]
        return []

    if role == "branch_a":
        return [f"mutex{index % num_locks}"]
    if role == "branch_b" and num_locks >= 2 and (index // 3) % 2 == 0:
        return [f"mutex{(index + 1) % num_locks}"]
    if role == "fuse" and num_locks >= 3 and index > 0 and (index // 3) % 3 == 1:
        return [f"mutex{(index // 3) % num_locks}"]
    return []


def task_time(index: int, locks: list[str]) -> list[list[int]]:
    role = task_role(index)
    if locks:
        if role == "branch_a":
            return [[1, 3], [3, 7], [1, 2]]
        if role == "branch_b":
            return [[2, 4], [2, 6], [1, 3]]
        if role == "fuse":
            return [[1, 2], [2, 5], [2, 4]]
        return [[1, 2], [2, 4], [1, 2]]

    if role == "entry":
        return [[2, 5]]
    if role == "fuse":
        return [[3, 8]]
    if role == "branch_a":
        return [[4, 10]]
    return [[3, 9]]


def count_locked_tasks(num_tasks: int, num_locks: int, *, heavy: bool = False) -> int:
    return sum(1 for i in range(num_tasks) if task_locks(i, num_locks, heavy=heavy))


def build_pipeline(
    num_tasks: int, num_cpus: int, num_locks: int = 0, *, heavy_locks: bool = False
) -> dict[str, Any]:
    if num_tasks < 1:
        raise ValueError("num_tasks must be >= 1")

    nodes: list[dict[str, Any]] = []
    edges: list[dict[str, str]] = []

    for i in range(num_tasks):
        locks = task_locks(i, num_locks, heavy=heavy_locks)
        nodes.append(
            {
                "id": task_name(i),
                "type": "task",
                "priority": 40 + (i % 20),
                "core": i % num_cpus,
                "time": task_time(i, locks),
                "locks": locks,
            }
        )

    current = 0
    stage = 0
    task_count = 1

    while task_count + 3 <= num_tasks:
        fork_id = f"F_{stage:03d}"
        join_id = f"J_{stage:03d}"
        nodes.append({"id": fork_id, "type": "fork"})
        nodes.append({"id": join_id, "type": "join"})

        ta = task_count
        tb = task_count + 1
        t_next = task_count + 2

        edges.append({"source": task_name(current), "target": fork_id})
        edges.append({"source": fork_id, "target": task_name(ta)})
        edges.append({"source": fork_id, "target": task_name(tb)})
        edges.append({"source": task_name(ta), "target": join_id})
        edges.append({"source": task_name(tb), "target": join_id})
        edges.append({"source": join_id, "target": task_name(t_next)})

        current = t_next
        task_count += 3
        stage += 1

    while task_count < num_tasks:
        edges.append({"source": task_name(current), "target": task_name(task_count)})
        current = task_count
        task_count += 1

    shared_locks = [f"mutex{i}" for i in range(num_locks)]

    name = (
        f"EmbeddedPipeline{num_tasks}T{num_cpus}C{num_locks}Locks"
        if num_locks
        else f"EmbeddedPipeline{num_tasks}T{num_cpus}C"
    )
    return {
        "graph": {"name": name},
        "configuration": {
            "num_cpus": num_cpus,
            "cores_per_cpu": 1,
            "shared_locks": shared_locks,
            "policy": "fixed",
            "start": [{"task": task_name(0), "tokens": 1}],
            "end": [task_name(num_tasks - 1)],
        },
        "nodes": nodes,
        "edges": edges,
    }


def all_case_specs() -> list[CaseSpec]:
    specs: list[CaseSpec] = []
    for tasks, cpus in STANDARD_CASES:
        locks = default_lock_count(tasks)
        cid = f"{tasks}t-{cpus}c" if locks <= 1 else f"{tasks}t-{cpus}c-{locks}l"
        specs.append(
            CaseSpec(
                case_id=cid,
                filename=case_filename(tasks, cpus),
                num_tasks=tasks,
                num_cpus=cpus,
                num_locks=locks,
            )
        )
    t, c, locks = LOCK_CASE
    specs.append(
        CaseSpec(
            case_id=f"{t}t-{c}c-{locks}l",
            filename=case_filename(t, c, locks),
            num_tasks=t,
            num_cpus=c,
            num_locks=locks,
        )
    )
    return specs


def is_heavy_lock_case(spec: CaseSpec) -> bool:
    return spec.case_id.endswith("-5l") and spec.num_locks == 5


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

    num_cpus = cfg.get("num_cpus")
    if num_cpus != spec.num_cpus:
        errors.append(f"expected num_cpus={spec.num_cpus}, got {num_cpus}")

    max_core = num_cpus * cfg.get("cores_per_cpu", 1) - 1
    for t in tasks:
        core = t.get("core")
        if core is None or core < 0 or core > max_core:
            errors.append(f"task {t.get('id')} has invalid core {core}")

    shared = cfg.get("shared_locks", [])
    if len(shared) != spec.num_locks:
        errors.append(f"expected {spec.num_locks} shared locks, got {len(shared)}")

    defined_locks = set(shared)
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
            if not (lock.startswith("mutex") or lock.startswith("spin")):
                errors.append(f"task {t.get('id')} has invalid lock prefix: {lock}")

    node_ids = {n["id"] for n in nodes}
    for e in edges:
        if e["source"] not in node_ids:
            errors.append(f"edge source missing node: {e['source']}")
        if e["target"] not in node_ids:
            errors.append(f"edge target missing node: {e['target']}")

    end_tasks = cfg.get("end", [])
    if end_tasks != [task_name(spec.num_tasks - 1)]:
        errors.append(f"unexpected end tasks: {end_tasks}")

    if cfg.get("periodic"):
        errors.append("periodic bindings should be empty for pipeline benchmarks")

    out_degree: dict[str, int] = {}
    in_degree: dict[str, int] = {}
    adj: dict[str, list[str]] = {nid: [] for nid in node_ids}
    for e in edges:
        out_degree[e["source"]] = out_degree.get(e["source"], 0) + 1
        in_degree[e["target"]] = in_degree.get(e["target"], 0) + 1
        adj[e["source"]].append(e["target"])

    for f in forks:
        fid = f["id"]
        out = [e for e in edges if e["source"] == fid]
        if len(out) != 2:
            errors.append(f"fork {fid} must have 2 outgoing task edges, got {len(out)}")

    for j in joins:
        jid = j["id"]
        inc = [e for e in edges if e["target"] == jid]
        if len(inc) != 2:
            errors.append(f"join {jid} must have 2 incoming task edges, got {len(inc)}")

    # Reachability from T00 to end
    start = task_name(0)
    end = task_name(spec.num_tasks - 1)
    seen = {start}
    stack = [start]
    while stack:
        u = stack.pop()
        for v in adj.get(u, []):
            if v not in seen:
                seen.add(v)
                stack.append(v)
    if end not in seen:
        errors.append(f"end task {end} not reachable from {start}")

    if len(forks) != len(joins):
        errors.append(f"fork/join count mismatch: {len(forks)} vs {len(joins)}")

    return errors


def write_case(spec: CaseSpec, out_dir: Path) -> Path:
    doc = build_pipeline(
        spec.num_tasks,
        spec.num_cpus,
        spec.num_locks,
        heavy_locks=is_heavy_lock_case(spec),
    )
    doc["graph"]["name"] = spec.graph_name
    path = out_dir / spec.filename
    path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    return path


def write_manifest(specs: list[CaseSpec], out_dir: Path) -> Path:
    manifest = {
        "description": "Progressive embedded pipeline l-bench TDG benchmarks",
        "policy": "fixed",
        "periodic_tasks": 0,
        "cases": [
            {
                **asdict(s),
                "fork_join_stages": (s.num_tasks - 1) // 3,
                "locked_tasks": count_locked_tasks(
                    s.num_tasks, s.num_locks, heavy=is_heavy_lock_case(s)
                ),
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
