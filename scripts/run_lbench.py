#!/usr/bin/env python3
"""Run l-bench pipeline TDG benchmarks and collect scaling metrics.

Targets the multi-lane parallel pipeline JSON under example/l-bench/
(manifest.json lists cases, lanes, locks, and periodic release counts).

Metrics per case: tasks, cpus, locks, lanes, periodic_tasks, fork/join nodes,
TDG nodes/edges (when exported), places, transitions, SCG states, memory (KB),
and phase timings (ms).

Summary: example/l-bench/bench-summary.json and bench-summary.csv

Use --validate-tdg-only to export TDG DOT only (no PTPN lowering / SCG).
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LBENCH_DIR = ROOT / "example" / "l-bench"
DEFAULT_PTPN = ROOT / "build" / "ptpn"
PIPELINE_GLOB = "pipeline-*.json"

RE_PLACES = re.compile(r"Places:\s*(\d+)")
RE_TRANSITIONS = re.compile(r"Transitions:\s*(\d+)")
RE_STATS_TDG2PN = re.compile(r"\[STATS\] TDG2PN lowering:\s*(\d+)\s*ms")
RE_STATS_SCG = re.compile(
    r"\[STATS\] SCG build:\s*(\d+)\s*ms "
    r"\(states=(\d+), edges=(\d+), dedup_hits=(\d+)(, truncated)?\)"
)
RE_BUILD_TRUNCATED = re.compile(r"build complete:.*truncated=(true|false)")
RE_STATS_TOTAL = re.compile(
    r"\[STATS\] TDG pipeline \(total\):\s*(\d+)\s*ms(?: \((\d+) KB\))?"
)
RE_STATS_PTPN_TOTAL = re.compile(
    r"\[STATS\] PTPN pipeline \(total\):\s*(\d+)\s*ms(?: \((\d+) KB\))?"
)
RE_TDG_DOT = re.compile(r"\[DOT\] Exported (\d+) nodes, (\d+) edges")
RE_MEMORY_KB = re.compile(r"\((\d+) KB\)")


@dataclass
class BenchRow:
    case: str
    file: str
    tasks: int
    cpus: int
    locks: int
    lanes: int = 0
    periodic_tasks: int = 0
    fork_nodes: int = 0
    join_nodes: int = 0
    reviewer_case: bool = False
    max_states: int = 0
    canonicalization: str = "equality"
    extrapolation: bool = False
    ok: bool = False
    error: str = ""
    tdg_nodes: int | None = None
    tdg_edges: int | None = None
    places: int | None = None
    transitions: int | None = None
    scg_states: int | None = None
    scg_edges: int | None = None
    scg_dedup_hits: int | None = None
    scg_truncated: bool = False
    ms_tdg2pn: int | None = None
    ms_scg_build: int | None = None
    ms_total: int | None = None
    memory_kb: int | None = None
    validate_tdg_only: bool = False
    outputs: list[str] = field(default_factory=list)


def load_manifest(lbench_dir: Path) -> dict:
    manifest_path = lbench_dir / "manifest.json"
    if not manifest_path.is_file():
        return {}
    return json.loads(manifest_path.read_text(encoding="utf-8"))


def manifest_case_map(manifest: dict) -> dict[str, dict]:
    return {c["filename"]: c for c in manifest.get("cases", [])}


def case_sort_key(path: Path, case_meta: dict[str, dict]) -> tuple:
    meta = case_meta.get(path.name, {})
    return (
        meta.get("num_tasks", 9999),
        meta.get("reviewer_case", False),
        path.name,
    )


def discover_cases(
    lbench_dir: Path,
    case_filter: list[str] | None,
    *,
    use_manifest: bool,
) -> list[Path]:
    case_meta = manifest_case_map(load_manifest(lbench_dir))
    all_paths = {p.name: p for p in lbench_dir.glob(PIPELINE_GLOB)}

    if use_manifest and case_meta:
        ordered = [all_paths[name] for name in case_meta if name in all_paths]
    else:
        ordered = sorted(all_paths.values(), key=lambda p: case_sort_key(p, case_meta))

    if not case_filter:
        return ordered

    wanted = set(case_filter)
    out: list[Path] = []
    for path in ordered:
        stem = path.stem
        short = stem.removeprefix("pipeline-")
        if stem in wanted or short in wanted or path.name in wanted:
            out.append(path)
    return out


def analyze_json(path: Path) -> dict:
    doc = json.loads(path.read_text(encoding="utf-8"))
    cfg = doc.get("configuration", {})
    nodes = doc.get("nodes", [])
    edges = doc.get("edges", [])

    tasks = sum(1 for n in nodes if n.get("type") == "task")
    forks = sum(1 for n in nodes if n.get("type") == "fork")
    joins = sum(1 for n in nodes if n.get("type") == "join")

    return {
        "tasks": tasks,
        "cpus": cfg.get("num_cpus", 0),
        "locks": len(cfg.get("shared_locks", [])),
        "periodic_tasks": len(cfg.get("periodic", [])),
        "fork_nodes": forks,
        "join_nodes": joins,
        "tdg_nodes": len(nodes),
        "tdg_edges": len(edges),
    }


def apply_manifest_meta(row: BenchRow, meta: dict | None) -> None:
    if not meta:
        return
    row.lanes = meta.get("num_lanes", row.lanes)
    if "periodic_tasks" in meta:
        row.periodic_tasks = meta["periodic_tasks"]
    row.reviewer_case = bool(meta.get("reviewer_case", False))


def parse_log(text: str) -> dict:
    stats: dict = {"scg_truncated": False}
    if m := RE_BUILD_TRUNCATED.search(text):
        stats["scg_truncated"] = m.group(1) == "true"
    elif "Reachability graph truncated" in text:
        stats["scg_truncated"] = True

    if m := RE_PLACES.search(text):
        stats["places"] = int(m.group(1))
    if m := RE_TRANSITIONS.search(text):
        stats["transitions"] = int(m.group(1))
    if m := RE_STATS_TDG2PN.search(text):
        stats["ms_tdg2pn"] = int(m.group(1))
    if m := RE_STATS_SCG.search(text):
        stats["ms_scg_build"] = int(m.group(1))
        stats["scg_states"] = int(m.group(2))
        stats["scg_edges"] = int(m.group(3))
        stats["scg_dedup_hits"] = int(m.group(4))
    if m := RE_TDG_DOT.search(text):
        stats["tdg_nodes"] = int(m.group(1))
        stats["tdg_edges"] = int(m.group(2))
    for pattern in (RE_STATS_TOTAL, RE_STATS_PTPN_TOTAL):
        if m := pattern.search(text):
            stats["ms_total"] = int(m.group(1))
            if m.group(2):
                stats["memory_kb"] = int(m.group(2))
            break
    if "memory_kb" not in stats:
        kb_vals = [int(m.group(1)) for m in RE_MEMORY_KB.finditer(text)]
        if kb_vals:
            stats["memory_kb"] = max(kb_vals)
    return stats


def recommended_max_states(tasks: int, periodic_tasks: int) -> int:
    base = 10_000
    if tasks >= 100:
        base = 50_000
    elif tasks >= 60:
        base = 30_000
    elif tasks >= 40:
        base = 20_000
    if periodic_tasks >= 2:
        base = int(base * 1.5)
    return base


def run_case(
    ptpn_bin: Path,
    input_path: Path,
    max_states: int,
    validate_tdg_only: bool,
    out_dir: Path,
    case_meta: dict | None,
    canonicalization: str = "equality",
    extrapolation: bool = False,
) -> BenchRow:
    case = input_path.stem
    parsed_json = analyze_json(input_path)
    row = BenchRow(
        case=case,
        file=input_path.name,
        tasks=parsed_json["tasks"],
        cpus=parsed_json["cpus"],
        locks=parsed_json["locks"],
        periodic_tasks=parsed_json["periodic_tasks"],
        fork_nodes=parsed_json["fork_nodes"],
        join_nodes=parsed_json["join_nodes"],
        tdg_nodes=parsed_json["tdg_nodes"],
        tdg_edges=parsed_json["tdg_edges"],
        max_states=max_states,
        canonicalization=canonicalization,
        extrapolation=extrapolation,
        validate_tdg_only=validate_tdg_only,
    )
    apply_manifest_meta(row, case_meta)

    out_dir.mkdir(parents=True, exist_ok=True)

    if validate_tdg_only:
        dot_path = out_dir / f"{case}.tdg.dot"
        cmd = [
            str(ptpn_bin),
            "tdg",
            "-f",
            str(input_path),
            "--export-tdg",
            str(dot_path),
            "--no-analysis",
        ]
    else:
        scg_dot = out_dir / f"{case}.scg.dot"
        ptpn_dot = out_dir / f"{case}.ptpn.dot"
        cmd = [
            str(ptpn_bin),
            "tdg",
            "-f",
            str(input_path),
            "-m",
            str(max_states),
            "--canonicalization",
            canonicalization,
            "--export-ptpn",
            str(ptpn_dot),
            "--export-scg",
            str(scg_dot),
        ]
        if extrapolation:
            cmd.append("--extrapolation")

    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    except OSError as exc:
        row.error = str(exc)
        return row

    log = proc.stdout + proc.stderr
    for key, value in parse_log(log).items():
        setattr(row, key, value)

    if validate_tdg_only:
        row.outputs = [str(out_dir / f"{case}.tdg.dot")]
    else:
        row.outputs = [str(out_dir / f"{case}.ptpn.dot"), str(out_dir / f"{case}.scg.dot")]

    if proc.returncode != 0:
        row.error = log.strip()[:500]
        return row

    missing = [p for p in row.outputs if not Path(p).is_file()]
    if missing:
        row.error = f"missing output: {', '.join(missing)}"
        return row

    row.ok = True
    return row


def write_summary(
    rows: list[BenchRow],
    json_path: Path,
    csv_path: Path,
    manifest: dict,
) -> None:
    payload = {
        "suite": "l-bench",
        "topology": manifest.get("topology"),
        "description": manifest.get("description"),
        "task_place_capacity": manifest.get("task_place_capacity"),
        "runs": [asdict(r) for r in rows],
    }
    json_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    fieldnames = [
        "case",
        "file",
        "tasks",
        "cpus",
        "locks",
        "lanes",
        "periodic_tasks",
        "fork_nodes",
        "join_nodes",
        "reviewer_case",
        "max_states",
        "canonicalization",
        "extrapolation",
        "ok",
        "tdg_nodes",
        "tdg_edges",
        "places",
        "transitions",
        "scg_states",
        "scg_edges",
        "scg_truncated",
        "memory_kb",
        "ms_tdg2pn",
        "ms_scg_build",
        "ms_total",
        "validate_tdg_only",
        "error",
    ]
    with csv_path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            data = asdict(row)
            writer.writerow({k: data.get(k) for k in fieldnames})


def print_table(rows: list[BenchRow]) -> None:
    header = (
        f"{'case':<26} {'ok':<4} {'T/C/L':<10} {'lane/p':<8} "
        f"{'P/T':<11} {'SCG':<8} {'mem':<8} {'ms':<8}"
    )
    print(header)
    print("-" * len(header))
    for r in rows:
        tcl = f"{r.tasks}/{r.cpus}/{r.locks}"
        lp = f"{r.lanes}/{r.periodic_tasks}"
        pt = (
            f"{r.places}/{r.transitions}"
            if r.places is not None and r.transitions is not None
            else "-"
        )
        scg = str(r.scg_states) if r.scg_states is not None else "-"
        mem = str(r.memory_kb) if r.memory_kb is not None else "-"
        ms = str(r.ms_total) if r.ms_total is not None else "-"
        ok = "yes" if r.ok else "no"
        print(f"{r.case:<26} {ok:<4} {tcl:<10} {lp:<8} {pt:<11} {scg:<8} {mem:<8} {ms:<8}")


def print_state_hints(
    cases: list[Path],
    case_meta: dict[str, dict],
    max_states: int,
    *,
    validate_tdg_only: bool,
) -> None:
    if validate_tdg_only:
        return
    for path in cases:
        meta = case_meta.get(path.name, {})
        tasks = meta.get("num_tasks")
        periodic = meta.get("periodic_tasks", 0)
        if tasks is None:
            continue
        suggested = recommended_max_states(tasks, periodic)
        if max_states < suggested:
            print(
                f"note: {path.name} (tasks={tasks}, periodic={periodic}) "
                f"may need -m {suggested} or higher",
                file=sys.stderr,
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--ptpn",
        type=Path,
        default=DEFAULT_PTPN,
        help=f"path to ptpn executable (default: {DEFAULT_PTPN})",
    )
    parser.add_argument(
        "--lbench-dir",
        type=Path,
        default=LBENCH_DIR,
        help=f"l-bench directory (default: {LBENCH_DIR})",
    )
    parser.add_argument(
        "-m",
        "--max-states",
        type=int,
        default=10_000,
        help="SCG state cap for full runs (default: 10000)",
    )
    parser.add_argument(
        "--cases",
        nargs="+",
        help="case stems to run, e.g. pipeline-40t-8c-5l or 40t-8c-5l",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="run all cases listed in manifest.json (fallback: pipeline-*.json)",
    )
    parser.add_argument(
        "--reviewer-case",
        action="store_true",
        help="run only manifest reviewer_case (pipeline-40t-8c-5l.json)",
    )
    parser.add_argument(
        "--canonicalization",
        choices=["equality", "max-lower", "intersection"],
        default="equality",
        help="state-class canonicalization mode passed to ptpn (default: equality)",
    )
    parser.add_argument(
        "--extrapolation",
        action="store_true",
        help="enable k-extrapolation of clock zones (passed to ptpn)",
    )
    parser.add_argument(
        "--validate-tdg-only",
        action="store_true",
        help="export TDG DOT only; skip PTPN lowering and SCG analysis",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=LBENCH_DIR / "results",
        help="artifact output directory (default: example/l-bench/results)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print planned runs without executing",
    )
    args = parser.parse_args()

    if not args.all and not args.cases and not args.reviewer_case:
        parser.error("specify --all, --reviewer-case, or --cases")

    lbench_dir = args.lbench_dir.expanduser()
    if not lbench_dir.is_absolute():
        lbench_dir = (ROOT / lbench_dir).resolve()

    manifest = load_manifest(lbench_dir)
    case_meta = manifest_case_map(manifest)

    if args.reviewer_case:
        reviewer_files = [
            name for name, meta in case_meta.items() if meta.get("reviewer_case")
        ]
        if not reviewer_files:
            print("error: no reviewer_case in manifest.json", file=sys.stderr)
            return 1
        case_filter = reviewer_files
        use_manifest = True
    else:
        case_filter = args.cases if not args.all else None
        use_manifest = bool(args.all and case_meta)

    cases = discover_cases(lbench_dir, case_filter, use_manifest=use_manifest)
    if not cases:
        print("error: no pipeline JSON files found", file=sys.stderr)
        return 1

    if args.dry_run:
        mode = "tdg-dot-only" if args.validate_tdg_only else f"full scg -m {args.max_states}"
        for path in cases:
            meta = case_meta.get(path.name, {})
            lanes = meta.get("num_lanes", "?")
            periodic = meta.get("periodic_tasks", "?")
            print(f"[dry-run] {path.name} lanes={lanes} periodic={periodic} ({mode})")
        return 0

    if not args.validate_tdg_only:
        print_state_hints(
            cases,
            case_meta,
            args.max_states,
            validate_tdg_only=args.validate_tdg_only,
        )

    ptpn_bin = args.ptpn.expanduser()
    if not ptpn_bin.is_absolute():
        ptpn_bin = (ROOT / ptpn_bin).resolve()

    if not ptpn_bin.is_file():
        print(f"error: ptpn not found: {ptpn_bin}", file=sys.stderr)
        print("build first: cmake --build build", file=sys.stderr)
        return 1

    out_dir = args.out_dir.expanduser()
    if not out_dir.is_absolute():
        out_dir = (ROOT / out_dir).resolve()

    rows: list[BenchRow] = []
    for path in cases:
        print(f"[*] {path.name} ...", flush=True)
        row = run_case(
            ptpn_bin,
            path,
            args.max_states,
            args.validate_tdg_only,
            out_dir,
            case_meta.get(path.name),
            canonicalization=args.canonicalization,
            extrapolation=args.extrapolation,
        )
        rows.append(row)
        print(f"    {'ok' if row.ok else 'FAILED: ' + row.error}")

    summary_json = lbench_dir / "bench-summary.json"
    summary_csv = lbench_dir / "bench-summary.csv"
    write_summary(rows, summary_json, summary_csv, manifest)

    print()
    print_table(rows)
    print()
    print(f"summary: {summary_json}")
    print(f"summary: {summary_csv}")

    failed = sum(1 for r in rows if not r.ok)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
