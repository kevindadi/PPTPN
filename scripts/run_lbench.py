#!/usr/bin/env python3
"""Run l-bench pipeline TDG benchmarks and collect scaling metrics.

Metrics per case: tasks, cpus, locks, places, transitions, SCG states,
memory (KB), and phase timings (ms).

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
    r"\(states=(\d+), edges=(\d+), dedup_hits=(\d+)(?:, truncated)?\)"
)
RE_STATS_TOTAL = re.compile(
    r"\[STATS\] TDG pipeline \(total\):\s*(\d+)\s*ms(?: \((\d+) KB\))?"
)
RE_STATS_PTPN_TOTAL = re.compile(
    r"\[STATS\] PTPN pipeline \(total\):\s*(\d+)\s*ms(?: \((\d+) KB\))?"
)
RE_MEMORY_KB = re.compile(r"\((\d+) KB\)")


@dataclass
class BenchRow:
    case: str
    file: str
    tasks: int
    cpus: int
    locks: int
    ok: bool = False
    error: str = ""
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


def discover_cases(lbench_dir: Path, case_filter: list[str] | None) -> list[Path]:
    paths = sorted(lbench_dir.glob(PIPELINE_GLOB))
    if not case_filter:
        return paths
    wanted = set(case_filter)
    out: list[Path] = []
    for path in paths:
        stem = path.stem
        if stem in wanted or stem.removeprefix("pipeline-") in wanted:
            out.append(path)
    return out


def load_manifest_meta(lbench_dir: Path) -> dict[str, dict]:
    manifest_path = lbench_dir / "manifest.json"
    if not manifest_path.is_file():
        return {}
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    return {c["filename"]: c for c in data.get("cases", [])}


def count_from_json(path: Path) -> tuple[int, int, int]:
    doc = json.loads(path.read_text(encoding="utf-8"))
    cfg = doc.get("configuration", {})
    tasks = sum(1 for n in doc.get("nodes", []) if n.get("type") == "task")
    cpus = cfg.get("num_cpus", 0)
    locks = len(cfg.get("shared_locks", []))
    return tasks, cpus, locks


def parse_log(text: str) -> dict:
    stats: dict = {"scg_truncated": ", truncated" in text}

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


def run_case(
    ptpn_bin: Path,
    input_path: Path,
    max_states: int,
    validate_tdg_only: bool,
    out_dir: Path,
) -> BenchRow:
    case = input_path.stem
    tasks, cpus, locks = count_from_json(input_path)
    row = BenchRow(
        case=case,
        file=input_path.name,
        tasks=tasks,
        cpus=cpus,
        locks=locks,
        validate_tdg_only=validate_tdg_only,
    )

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
            "--export-ptpn",
            str(ptpn_dot),
            "--export-scg",
            str(scg_dot),
        ]

    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    except OSError as exc:
        row.error = str(exc)
        return row

    log = proc.stdout + proc.stderr
    parsed = parse_log(log)
    for key, value in parsed.items():
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


def write_summary(rows: list[BenchRow], json_path: Path, csv_path: Path) -> None:
    payload = {
        "suite": "l-bench",
        "runs": [asdict(r) for r in rows],
    }
    json_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    fieldnames = [
        "case",
        "file",
        "tasks",
        "cpus",
        "locks",
        "ok",
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
        f"{'case':<28} {'ok':<4} {'T/C/L':<12} {'P/T':<12} "
        f"{'SCG states':<12} {'mem KB':<10} {'ms tot':<10}"
    )
    print(header)
    print("-" * len(header))
    for r in rows:
        tcl = f"{r.tasks}/{r.cpus}/{r.locks}"
        pt = (
            f"{r.places}/{r.transitions}"
            if r.places is not None and r.transitions is not None
            else "-"
        )
        scg = str(r.scg_states) if r.scg_states is not None else "-"
        mem = str(r.memory_kb) if r.memory_kb is not None else "-"
        ms = str(r.ms_total) if r.ms_total is not None else "-"
        ok = "yes" if r.ok else "no"
        print(f"{r.case:<28} {ok:<4} {tcl:<12} {pt:<12} {scg:<12} {mem:<10} {ms:<10}")


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
        help="SCG state cap for full runs (default: 10000; use 50000+ for 100t)",
    )
    parser.add_argument(
        "--cases",
        nargs="+",
        help="case stems to run, e.g. pipeline-40t-8c-5l or 40t-8c-5l",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="run all pipeline-*.json cases",
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

    if not args.all and not args.cases:
        parser.error("specify --all or --cases")

    lbench_dir = args.lbench_dir.expanduser()
    if not lbench_dir.is_absolute():
        lbench_dir = (ROOT / lbench_dir).resolve()

    ptpn_bin = args.ptpn.expanduser()
    if not ptpn_bin.is_absolute():
        ptpn_bin = (ROOT / ptpn_bin).resolve()

    cases = discover_cases(lbench_dir, args.cases if not args.all else None)
    if not cases:
        print("error: no pipeline JSON files found", file=sys.stderr)
        return 1

    if not args.validate_tdg_only and any(
        "100t" in p.stem for p in cases
    ):
        print(
            "note: 100-task cases may need -m 50000 or higher to avoid SCG truncation",
            file=sys.stderr,
        )

    if args.dry_run:
        mode = "tdg-dot-only" if args.validate_tdg_only else f"full scg -m {args.max_states}"
        for path in cases:
            print(f"[dry-run] {path.name} ({mode})")
        return 0

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
        )
        rows.append(row)
        print(f"    {'ok' if row.ok else 'FAILED: ' + row.error}")

    summary_json = lbench_dir / "bench-summary.json"
    summary_csv = lbench_dir / "bench-summary.csv"
    write_summary(rows, summary_json, summary_csv)

    print()
    print_table(rows)
    print()
    print(f"summary: {summary_json}")
    print(f"summary: {summary_csv}")

    failed = sum(1 for r in rows if not r.ok)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
