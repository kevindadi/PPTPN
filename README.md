# PTPN

A Priority Timed Petri Net analyzer that lowers task dependency graphs (TDGs) into timed Petri nets with priorities, then builds state-class reachability graphs for real-time scheduling analysis.

## Features

- Parse and validate TDG JSON (tasks, locks, dependencies)
- Lower TDG models into PTPNs (multi-core, priorities, preemption, locks)
- Parse the PTPN domain language (`.ptpn`) directly, skipping TDG conversion
- Export TDG, PTPN, and state-class graphs (DOT)
- Export Romeo CTS and PToPNer `.ppn` formats
- State-class reachability analysis with multiple canonicalization modes

## Input Modes

| Mode | Command | Input | Pipeline |
|------|---------|-------|----------|
| **TDG** | `ptpn tdg -f model.json` | Task dependency graph JSON | JSON → TDG → PTPN → analysis |
| **Direct** | `ptpn ptpn -f model.ptpn` | PTPN source file | PTPN → analysis |

```text
# TDG path
JSON ──► Parser ──► TDG ──► TDG2PN ──► PTPN ──► State-Class Analysis
                                              └──► DOT / Romeo / PToPNer

# Direct path
.ptpn ──► Parser ──► PTPN ──► State-Class Analysis
                          └──► DOT / Romeo / PToPNer
```

## Quick Start

### Dependencies

- C++17
- CMake 3.27+
- Ninja (recommended)
- Boost (graph, filesystem, thread, date_time) — via vcpkg or your system package manager

**Linux (Debian/Ubuntu):**

```bash
sudo apt install libboost-all-dev ninja-build cmake
```

**macOS:**

```bash
brew install boost ninja cmake
```

Optional: set `VCPKG_ROOT` and CMake will pick up the vcpkg toolchain automatically (see `vcpkg.json`).

### Build

```bash
cmake -B build -G Ninja
cmake --build build
```

The executable is at `build/ptpn`.

### Run Tests

```bash
ctest --test-dir build --output-on-failure
```

## Usage

```bash
# Analyze from TDG JSON
./build/ptpn tdg -f example/single_lock/input.json

# Analyze from PTPN source
./build/ptpn ptpn -f example/common/simple.ptpn

# Help
./build/ptpn --help
./build/ptpn tdg --help
./build/ptpn ptpn --help
```

### Common Options

Both subcommands support:

| Option | Description |
|--------|-------------|
| `-f, --file` | Input file (required) |
| `-m, --max-states` | Max states in reachability graph (default: 10000) |
| `--canonicalization` | Canonicalization mode: `equality` (default), `max-lower`, `intersection` |
| `--ppn <path>` | Export PToPNer `.ppn` format |
| `--romeo <path>` | Export Romeo CTS format |
| `--tina <path>` | Export Tina `.net` format (not implemented yet) |
| `--debug` | Enable debug logging |
| `-v, --version` | Show version |

### Examples

```bash
# TDG: export Romeo format, cap at 1000 states
./build/ptpn tdg -f example/common/input.json \
  --romeo example/common/ptpn.cts -m 1000

# PTPN: use max-lower canonicalization
./build/ptpn ptpn -f example/common/simple.ptpn \
  --canonicalization max-lower

# TDG: export PToPNer format
./build/ptpn tdg -f example/single_lock/input.json \
  --ppn output.ppn
```

## Output Files

After analysis, artifacts are written to the **directory of the input file**:

| File | Description | TDG | PTPN |
|------|-------------|:---:|:----:|
| `tdg.dot` | TDG visualization | ✓ | |
| `wcet.json` | Per-task WCET summary | ✓ | |
| `ptpn.dot` | PTPN structure graph | ✓ | ✓ |
| `state-class-graph.dot` | State-class reachability graph | ✓ | ✓ |
| `--romeo` path | Romeo CTS export | ✓ | ✓ |
| `--ppn` path | PToPNer `.ppn` export | ✓ | ✓ |

## Project Layout

| Module | Directory | Description |
|--------|-----------|-------------|
| JSON parser | [src/json/](src/json/) | TDG JSON parsing and validation |
| TDG | [src/tdg/](src/tdg/) | Task dependency graph model |
| PTPN parser | [src/parser/](src/parser/) | `.ptpn` language parsing and building |
| PTPN core | [src/petri/](src/petri/) | Petri net data structures and export |
| TDG2PN | [src/tdg2pn/](src/tdg2pn/) | TDG → PTPN lowering |
| PToPNer | [src/tdg2ptopner/](src/tdg2ptopner/) | PToPNer validation and `.ppn` export |
| Analysis | [src/analysis/](src/analysis/) | State-class reachability analysis |
| Examples | [example/](example/) | JSON and `.ptpn` examples |
| Tests | [test/](test/) | Unit and integration tests |

## Documentation

| Document | Contents |
|----------|----------|
| [docs/json_format.md](docs/json_format.md) | TDG JSON input format |
| [docs/ptpn-language-spec.md](docs/ptpn-language-spec.md) | PTPN domain language specification |
| [docs/rule.md](docs/rule.md) | TDG → PTPN lowering rules |
| [docs/ptpn-formal-semantics.md](docs/ptpn-formal-semantics.md) | PTPN formal semantics |


## Third-Party Dependencies

Fetched automatically at build time via FetchContent:

- [CLI11](https://github.com/CLIUtils/CLI11) — command-line parsing
- [nlohmann/json](https://github.com/nlohmann/json) — JSON handling
- [spdlog](https://github.com/gabime/spdlog) / [fmt](https://github.com/fmtlib/fmt) — logging
- [GoogleTest](https://github.com/google/googletest) — test framework

Boost is provided via vcpkg or system packages. The [tools/PToPNer](tools/PToPNer/) submodule supplies PToPNer-related tooling.
