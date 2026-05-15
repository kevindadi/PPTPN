# PTPN - Priority Timed Petri Net Analyzer

A tool for analyzing and verifying real-time task system schedulability based on Priority Timed Petri Nets (PTPN) and state class analysis.

## Background

This tool converts Task Dependency Graphs (TDG) into Priority Timed Petri Nets, then analyzes the system through state class reachability graphs to determine:
- **Schedulability** - Whether tasks meet deadlines
- **Deadlock Detection** - Identify potential deadlocks
- **WCRT Analysis** - Compute worst-case response times
- **Resource Contention** - Analyze CPU and lock resource allocation

## Architecture

```
Input (DOT) ──→ [TDG Parser] ──→ [MatrixPTPN] ──→ [StateClassRG] ──→ Output
                  │                │                 │
              DAG/CLAP         Matrix Rep       Reachability
                  │                │                 │
              Task Graph        Petri Net          State Class
```

### Core Modules

| Module | File | Description |
|--------|------|-------------|
| **DAG/CLAP** | [dag.h](include/dag.h), [clap.h](include/clap.h) | Parse task dependency graphs in DOT format |
| **MatrixPTPN** | [matrix_ptpn.h](include/matrix_ptpn.h) | Matrix representation of PTPN (Pre/Post matrices) |
| **GraphPTPN** | [graph_ptpn.h](include/graph_ptpn.h) | Convert to Boost Graph for export and visualization |
| **StateClass** | [state_class.h](include/state_class.h) | DBM (Difference Bound Matrix) and state class structures |
| **StateClassRG** | [state_class_graph.h](include/state_class_graph.h) | State class reachability graph builder |

### Task Types

```cpp
PeriodicTask    // Periodic task (Period, WCET, Priority, Core, Locks)
APeriodicTask   // Aperiodic task (WCET, Priority, Core, Locks)
DistTask        // Distributed task
SyncTask        // Synchronization task
EmptyTask       // Empty task
```

### PTPN Structure

- **Places**: CPU core resources, lock resources, task states
- **Transitions**: Task execution with time interval `[α, β]` and priority π
- **Pre/Post Matrices**: Arc weights

### State Class Analysis

State class = (M, Z1, Z2), where:
- `M`: Marking vector (token distribution)
- `Z1`: DBM for non-suspendable transition time constraints
- `Z2`: DBM for suspendable transition time constraints

DBM represents clock constraints and is minimized using the Floyd-Warshall algorithm.

## Dependencies

- **C++17** or higher
- **Boost** libraries:
  - `graph` - Graph structures
  - `program_options` - Command-line arguments
  - `filesystem` - File operations
  - `log` / `log_setup` - Logging
  - `thread` - Multi-threading
  - `date_time` - Time handling

Install (macOS):
```bash
brew install boost
```

Install (Linux):
```bash
sudo apt install libboost-all-dev
```

## Build

```bash
cmake -B build -H .
cmake --build build
```

## Usage

### Basic Usage

```bash
./build/PTPN --cpus <num_cpus> --cores <cores_per_cpu> --file <task_graph.dot>
```

### Command Line Options

| Option | Description |
|--------|-------------|
| `--cpus` | Number of CPUs (required) |
| `--cores` | Number of cores per CPU (required) |
| `--file` | Task graph DOT file path (default: `dag.dot`) |
| `--deadline` | Set deadline for checking |
| `--max_states` | Maximum number of states in reachability graph |

### Examples

```bash
# Analyze task graph with 2 CPUs, 4 cores per CPU
./build/PTPN --cpus 2 --cores 4 --file example/common.dot

# Analyze simple task graph
./build/PTPN --cpus 1 --cores 2 --file example/fixed_time/common_fixed_lower.dot

# Limit maximum states
./build/PTPN --cpus 2 --cores 4 --file example/common.dot --max_states 1000
```

### Input Format (DOT)

Task graph uses DOT format, node label format:
```
{NodeName; TimeInterval; Priority; CoreID; [LockSet]}
```

Example:
```
digraph G {
    A [label = "{A;[100,100];97;0;[3,3]}";];
    A -> B [xlabel = "19";];
    B [label = "{B;98;0;[3,3]}";];
}
```

Where:
- `[100,100]` - Execution time interval (WCET)
- `97` - Priority (higher value = higher priority)
- `0` - Assigned CPU core
- `[3,3]` - Lock IDs used

### Output

Running generates:
- `matrix_ptpn.dot` - PTPN structure diagram
- `state_class_graph.dot` - State class reachability graph
- `state_class_graph.json` - State class reachability graph (JSON format)

## Example Task Graphs

Located in `example/` directory:
- `common.dot` - Standard task graph
- `fixed_time/` - Fixed time task graph examples

## Testing

```bash
# Run all tests
ctest --output-on-failure

# Run specific tests
./build/test_matrix_conversion
./build/test_state_class_example
```

## Project Structure

```
priority/
├── include/              # Header files
│   ├── dag.h             # Task type definitions
│   ├── clap.h            # TDG parser
│   ├── matrix_ptpn.h      # PTPN matrix representation
│   ├── graph_ptpn.h       # Boost Graph export
│   ├── state_class.h      # DBM and state class
│   └── state_class_graph.h  # Reachability graph builder
├── src/                  # Implementation files
├── test/                 # Test files
├── example/              # Example task graphs
├── main.cpp              # Main entry point
└── CMakeLists.txt        # Build configuration
```

## Core Algorithms

1. **TDG → PTPN Conversion**: Convert task dependency graph to Priority Timed Petri Net
2. **State Class Reachability Graph Construction**: Use DBM for time constraints, select transitions by core and priority
3. **DBM Minimization**: Floyd-Warshall algorithm for constraint consistency checking
4. **Preemption Handling**: Separate Z1/Z2 for non-suspendable/suspendable transition constraints

## License

MIT License
