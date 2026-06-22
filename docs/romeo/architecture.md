# Roméo Architecture Map

## Goal
This document answers how `tools/romeo-2t` is modularized and what its main execution path is.

## Directory Responsibilities
- `tools/romeo-2t/model/` defines the core CTS-level data model, especially transitions, time intervals, valuations, parameters, variables, and the `CTS` container.
- `tools/romeo-2t/domains/` implements the symbolic state/state-class and time-domain representations used during exploration, especially DBM-backed timing constraints and successor-side zone operations.
- `tools/romeo-2t/graph/` owns execution jobs and graph exploration: it selects exploration behavior, builds the reachable state-class graph, manages waiting/passed structures, and exports the result in Graphviz form.
- `tools/romeo-2t/parser/` is the parser layer for `.cts` syntax. Its lexer/parser pair reads input, populates `ParserData`, constructs internal `CTS` objects, and pushes graph jobs into `ParserData.jobs`.
- `tools/romeo-2t/lang/` provides the internal expression, instruction, type, and evaluation language used by the parser and model objects to represent guards, assignments, constraints, and executable semantic fragments.
- `tools/romeo-2t/support/` and common utility headers provide cross-cutting infrastructure such as logging, color/output helpers, printable abstractions, integer/support types, hashing, and queue/heap utilities.
- `tools/romeo-2t/cli/` contains the command-line entry point that wires file loading, parsing, option handling, and job execution together.

## Main Execution Path
1. `tools/romeo-2t/cli/main.cc` accepts a `.cts` input path from the command line, opens the file, allocates `ParserData`, and starts parsing.
2. `yyparse()` consumes the `.cts` syntax and builds `ParserData.jobs`; in the parser grammar this includes pushing `GraphJob` instances after constructing the corresponding internal `CTS` representation.
3. `GraphJob::start()` begins the job-level execution path by applying options, selecting the computation mode through the job setup logic, and then dispatching graph generation through the graph job execution flow.
4. `Job::initial_state()` chooses the symbolic state representation to seed exploration, selecting untimed `VSState` or timed `VSClass` initialization depending on computation mode.
5. `GraphNode::build_graph()` performs forward expansion from the initial symbolic state, iterates successors, interns visited states in the passed structure, and constructs the state-class graph.
6. `GraphNode::export_graphviz()` traverses the built graph and prints the Graphviz `digraph` output, optionally followed by per-node state dumps.

## Semantic Core vs Support Code
- The semantic core is concentrated in `model/`, `domains/`, and the semantic parts of `lang/`: these directories contain the core CTS semantic data structures and the state-evolution machinery used during exploration.
- `parser/` is a translation boundary rather than the semantic engine: it maps `.cts` text into core objects and jobs, but the actual state-space meaning lives in the model/domain layers.
- `graph/` is the orchestration layer between semantics and output: it drives exploration using the domain/model abstractions and turns the result into a graph artifact.
- `support/` and CLI-facing utilities are support code: they matter for usability, logging, containers, and output formatting, but they are not where the CTS semantics are defined.

## Recommended Entry Points For Reading
- Quick lookup: parse path -> `tools/romeo-2t/cli/main.cc`, `tools/romeo-2t/parser/parser.y`, `tools/romeo-2t/parser/parser_data.hh`.
- Quick lookup: exploration path -> `tools/romeo-2t/graph/job.cc`, `tools/romeo-2t/graph/graph_node.cc`.
- Quick lookup: core semantic types -> `tools/romeo-2t/model/cts.hh`, `tools/romeo-2t/model/transition.hh`, `tools/romeo-2t/model/time_interval.hh`.
- Start with `tools/romeo-2t/cli/main.cc` to understand the top-level lifecycle from file input to job execution.
- Then read `tools/romeo-2t/parser/parser.y` together with `tools/romeo-2t/parser/parser_data.hh` to see how `.cts` syntax becomes `CTS` objects and `ParserData.jobs`.
- Next read `tools/romeo-2t/graph/job.cc` to understand computation-mode selection, `Job::initial_state()`, and how graph-oriented jobs invoke exploration.
- Follow with `tools/romeo-2t/graph/graph_node.cc` to inspect the actual graph expansion and Graphviz export path.
- Use `tools/romeo-2t/model/cts.hh`, `tools/romeo-2t/model/transition.hh`, and `tools/romeo-2t/model/time_interval.hh` as the core semantic data model references.
- Dive into `tools/romeo-2t/domains/` only after the above, because the state-class and DBM code is the densest layer and makes more sense once the job and model flow is clear.
