# TDG 到 Petri 网的统一 P/T 转换规则

本节描述 JSON 任务节点在 FIFO 调度策略下如何静态转换为 P/T Petri 网。`fixed` 固定优先级策略可以看作在这些基础模板上再增加高优先级任务的抢占/恢复路径。

## 总体约定

### 库所含义

- `entry_p`：任务到达，等待申请核心
- `ready_p`：已经拿到核心，准备执行下一段
- `seg_i_done_p`：第 `i` 段执行完成后的状态
- `hold_lock_k_p`：任务已经持有第 `k` 个锁后的状态
- `exit_p`：任务完成
- `core_c_p`：核心资源库所
- `lock_x_p`：锁资源库所

### 变迁含义

- `get_core_t`：申请核心，零时间变迁
- `lock_k_t`：申请第 `k` 个锁，零时间变迁
- `exec_i_t`：第 `i` 段执行，带时间变迁
- `release_t`：周期任务的周期释放变迁

### 统一原则

1. **带时间的只有执行变迁和周期释放变迁**
2. **lock 必须单独建模为零时间变迁**
3. **unlock 不单独建模，而是并入对应执行变迁的输出弧**
4. **每一段执行时间对应一个带时间变迁**
5. **Petri 网必须保持库所—变迁—库所交替结构**
6. **时间段数量 = 2 × 锁数量 + 1**

---

## 无锁任务模板

### Aperiodic 任务

若 `time = [C1]`，则转换为：

```table
entry_p -> get_core_t -> ready_p -> exec_1_t(C1) -> exit_p
```

#### Aperiodic 任务的弧规则

| 变迁 | 输入库所 | 输出库所 |
| ------ | ------ | ------ |
| `get_core_t` | `entry_p`, `core_c_p` | `ready_p` |
| `exec_1_t` | `ready_p` | `exit_p`, `core_c_p` |

### Periodic 任务

若 `period = [P]`, `time = [C1]`，则转换为：

```table
release_p -> release_t(P) -> entry_p -> get_core_t -> ready_p -> exec_1_t(C1) -> exit_p
```

并且 `release_t` 还需要输出回 `release_p`，形成周期触发。

#### Periodic 任务的弧规则

| 变迁 | 输入库所 | 输出库所 |
| ------ | ------ | ------ |
| `release_t` | `release_p` | `release_p`, `entry_p` |
| `get_core_t` | `entry_p`, `core_c_p` | `ready_p` |
| `exec_1_t` | `ready_p` | `exit_p`, `core_c_p` |

---

## 单锁任务模板

设 `locks = [L1]`，时间拆分为：

- `C1`：临界区前
- `C2`：临界区内
- `C3`：临界区后

### 单锁任务的统一结构

```table
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

其中：

- `lock_1_t` 从 `lock_L1_p` 取 token
- `exec_2_t` 结束时释放 `lock_L1_p`
- `exec_3_t` 结束时释放 `core_c_p`

### 单锁任务的弧规则

| 变迁 | 输入库所 | 输出库所 |
| ------ | ------ | ------ |
| `get_core_t` | `entry_p`, `core_c_p` | `ready_p` |
| `exec_1_t` | `ready_p` | `seg_1_done_p` |
| `lock_1_t` | `seg_1_done_p`, `lock_L1_p` | `hold_1_p` |
| `exec_2_t` | `hold_1_p` | `seg_2_done_p`, `lock_L1_p` |
| `exec_3_t` | `seg_2_done_p` | `exit_p`, `core_c_p` |

---

## 双锁嵌套任务模板

设 `locks = [L1, L2]`，默认嵌套次序为：

1. 先获取 `L1`
2. 再获取 `L2`
3. 先释放 `L2`
4. 最后释放 `L1`

时间拆分为：

- `C1`：锁1前
- `C2`：持有锁1、获取锁2前
- `C3`：同时持有锁1和锁2
- `C4`：释放锁2后、仍持有锁1
- `C5`：释放锁1后到任务结束

### 双锁任务的统一结构

```table
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

资源释放规则：

- `lock_1_t`：获取 `L1`
- `lock_2_t`：获取 `L2`
- `exec_3_t` 结束时释放 `L2`
- `exec_4_t` 结束时释放 `L1`
- `exec_5_t` 结束时释放 `core`

### 双锁任务的弧规则

| 变迁 | 输入库所 | 输出库所 |
| ------ | ------ | ------ |
| `get_core_t` | `entry_p`, `core_c_p` | `ready_p` |
| `exec_1_t` | `ready_p` | `seg_1_done_p` |
| `lock_1_t` | `seg_1_done_p`, `lock_L1_p` | `hold_1_p` |
| `exec_2_t` | `hold_1_p` | `seg_2_done_p` |
| `lock_2_t` | `seg_2_done_p`, `lock_L2_p` | `hold_12_p` |
| `exec_3_t` | `hold_12_p` | `seg_3_done_p`, `lock_L2_p` |
| `exec_4_t` | `seg_3_done_p` | `seg_4_done_p`, `lock_L1_p` |
| `exec_5_t` | `seg_4_done_p` | `exit_p`, `core_c_p` |

---

## 一般 n 锁嵌套模板

设锁序列为：

```table
L1, L2, ..., Ln
```

则时间段为：

```table
C1, C2, ..., C(2n+1)
```

对应语义如下：

- `C1`：获取 `L1` 前
- `C2`：持有 `L1`，获取 `L2` 前
- ...
- `Cn`：持有 `L1...L(n-1)`，获取 `Ln` 前
- `C(n+1)`：持有全部 `n` 个锁
- `C(n+2)`：释放 `Ln` 后
- ...
- `C(2n)`：释放 `L2` 后，仍持有 `L1`
- `C(2n+1)`：释放 `L1` 后到结束

### 结构规则

#### 获取阶段

对 `i = 1..n`：

- `lock_i_t` 单独出现
- `lock_i_t` 从 `lock_Li_p` 取 token

#### 执行阶段

对 `j = 1..2n+1`：

- 每个 `Cj` 对应一个 `exec_j_t(Cj)`

#### 释放规则

- `exec_(n+1)_t` 结束时释放 `Ln`
- `exec_(n+2)_t` 结束时释放 `L(n-1)`
- ...
- `exec_(2n)_t` 结束时释放 `L1`
- `exec_(2n+1)_t` 结束时释放 `core`

---

## FIFO 调度策略

FIFO 模板本身不需要额外的调度控制库所：

1. 任务到达后进入 `entry_p`
2. 只有拿到 `core_c_p` token 才能进入执行链
3. 锁竞争由 `lock_x_p` token 自然表达
4. 不增加额外抢占路径
5. 所有执行变迁默认 `suspendable = false`

因此，FIFO 是最基础的静态转换形式。

---

## fixed 固定优先级调度策略

`fixed` 固定优先级策略不改变 FIFO 的基础任务链，而是在 FIFO 基础网上额外增加高优先级任务的抢占/恢复结构。

### 与 FIFO 的关系

| 项 | FIFO | fixed |
| ------ | ------ | ------ |
| 基础任务链 | 保留 | 保留 |
| 核心资源建模 | 保留 | 保留 |
| 锁资源建模 | 保留 | 保留 |
| 抢占路径 | 无 | 增加 |
| `suspendable` | 全部为 `false` | 低优先级可抢占执行段设为 `true` |

### 当前约束

- 低优先级任务在同一核心上显然是可挂起的
- 高优先级任务通过额外执行路径打断低优先级任务
- 抢占和恢复结构通过增加新的库所和变迁来静态展开
- 后续 PTPN 分析直接在展开后的网结构上进行，而不是在运行时动态解释调度策略

> **FIFO 是基础网；fixed 是在 FIFO 基础网上增加高优先级任务的抢占与恢复结构。**
