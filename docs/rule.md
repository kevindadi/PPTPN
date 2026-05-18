# Unified TDG-to-Petri-Net P/T Rules

This document describes how TDG nodes are lowered into a static P/T Petri net under the FIFO scheduling policy. The fixed-priority variants keep the same base net and add extra preemption paths for higher-priority tasks.

## Conventions

### Places

- `entry_p`: task arrival, waiting for CPU acquisition
- `ready_p`: CPU acquired, ready to execute the next segment
- `seg_i_done_p`: state after execution segment `i`
- `hold_lock_k_p`: state after acquiring lock `k`
- `exit_p`: task completed
- `core_c_p`: CPU resource place
- `lock_x_p`: lock resource place

### Transitions

- `get_core_t`: zero-time CPU acquisition
- `lock_k_t`: zero-time lock acquisition
- `exec_i_t`: timed execution segment
- `release_t`: periodic release transition

## Rules

1. Only execution transitions and periodic release transitions carry time.
2. Lock acquisition is modeled as a separate zero-time transition.
3. Unlock is not modeled as a standalone transition; it is expressed by the output arcs of execution transitions.
4. Each execution segment maps to one timed transition.
5. The generated Petri net must keep the place-transition-place alternation.
6. The number of execution segments is `2 * lock_count + 1`.

## Task template without locks

### Aperiodic task

If `time = [C1]`:

```text
entry_p -> get_core_t -> ready_p -> exec_1_t(C1) -> exit_p
```

| Transition | Input places | Output places |
| --- | --- | --- |
| `get_core_t` | `entry_p`, `core_c_p` | `ready_p` |
| `exec_1_t` | `ready_p` | `exit_p`, `core_c_p` |

### Periodic task

The task body is lowered exactly like an aperiodic task:

```text
entry_p -> get_core_t -> ready_p -> exec_1_t(C1) -> exit_p
```

Periodic release is added separately:

```text
release_p -> release_t(P) -> release_p, entry_p
```

This release structure is created only when periodic activation is expressed by configuration or by an explicit self-loop release edge.

## Task template with one lock

If `locks = [L1]`, the time segments are:

- `C1`: before the critical section
- `C2`: inside the critical section
- `C3`: after the critical section

```text
entry_p
-> get_core_t
-> ready_p
-> exec_1_t(C1)
-> seg_1_done_p
-> lock_1_t
-> hold_1_p
-> exec_2_t(C2)
-> seg_2_done_p
-> exec_3_t(C3)
-> exit_p
```

| Transition | Input places | Output places |
| --- | --- | --- |
| `get_core_t` | `entry_p`, `core_c_p` | `ready_p` |
| `exec_1_t` | `ready_p` | `seg_1_done_p` |
| `lock_1_t` | `seg_1_done_p`, `lock_L1_p` | `hold_1_p` |
| `exec_2_t` | `hold_1_p` | `seg_2_done_p`, `lock_L1_p` |
| `exec_3_t` | `seg_2_done_p` | `exit_p`, `core_c_p` |

## Task template with two nested locks

If `locks = [L1, L2]`, the assumed order is:

1. acquire `L1`
2. acquire `L2`
3. release `L2`
4. release `L1`

The time segments are:

- `C1`: before `L1`
- `C2`: after `L1`, before `L2`
- `C3`: while holding both `L1` and `L2`
- `C4`: after releasing `L2`, while still holding `L1`
- `C5`: after releasing `L1`

```text
entry_p
-> get_core_t
-> ready_p
-> exec_1_t(C1)
-> seg_1_done_p
-> lock_1_t
-> hold_1_p
-> exec_2_t(C2)
-> seg_2_done_p
-> lock_2_t
-> hold_12_p
-> exec_3_t(C3)
-> seg_3_done_p
-> exec_4_t(C4)
-> seg_4_done_p
-> exec_5_t(C5)
-> exit_p
```

| Transition | Input places | Output places |
| --- | --- | --- |
| `get_core_t` | `entry_p`, `core_c_p` | `ready_p` |
| `exec_1_t` | `ready_p` | `seg_1_done_p` |
| `lock_1_t` | `seg_1_done_p`, `lock_L1_p` | `hold_1_p` |
| `exec_2_t` | `hold_1_p` | `seg_2_done_p` |
| `lock_2_t` | `seg_2_done_p`, `lock_L2_p` | `hold_12_p` |
| `exec_3_t` | `hold_12_p` | `seg_3_done_p`, `lock_L2_p` |
| `exec_4_t` | `seg_3_done_p` | `seg_4_done_p`, `lock_L1_p` |
| `exec_5_t` | `seg_4_done_p` | `exit_p`, `core_c_p` |

## General nested-lock pattern

For lock sequence:

```text
L1, L2, ..., Ln
```

The time segments are:

```text
C1, C2, ..., C(2n+1)
```

Meaning:

- `C1`: before acquiring `L1`
- `C2`: holding `L1`, before acquiring `L2`
- ...
- `Cn`: holding `L1...L(n-1)`, before acquiring `Ln`
- `C(n+1)`: holding all `n` locks
- `C(n+2)`: after releasing `Ln`
- ...
- `C(2n)`: after releasing `L2`, still holding `L1`
- `C(2n+1)`: after releasing `L1`, until completion

Release rules:

- `exec_(n+1)_t` releases `Ln`
- `exec_(n+2)_t` releases `L(n-1)`
- ...
- `exec_(2n)_t` releases `L1`
- `exec_(2n+1)_t` releases the CPU

## FIFO policy

FIFO does not add extra scheduling-control places.

1. A task must enter `entry_p` before execution.
2. A task must consume a token from `core_c_p` before running.
3. Lock competition is expressed naturally by lock tokens.
4. No extra preemption path is added.
5. All execution transitions are non-suspendable by default.

## Fixed-priority variants

The fixed-priority variants keep the FIFO base task chains and add static preemption paths for higher-priority tasks on the same core.

| Aspect | FIFO | `fixed_prior_with_restart` | `fixed_prior_with_resume` |
| --- | --- | --- | --- |
| Base task chain | kept | kept | kept |
| CPU resource model | kept | kept | kept |
| Lock resource model | kept | kept | kept |
| Preemption paths | none | added | added |
| Low-priority token after preemption | n/a | returns to task entry | moves to suspended place |
| Recovery after high-priority completion | n/a | low task restarts from entry | low task resumes at the same preemption point |
| `suspendable` | all `false` | lower-priority execution segments may become `true` | lower-priority execution segments may become `true` |
| Spin-lock preemption paths | n/a | not added | not added |

### `fixed_prior_with_restart`

This variant extends the FIFO base net with immediate preemption transitions that redirect the preempted low-priority task token back to its entry place.

```text
H_entry_p + L_preempt_p -> restart_preempt_t -> H_ready_p + L_entry_p
```

Use this when you want the cheaper static model in which a preempted task restarts from the beginning of its task chain.

### `fixed_prior_with_resume`

This variant extends the FIFO base net with a suspended place and a resume transition for each allowed preemption point.

```text
H_entry_p + L_preempt_p -> resume_preempt_t -> H_ready_p + L_suspended_p
H_exit_p + L_suspended_p -> resume_t -> L_preempt_p
```

Use this when you want the usual real-time preempt-resume semantics, where the preempted task continues from the exact point where it was interrupted.

In both variants:

1. Priority is already known during lowering because tasks are grouped by core and sorted before preemption arcs are added.
2. Only selected low-priority execution segments are marked `suspendable`.
3. `fork` and `join` nodes do not participate in preemption expansion.
4. No extra lock-specific preemption path is added once a `spin` lock is encountered.
