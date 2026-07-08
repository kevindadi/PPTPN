#!/usr/bin/env python3
"""Resolve project root, CMake build directory, and the ptpn binary.

Resolution order for the build directory:
  1. explicit --ptpn / --build-dir argument (callers)
  2. environment variable PTPN_BUILD_DIR
  3. marker file .ptpn-build-dir at the repository root (written by build.py)
  4. default: <repo>/build

The ptpn executable is always <build_dir>/ptpn (or ptpn.exe on Windows).
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BUILD_DIR = ROOT / "build"
BUILD_DIR_MARKER = ROOT / ".ptpn-build-dir"
PTPN_EXE = "ptpn.exe" if os.name == "nt" else "ptpn"


def read_build_dir() -> Path:
    env = os.environ.get("PTPN_BUILD_DIR", "").strip()
    if env:
        path = Path(env).expanduser()
        return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()

    if BUILD_DIR_MARKER.is_file():
        line = BUILD_DIR_MARKER.read_text(encoding="utf-8").strip()
        if line:
            path = Path(line).expanduser()
            return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()

    return DEFAULT_BUILD_DIR.resolve()


def resolve_ptpn_bin(
    explicit: Path | None = None,
    *,
    build_dir: Path | None = None,
) -> Path:
    if explicit is not None:
        path = explicit.expanduser()
        return (ROOT / path).resolve() if not path.is_absolute() else path.resolve()

    base = build_dir.expanduser().resolve() if build_dir is not None else read_build_dir()
    return base / PTPN_EXE


def write_build_dir_marker(build_dir: Path) -> None:
    build_dir = build_dir.expanduser().resolve()
    try:
        stored = str(build_dir.relative_to(ROOT))
    except ValueError:
        stored = str(build_dir)
    BUILD_DIR_MARKER.write_text(stored + "\n", encoding="utf-8")


def default_ptpn_help() -> str:
    build_dir = read_build_dir()
    return f"{build_dir / PTPN_EXE} (from {build_dir.name}/; override with PTPN_BUILD_DIR or --ptpn)"


def ensure_ptpn(ptpn_bin: Path) -> None:
    if ptpn_bin.is_file():
        return
    print(f"error: ptpn not found: {ptpn_bin}", file=sys.stderr)
    print("build first: ./scripts/build.py", file=sys.stderr)
    print("or set PTPN_BUILD_DIR / pass --ptpn PATH", file=sys.stderr)
    raise SystemExit(1)
