# PTPN JSON 输入格式规范

本文档定义了 PTPN (Priority Timed Petri Net) 工具的 JSON 输入格式规范。

## 概述

JSON 文件描述了一个任务依赖图 (Task Dependency Graph)，包含任务节点、任务间依赖关系以及系统配置信息。

## 完整 JSON Schema

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "PTPN Task Graph",
  "type": "object",
  "required": ["configuration", "nodes", "edges"],
  "properties": {
    "graph": {
      "type": "object",
      "properties": {
        "name": { "type": "string" }
      }
    },
    "configuration": {
      "type": "object",
      "required": ["num_cpus", "cores_per_cpu", "shared_locks"],
      "properties": {
        "num_cpus": { "type": "integer", "minimum": 1 },
        "cores_per_cpu": { "type": "integer", "minimum": 1 },
        "shared_locks": {
          "type": "array",
          "items": { "type": "string" },
          "description": "锁名称必须以 'mutex' 或 'spin' 开头"
        }
      }
    },
    "nodes": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["id", "type"],
        "properties": {
          "id": { "type": "string" },
          "type": {
            "type": "string",
            "enum": ["periodic", "aperiodic", "fork", "join", "empty"]
          },
          "priority": { "type": "integer" },
          "core": { "type": "integer" },
          "time": {
            "type": "array",
            "items": {
              "type": "array",
              "minItems": 2,
              "maxItems": 2,
              "items": { "type": "integer" }
            }
          },
          "period": {
            "type": "array",
            "minItems": 2,
            "maxItems": 2,
            "items": { "type": "integer" }
          },
          "locks": {
            "type": "array",
            "items": { "type": "string" }
          }
        }
      }
    },
    "edges": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["source", "target"],
        "properties": {
          "source": { "type": "string" },
          "target": { "type": "string" },
          "label": { "type": "string" },
          "style": { "type": "string" }
        }
      }
    }
  }
}
```

## 顶层结构

| 字段 | 类型 | 必需 | 说明 |
| ------ | ------ | ------ | ------ |
| `graph` | object | 否 | 图元数据 |
| `graph.name` | string | 否 | 图名称 |
| `configuration` | object | 是 | 系统配置 |
| `nodes` | array | 是 | 任务节点列表 |
| `edges` | array | 是 | 依赖边列表 |

---

## configuration 配置

```json
"configuration": {
  "num_cpus": 2,
  "cores_per_cpu": 4,
  "shared_locks": ["mutex1", "spin1", "mutex_global"],
  "policy": "fixed"
}
```

| 字段 | 类型 | 必需 | 说明 |
| ------ | ------ | ------ | ------ |
| `num_cpus` | integer | 是 | CPU 数量 |
| `cores_per_cpu` | integer | 是 | 每 CPU 核心数 |
| `shared_locks` | array | 是 | 全局共享锁列表，**所有节点使用的锁必须在此定义** |
| `policy` | string | 否 | 调度策略，默认 `fixed` |

### 调度策略 (policy)

| 策略 | 说明 |
| ------ | ------ |
| `fixed` | 固定优先级（默认） |
| `rm` | Rate Monotonic - 周期越短优先级越高 |
| `dm` | Deadline Monotonic - 截止时间越短优先级越高 |
| `edf` | Earliest Deadline First - 截止时间最早优先 |
| `llf` | Least Laxity First - 松弛时间最小优先 |
| `fifo` | 先来先服务 |
| `pip` | Priority Inheritance Protocol - 优先级继承协议 |
| `pcp` | Priority Ceiling Protocol - 优先级天花板协议 |
| `srp` | Stack Resource Policy - 栈资源策略 |

---

## nodes 节点

### 节点类型 (type)

| 类型 | 说明 | 必需字段 |
| ------ | ------ | ---------- |
| `periodic` | 周期任务 | `priority`, `core`, `time`, `period`, `locks` |
| `aperiodic` | 非周期任务 | `priority`, `core`, `time`, `locks` |
| `fork` | 分支节点 | 仅 `id`, `type` |
| `join` | 汇合节点 | 仅 `id`, `type` |
| `empty` | 空节点 | 仅 `id`, `type` |

### 字段说明

| 字段 | 类型 | 适用范围 | 说明 |
| ------ | ------ | ---------- | ------ |
| `id` | string | 所有 | 节点唯一标识符 |
| `type` | string | 所有 | 节点类型 |
| `priority` | integer | task 节点 | 优先级 (数值越大优先级越高) |
| `core` | integer | task 节点 | 分配的核心 ID (从 0 开始) |
| `time` | array | task 节点 | 执行时间区间数组，见下方详细规则 |
| `period` | array | periodic | 周期 [lower, upper]，仅 periodic 类型必需 |
| `locks` | array | task 节点 | 使用的锁列表 |

---

## 锁类型前缀规范

锁名称通过前缀区分类型：

| 前缀 | 锁类型 | 示例 |
| ------ | -------- | ------ |
| `mutex` | 互斥锁 | `mutex1`, `mutex_global`, `mutex_resource_a` |
| `spin` | 自旋锁 | `spin1`, `spin_irq`, `spin_device` |

**所有锁名称必须以 `mutex` 或 `spin` 开头，否则验证将失败。**

---

## 时间区间数量规则

任务执行时间根据锁的数量自动拆分：

```table
时间区间数量 = 2 × 锁数量 + 1
```

### 0 个锁

```table
┌─────────────────────────────────┐
│         整体执行时间              │
│            [0, 100]              │
└─────────────────────────────────┘
```

- 需要 1 个时间区间

### 1 个锁

```table
┌────────────┬─────────────┬────────────┐
│ 临界区前   │  临界区内   │  临界区后   │
│  [0, 10]   │   [10, 15]  │  [15, 30]  │
└────────────┴─────────────┴────────────┘
```

- 需要 3 个时间区间
- 区间语义：Pre-CS, CS:锁名, Post-CS

### 2 个锁（嵌套加锁）

```table
┌──────────┬────────────┬──────────┬──────────┬────────────┐
│ 锁1临界  │ 锁1内到    │ 锁2临界  │ 锁2临界区 │ 锁1临界区  │
│ 区前     │ 锁2临界前  │ 区内     │ 到锁1解锁 │ 后到结束   │
│  [0, 5]  │  [5, 10]   │ [10, 15] │ 前[15,20] │  [20, 40]  │
└──────────┴────────────┴──────────┴──────────┴────────────┘
```

- 需要 5 个时间区间
- 假设锁按顺序嵌套：先获取锁1，再获取锁2

### 时间区间数量速查表

| 锁数量 | 时间区间数 | 说明 |
| -------- | ----------- | ------ |
| 0 | 1 | 整体执行时间 |
| 1 | 3 | 临界区前、临界区内、临界区后 |
| 2 | 5 | 嵌套加锁的 5 个阶段 |
| 3 | 7 | 嵌套加锁的 7 个阶段 |
| n | 2n+1 | 通用公式 |

---

## edges 边

```json
"edges": [
  {"source": "TaskA", "target": "TaskB", "label": "10"},
  {"source": "TaskA", "target": "TaskC", "label": "50", "style": "dashed"}
]
```

| 字段 | 类型 | 必需 | 说明 |
| ------ | ------ | ------ | ------ |
| `source` | string | 是 | 源节点 ID |
| `target` | string | 是 | 目标节点 ID |
| `label` | string | 否 | 边标签（通常表示延迟） |
| `style` | string | 否 | 边样式（如 "dashed"） |

---

## 完整示例

```json
{
  "graph": {
    "name": "RealTimeTaskSystem"
  },
  "configuration": {
    "num_cpus": 2,
    "cores_per_cpu": 4,
    "shared_locks": ["mutex1", "spin1", "mutex_global", "spin_device"]
  },
  "nodes": [
    {
      "id": "TaskA",
      "type": "periodic",
      "priority": 97,
      "core": 0,
      "time": [[0, 10], [10, 15], [15, 30]],
      "period": [100, 100],
      "locks": ["mutex1"]
    },
    {
      "id": "TaskB",
      "type": "aperiodic",
      "priority": 98,
      "core": 1,
      "time": [[0, 5], [5, 10], [10, 15], [15, 20], [20, 40]],
      "locks": ["mutex1", "spin1"]
    },
    {
      "id": "TaskC",
      "type": "periodic",
      "priority": 99,
      "core": 0,
      "time": [[0, 100]],
      "period": [200, 200],
      "locks": []
    }
  ],
  "edges": [
    {"source": "TaskA", "target": "TaskB", "label": "10"},
    {"source": "TaskB", "target": "TaskC", "label": "20"}
  ]
}
```

---

## 验证规则

系统在解析 JSON 后会执行以下验证：

### 错误 (会导致解析失败)

| 验证项 | 错误信息 |
| -------- | ---------- |
| 重复节点 ID | `Duplicate node ID: X` |
| 未知节点类型 | `Unknown node type: X for node Y` |
| 无效核心编号 | `Invalid core number for node X: Y (valid range: 0-Z)` |
| 边引用未知节点 | `Edge references unknown source/target node: X` |
| Periodic 无 period | `Periodic task X missing or invalid period` |
| 使用未定义锁 | `Node X uses undefined lock: Y` |
| 无效锁前缀 | `Node X uses invalid lock prefix Y: must start with 'mutex' or 'spin'` |
| 时间区间数量不符 | `Node X has Y locks but Z time intervals (expected 2×Y+1=Z)` |
| 无效时间区间 | `Invalid time interval for node X: [Y, Z]` |

### 警告 (不会阻止解析)

| 验证项 | 警告信息 |
| -------- | ---------- |
| 无任务节点 | `No task nodes found in graph` |
| fork/join 带任务属性 | `Node X is fork/join but has task attributes` |

---

## 导出 DOT 格式

解析后的任务图可以导出为 DOT 格式进行可视化。DOT 输出会包含完整信息：

```dot
digraph G {
    TaskA [label="TaskA\nperiodic\nprio=97 core=0\ntime: [Pre] [0,10], [CS:mutex1] [10,15], [Post] [15,30]\nperiod=[100,100]\nlocks: [M]mutex1"];
    TaskB [label="TaskB\naperiodic\nprio=98 core=1\ntime: [Pre] [0,5], [CS:mutex1] [5,10], [CS:spin1] [10,15], [Post1] [15,20], [Post2] [20,40]\nlocks: [M]mutex1 [S]spin1"];
}
```

锁类型显示：

- `[M]` = mutex 互斥锁
- `[S]` = spin 自旋锁

时间区间语义：

- `[Pre]` = 临界区前
- `[CS:锁名]` = 临界区内
- `[Post]` = 临界区后
- `[PostN]` = 第 N 个临界区后

---

## 核心数量计算

有效核心范围：`0` 到 `num_cpus × cores_per_cpu - 1`

例如：`num_cpus=2, cores_per_cpu=4` 时，核心范围为 `0-7`

---

## TDG 到 Petri 网的统一 P/T 转换规则

本节描述 JSON 任务节点在 FIFO 调度策略下如何静态转换为 P/T Petri 网。`fixed` 固定优先级策略可以看作在这些基础模板上再增加高优先级任务的抢占/恢复路径。

### 总体约定

#### 库所含义

- `entry_p`：任务到达，等待申请核心
- `ready_p`：已经拿到核心，准备执行下一段
- `seg_i_done_p`：第 `i` 段执行完成后的状态
- `hold_lock_k_p`：任务已经持有第 `k` 个锁后的状态
- `exit_p`：任务完成
- `core_c_p`：核心资源库所
- `lock_x_p`：锁资源库所

#### 变迁含义

- `get_core_t`：申请核心，零时间变迁
- `lock_k_t`：申请第 `k` 个锁，零时间变迁
- `exec_i_t`：第 `i` 段执行，带时间变迁
- `release_t`：周期任务的周期释放变迁

#### 统一原则

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

因此：

> **FIFO 是基础网；fixed 是在 FIFO 基础网上增加高优先级任务的抢占与恢复结构。**
