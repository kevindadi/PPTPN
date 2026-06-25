# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PTPN is a **Priority Timed Petri Net** analyzer for real-time scheduling analysis. It accepts two input modes:

1. **TDG mode**: Task Dependency Graph JSON → TDG → PTPN → state-class reachability analysis
2. **Direct mode**: `.ptpn` domain language → PTPN → state-class reachability analysis

The tool builds state-class reachability graphs with DBM (Difference Bound Matrix) clock zones and supports multiple canonicalization strategies.

## Roméo knowledge retrieval

When the user asks how Roméo implements a method, semantic rule, scheduling behavior, or source-level mechanism:

1. Check `docs/romeo/*.md` first.
2. If the knowledge docs already answer the question clearly, answer directly without scanning the whole `tools/romeo-2t` tree.
3. If the latest source fact must be confirmed, only do narrow verification:
   - one file;
   - one class;
   - one function body or function-local context;
   - one short call-chain fragment.
4. Prefer answers in the form: document conclusion first, then only the minimal source anchor needed to verify it.
5. Do not perform full-repository rescans for ordinary Roméo implementation questions.

## PToPNer knowledge retrieval

When the user asks about this repository's PTPN semantics, state-class construction, time advancement, scheduling / suspension / restore behavior, PToPNer export limits:

1. Check `docs/ptopner/*.md` first.
2. If those knowledge docs already answer the question clearly, answer directly without scanning the whole repository.
3. If more formal background is needed, next consult only the relevant authority doc:
   - `tools/PToPNer/README.md`
4. If the latest implementation fact must be confirmed, only do narrow verification:
   - one file;
   - one class;
   - one function body or function-local context;
   - one short call-chain fragment.
5. Prefer answers in the form: document conclusion first, then only the minimal source anchor needed to verify it.
6. Do not perform full-repository rescans for ordinary PToPNer implementation questions.
7. If a knowledge doc and current source disagree, trust the source and then update `docs/ptopner/*.md` so future answers stay docs-first.

## Build Commands

### Initial Build

```bash
cmake -B build -G Ninja
cmake --build build
```

The executable is `build/ptpn`.

### Rebuild After Changes

```bash
cmake --build build
```

### Clean Build

```bash
rm -rf build
cmake -B build -G Ninja
cmake --build build
```

### Compile Commands

The repo symlinks `compile_commands.json` at the root for clangd/IDE support. It's auto-generated during CMake configuration.

## Test Commands

### Run All Tests

```bash
ctest --test-dir build --output-on-failure
```

### Run a Single Test

GoogleTest discovers all tests from the `ptpn_test` executable. To run a specific test case:

```bash
./build/test/ptpn_test --gtest_filter='TestSuiteName.TestName'
```

Examples:

```bash
# Run one test
./build/test/ptpn_test --gtest_filter='PtpnAnalysisSemanticsTest.PicksEarliestFiringTimeBeforePriority'

# Run all tests in a suite
./build/test/ptpn_test --gtest_filter='PTPNParserTest.*'

# Run tests matching a pattern
./build/test/ptpn_test --gtest_filter='*Strict*'
```

### List All Tests

```bash
./build/test/ptpn_test --gtest_list_tests
```

## Running the Analyzer

### TDG Mode

```bash
./build/ptpn tdg -f example/single_lock/input.json
```

### Direct PTPN Mode

```bash
./build/ptpn ptpn -f example/common/simple.ptpn
```

### Common Options

- `-m, --max-states N`: Cap reachability graph at N states (default: 10000)
- `--canonicalization MODE`: Choose `equality` (default), `max-lower`, or `intersection`
- `--romeo PATH`: Export Romeo CTS format
- `--ppn PATH`: Export PToPNer `.ppn` format
- `--debug`: Enable debug logging

### Output Artifacts

Generated files appear in the **input file's directory**:

- `tdg.dot`: TDG visualization (TDG mode only)
- `wcet.json`: Per-task WCET summary (TDG mode only)
- `ptpn.dot`: PTPN structure graph
- `state-class-graph.dot`: State-class reachability graph

## Architecture

### Pipeline Stages

**TDG mode:**

```
JSON → json::parse_tdg_json() → tdg::TDG → converter::TDG2PN::transform() → petri::PTPN → state_class::build_reachability_graph()
```

**Direct mode:**

```
.ptpn source → parser::PTPNParser::parse() → petri::PTPN → state_class::build_reachability_graph()
```

### Module Responsibilities

| Module          | Path               | Purpose                                                    |
| --------------- | ------------------ | ---------------------------------------------------------- |
| **JSON parser** | `src/json/`        | Parse and validate TDG JSON input                          |
| **TDG**         | `src/tdg/`         | Task dependency graph model and DOT export                 |
| **PTPN parser** | `src/parser/`      | Lexer/parser for `.ptpn` domain language                   |
| **PTPN core**   | `src/petri/`       | Petri net data structures, DOT/Romeo/Tina export           |
| **TDG2PN**      | `src/tdg2pn/`      | TDG → PTPN lowering rules (see `docs/rule.md`)             |
| **PToPNer**     | `src/tdg2ptopner/` | Validation and `.ppn` export for PToPNer tool              |
| **Analysis**    | `src/analysis/`    | State-class reachability, DBM operations, canonicalization |

### Key Analysis Concepts

**State-class reachability** (`src/analysis/graph.cpp`, `graph.h`):

- Builds a graph where each node is a state-class (marking + clock zone represented as a DBM)
- Successor computation: `fire_transition()` → `canonicalize()` → check for duplicate states
- Canonicalization modes determine which states are merged (equality, max-lower-bound, intersection)

**DBM (Difference Bound Matrix)** (`src/analysis/dbm.cpp`, `dbm.h`):

- Represents clock constraints as inequalities: `x_i - x_j <= c_ij`
- Operations: `constrain_upper_bound()`, `future()`, `synchronize_clocks()`, `is_consistent()`
- The zero clock (`clock[0]`) anchors all absolute time constraints

**Scheduling semantics** (`src/analysis/scheduling.cpp`, `scheduling.h`):

- Implements time-first, priority-second firing rule
- Handles transition preemption for fixed-priority scheduling
- Suspension state tracking for preempted transitions

**Canonicalization** (`src/analysis/canonicalization.cpp`, `canonicalization.h`):

- Three modes: `EQUALITY` (exact DBM match), `MAX_LOWER_BOUND`, `INTERSECTION`
- Used to detect duplicate state-classes and bound the reachability graph

### TDG → PTPN Lowering Rules

The `converter::TDG2PN::transform()` function applies rules documented in `docs/rule.md`:

1. Each task gets `entry_p`, `ready_p`, per-segment execution places, and `exit_p`
2. Execution segments split at lock acquire/release boundaries: `2 * lock_count + 1` segments
3. CPU and lock resources are modeled as places with capacity 1
4. Only execution transitions and periodic release transitions carry time intervals
5. Lock acquisition/release are zero-time transitions

Preemption and priorities are handled by augmenting the base net with preemption arcs (see `docs/rule.md` § "Fixed-Priority Scheduling").

### PTPN Domain Language

The `.ptpn` format (spec: `docs/ptpn-language-spec.md`) lets you bypass TDG conversion and directly define:

- **places**: `places P0:Start, P1:Running:2` (id, optional name, optional capacity)
- **transitions**: `transitions T0[0,5]:exec_t1, T1[3,inf):release` (time interval, optional name, priority, core, suspendable)
- **arcs**: `arcs P0 -> T0, T0 -> P1` (with optional weights)
- **init**: `init P0:2, P1:1` (initial marking)

The parser (`src/parser/ptpn_parser.cpp`) outputs a `petri::PTPN` directly.

## Dependencies

**Fetched by CMake (FetchContent):**

- CLI11 (command-line parsing)
- nlohmann/json (JSON handling)
- spdlog + fmt (logging)
- GoogleTest (testing)

**System/vcpkg:**

- Boost (graph, filesystem, thread, date_time)

Set `VCPKG_ROOT` and CMake will automatically use the vcpkg toolchain if available.

## Language and Standards

- **C++17** (set in CMakeLists.txt)
- All core logic lives in `PTPN_core` static library (`CMakeLists.txt:67-96`)
- Main executable links against `PTPN_core` and `CLI11`
- Test executable links against `PTPN_core` and `gtest_main`

## Important Implementation Details

1. **State-class representation**: Each state-class is a pair `(marking, DBM)`. The DBM tracks all enabled transition clocks relative to the zero clock.

2. **Transition firing order**: The analysis picks the earliest-firing transition(s) first, then applies priority rules among simultaneous firings. See `src/analysis/scheduling.cpp:pick_next_transitions()`.

3. **Strict interval endpoints**: The `.ptpn` parser supports open intervals `(a,b)` and mixed `(a,b]` / `[a,b)`. The lowering stores `strict_lower` and `strict_upper` flags. Open lower bounds delay firing by one tick.

4. **Canonicalization state hashing**: The graph uses a custom hash and equality function (`state_class::StateHash`, `state_class::StateEqual`) that respects the selected canonicalization mode.

5. **Test access**: Tests compile with `PTPN_ENABLE_TEST_ACCESS` to expose private members for white-box testing (`test/CMakeLists.txt:26`).

## Documentation

- `docs/json_format.md`: TDG JSON schema
- `docs/ptpn-language-spec.md`: `.ptpn` syntax and semantics
- `docs/rule.md`: TDG → PTPN lowering rules
- `docs/ptpn-formal-semantics.md`: Formal semantics of PTPN state-class analysis
