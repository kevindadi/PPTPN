#!/usr/bin/env python3
"""Run p-bench / s-bench / t-bench examples and collect DOT exports + timing stats.

Outputs (mirroring each benchmark subfolder):
  example/ptpn/<suite>/<case>.{ptpn,scg}.dot
  example/romeo/<suite>/<case>.{ptpn,scg}.dot  (+ .cts)
  example/ptopner/<suite>/<case>.{ptpn,scg}.dot (+ .ppn when validation passes)

Summary written to example/bench-summary.json (and .csv).
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

ROOT = Path(__file__).resolve().parent
EXAMPLE = ROOT / "example"
DEFAULT_PTPN = ROOT / "build" / "ptpn"

BENCH_SUITES = ("p-bench", "s-bench", "t-bench")

RE_PLACES = re.compile(r"Places:\s*(\d+)")
RE_TRANSITIONS = re.compile(r"Transitions:\s*(\d+)")
RE_STATS_TDG2PN = re.compile(r"\[STATS\] TDG2PN lowering:\s*(\d+)\s*ms")
RE_STATS_SCG = re.compile(
    r"\[STATS\] SCG build:\s*(\d+)\s*ms "
    r"\(states=(\d+), edges=(\d+), dedup_hits=(\d+)(?:, truncated)?\)"
)
RE_STATS_SCG_DOT = re.compile(r"\[STATS\] SCG DOT export:\s*(\d+)\s*ms")
RE_STATS_TOTAL = re.compile(
    r"\[STATS\] TDG pipeline \(total\):\s*(\d+)\s*ms"
)


@dataclass
class RunStats:
    suite: str
    case: str
    profile: str
    ok: bool
    error: str = ""
    places: int | None = None
    transitions: int | None = None
    scg_states: int | None = None
    scg_edges: int | None = None
    scg_dedup_hits: int | None = None
    scg_truncated: bool = False
    ms_tdg2pn: int | None = None
    ms_scg_build: int | None = None
    ms_scg_dot: int | None = None
    ms_total: int | None = None
    ppn_exported: bool = False
    ppn_error: str = ""
    outputs: list[str] = field(default_factory=list)


PROFILES: dict[str, dict] = {
    "ptpn": {
        "policy": None,
        "export_romeo": False,
        "export_ppn": False,
    },
    "romeo": {
        "policy": "fixed_prior_with_resume",
        "export_romeo": True,
        "export_ppn": False,
    },
    "ptopner": {
        "policy": "fixed_prior_with_restart",
        "export_romeo": False,
        "export_ppn": True,
    },
}


def discover_cases(suites: list[str]) -> list[tuple[str, Path]]:
    cases: list[tuple[str, Path]] = []
    for suite in suites:
        suite_dir = EXAMPLE / suite
        if not suite_dir.is_dir():
            print(f"warning: missing suite directory {suite_dir}", file=sys.stderr)
            continue
        for path in sorted(suite_dir.glob("*.json")):
            cases.append((suite, path))
    return cases


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
    if m := RE_STATS_SCG_DOT.search(text):
        stats["ms_scg_dot"] = int(m.group(1))
    if m := RE_STATS_TOTAL.search(text):
        stats["ms_total"] = int(m.group(1))

    return stats


def run_case(
    ptpn_bin: Path,
    suite: str,
    input_path: Path,
    profile: str,
    out_dir: Path,
    max_states: int,
) -> RunStats:
    case = input_path.stem
    cfg = PROFILES[profile]
    out_dir.mkdir(parents=True, exist_ok=True)

    ptpn_dot = out_dir / f"{case}.ptpn.dot"
    scg_dot = out_dir / f"{case}.scg.dot"
    romeo_cts = out_dir / f"{case}.cts"
    ppn_file = out_dir / f"{case}.ppn"

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
    if cfg["policy"]:
        cmd.extend(["--policy", cfg["policy"]])
    if cfg["export_romeo"]:
        cmd.extend(["--romeo", str(romeo_cts)])

    result = RunStats(suite=suite, case=case, profile=profile, ok=False)

    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError as exc:
        result.error = str(exc)
        return result

    log = proc.stdout + proc.stderr
    parsed = parse_log(log)
    for key, value in parsed.items():
        setattr(result, key, value)

    outputs = [str(ptpn_dot), str(scg_dot)]
    if cfg["export_romeo"] and romeo_cts.exists():
        outputs.append(str(romeo_cts))

    # PToPNer .ppn is attempted separately so DOT export still succeeds when
    # validation fails (common for p-bench / s-bench interval tasks).
    if cfg["export_ppn"]:
        ppn_cmd = [
            str(ptpn_bin),
            "export",
            "ptopner",
            "-f",
            str(input_path),
            "-o",
            str(ppn_file),
            "--policy",
            "fixed_prior_with_restart",
        ]
        ppn_proc = subprocess.run(
            ppn_cmd, capture_output=True, text=True, check=False
        )
        if ppn_proc.returncode == 0 and ppn_file.exists():
            outputs.append(str(ppn_file))
            result.ppn_exported = True
        else:
            err = (ppn_proc.stderr or ppn_proc.stdout or "").strip()
            result.ppn_error = err.splitlines()[0][:200] if err else "export failed"

    result.outputs = outputs

    if proc.returncode != 0:
        result.error = (proc.stderr or proc.stdout or "unknown error").strip()
        if len(result.error) > 500:
            result.error = result.error[:500] + "..."
        return result

    missing = [p for p in (ptpn_dot, scg_dot) if not p.exists()]
    if missing:
        result.error = f"missing output: {', '.join(str(p) for p in missing)}"
        return result

    result.ok = True
    return result


def write_summary(rows: list[RunStats], json_path: Path, csv_path: Path) -> None:
    json_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "profiles": list(PROFILES.keys()),
        "suites": list(BENCH_SUITES),
        "runs": [asdict(r) for r in rows],
    }
    json_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    fieldnames = [
        "profile",
        "suite",
        "case",
        "ok",
        "places",
        "transitions",
        "scg_states",
        "scg_edges",
        "scg_dedup_hits",
        "scg_truncated",
        "ms_tdg2pn",
        "ms_scg_build",
        "ms_scg_dot",
        "ms_total",
        "ppn_exported",
        "ppn_error",
        "error",
    ]
    with csv_path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            data = asdict(row)
            writer.writerow({k: data.get(k) for k in fieldnames})


def print_table(rows: list[RunStats]) -> None:
    header = (
        f"{'profile':<9} {'suite':<9} {'case':<22} {'ok':<4} "
        f"{'P/T':<11} {'SCG s/e':<14} {'ms(scg/tot)':<14}"
    )
    print(header)
    print("-" * len(header))
    for r in rows:
        pt = (
            f"{r.places}/{r.transitions}"
            if r.places is not None and r.transitions is not None
            else "-"
        )
        scg = (
            f"{r.scg_states}/{r.scg_edges}"
            if r.scg_states is not None and r.scg_edges is not None
            else "-"
        )
        ms = (
            f"{r.ms_scg_build or '-':>4}/{r.ms_total or '-':<4}"
            if r.ok
            else r.error[:12]
        )
        print(
            f"{r.profile:<9} {r.suite:<9} {r.case:<22} "
            f"{'yes' if r.ok else 'no':<4} {pt:<11} {scg:<14} {ms:<14}"
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
        "-m",
        "--max-states",
        type=int,
        default=10_000,
        help="state-class reachability cap (default: 10000)",
    )
    parser.add_argument(
        "--suites",
        nargs="+",
        choices=BENCH_SUITES,
        default=list(BENCH_SUITES),
        help="benchmark suites to run (default: all)",
    )
    parser.add_argument(
        "--profiles",
        nargs="+",
        choices=list(PROFILES.keys()),
        default=list(PROFILES.keys()),
        help="export profiles to run (default: ptpn romeo ptopner)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print planned commands without executing",
    )
    args = parser.parse_args()

    if not args.dry_run and not args.ptpn.is_file():
        print(f"error: ptpn not found: {args.ptpn}", file=sys.stderr)
        print("build first: cmake -B build -G Ninja && cmake --build build", file=sys.stderr)
        return 1

    cases = discover_cases(args.suites)
    if not cases:
        print("error: no benchmark JSON files found", file=sys.stderr)
        return 1

    rows: list[RunStats] = []

    for suite, input_path in cases:
        case = input_path.stem
        for profile in args.profiles:
            out_dir = EXAMPLE / profile / suite
            if args.dry_run:
                print(f"[dry-run] {profile}/{suite}/{case} -> {out_dir}/")
                continue

            print(f"[*] {profile}/{suite}/{case} ...", flush=True)
            row = run_case(
                args.ptpn,
                suite,
                input_path,
                profile,
                out_dir,
                args.max_states,
            )
            rows.append(row)
            status = "ok" if row.ok else f"FAILED: {row.error}"
            print(f"    {status}")

    if args.dry_run:
        return 0

    summary_json = EXAMPLE / "bench-summary.json"
    summary_csv = EXAMPLE / "bench-summary.csv"
    write_summary(rows, summary_json, summary_csv)

    print()
    print_table(rows)
    print()
    print(f"summary: {summary_json}")
    print(f"summary: {summary_csv}")

    failed = sum(1 for r in rows if not r.ok)
    ppn_failed = sum(
        1 for r in rows if r.profile == "ptopner" and r.ok and not r.ppn_exported
    )
    if ppn_failed:
        print(f"note: {ppn_failed} ptopner run(s) wrote DOT but .ppn export failed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
