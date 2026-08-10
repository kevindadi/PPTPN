# PTPN (Rust port)

A Rust port of the C++ PTPN analyzer, focusing on the **core analysis
pipeline**: TDG JSON parsing, TDG → PTPN lowering, and state-class
reachability analysis (DBM clock zones, canonicalization, k-extrapolation,
metrics).

This port lives on the `rust-port` branch. The C++ implementation remains the
reference under the repo root.

## Scope

Implemented:

- `json` — TDG JSON parsing + validation (`src/json.rs`)
- `tdg` — task dependency graph model (`src/tdg.rs`)
- `petri` — PTPN model with saturating places (`src/petri.rs`)
- `tdg2pn` — TDG → PTPN lowering (`src/tdg2pn.rs`)
- `parser` — `.ptpn` domain language (`src/parser.rs`)
- `analysis` — DBM, state classes, canonicalization, scheduling, reachability
  graph, k-extrapolation, metrics (`src/analysis/`)

Not yet migrated: `tdg2romeo` (`.cts` export), `tdg2ptopner` (`.ppn` export),
and the full DOT/export surface of the C++ CLI. The CLI implements the two
core subcommands `ptpn tdg` and `ptpn ptpn` with the main analysis options.

## Build & test

```bash
cargo build --release      # binary: target/release/ptpn
cargo test                 # unit + integration tests (ported from test/)
cargo clippy
```

## Usage

```bash
# TDG mode
cargo run --release -- tdg -f ../example/p-bench/initial.json -m 100000
cargo run --release -- tdg -f ../example/p-bench/initial.json \
  --export-scg scg.dot --export-metrics metrics.json --extrapolation

# Direct .ptpn mode
cargo run --release -- ptpn -f model.ptpn --canonicalization max-lower
```

## Parity verification

The state-class graphs are verified against the C++ implementation using a
**name-normalized** comparison (transition/place IDs differ between the two
nets because vertex ordering is not guaranteed, but names derived from the TDG
are identical):

- Reachable marking sets (place names)
- Per-marking `E_struct` enabled sets (transition names)
- Edge multisets keyed by (source-state signature, transition name) with the
  firing/dwell time windows

`cargo test` covers the ported core test suites: analysis semantics,
extrapolation, saturation, metrics, and the `.ptpn` parser.

## Layout

```
rust/
  Cargo.toml
  src/
    lib.rs          # module tree
    main.rs         # CLI (tdg / ptpn subcommands)
    types.rs        # shared types, scheduling policies, TDG node variants
    json.rs         # TDG JSON parser + validation
    tdg.rs          # task dependency graph model
    petri.rs        # PTPN (places, transitions, saturating marking)
    tdg2pn.rs       # TDG -> PTPN lowering
    parser.rs       # .ptpn domain language
    analysis/
      dbm.rs        # Difference Bound Matrix
      state_class.rs# symbolic state class
      canonicalization.rs
      scheduling.rs
      ptpn_analysis.rs  # state-class reachability graph
      metrics.rs    # schedulability / response-time metrics
  tests/            # ported GoogleTest suites as Rust integration tests
```
