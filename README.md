# PTPN - Priority Timed Petri Net Analyzer

A tool for lowering task dependency graphs (TDGs) into Priority Timed Petri Nets (PTPNs) and analyzing real-time scheduling behavior.

## What it does

- Parse TDG input from JSON
- Validate task, lock, and graph structure
- Lower TDG models into PTPN models
- Export TDG and PTPN graphs for inspection
- Run analysis on the generated net

## Pipeline

```text
JSON -> Parser -> TDG -> TDG2PN -> PTPN -> Analysis
```

## Project layout

| Module | Directory | Description |
| --- | --- | --- |
| JSON parser | [src/json/](src/json/) | Parses and validates TDG JSON input |
| TDG | [src/tdg/](src/tdg/) | Task dependency graph model |
| PTPN | [src/petri/](src/petri/) | Petri net data structures and DOT export |
| TDG2PN | [src/tdg2pn/](src/tdg2pn/) | TDG-to-PTPN lowering |
| Analysis | [src/analysis/](src/analysis/) | Reachability and state-class analysis |
| Tests | [test/](test/) | Unit and integration tests |

## Build

### Dependencies

macOS:

```bash
brew install boost ninja cmake
```

Linux:

```bash
sudo apt install libboost-all-dev ninja-build cmake
```

### Compile

```bash
cmake -B build -G Ninja
cmake --build build
```

### Run tests

```bash
./build/test/ptpn_test
```

## Usage

```bash
./build/PTPN -f <input.json>
```

Common options:

| Option | Description |
| --- | --- |
| `-f, --file` | Input JSON file |
| `-m, --max-states` | Maximum number of states in reachability analysis |
| `-e, --export-dot` | Export TDG DOT output |
| `--tina` | Export Tina `.net` format |
| `--romeo` | Export Romeo XML format |
| `-v, --version` | Show version |

Examples:

```bash
./build/PTPN -f example/common.json
./build/PTPN -f example/common.json --export-dot
./build/PTPN -f example/common.json -m 1000
```

## Input documentation

- [docs/json_format.md](docs/json_format.md): JSON input format
- [docs/rule.md](docs/rule.md): TDG-to-PTPN lowering rules

## Outputs

Typical generated files:

- `ptpn.dot`: exported PTPN graph
- optional TDG DOT export when `--export-dot` is enabled

## Requirements

- C++17 or later
- CMake 3.14+
- Boost
- CLI11
- spdlog
- nlohmann/json
