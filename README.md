# PTPN - Priority Timed Petri Net Analyzer

A tool for analyzing and verifying real-time task system schedulability based on Priority Timed Petri Nets (PTPN) and state class analysis.

## 功能概述

将任务依赖图 (TDG) 转换为优先级时间 Petri 网 (PTPN)，通过状态类可达性图分析系统：

- **可调度性分析** - 判断任务是否满足截止时间
- **死锁检测** - 识别潜在的死锁情况
- **WCRT 分析** - 计算最坏情况响应时间
- **资源竞争分析** - 分析 CPU 和锁资源的分配

## 架构

```
JSON 文件 ──→ [JSON 解析] ──→ [TDG 模型] ──→ [TDG2PN 转换] ──→ [PTPN] ──→ [可达性分析]
                  │              │                │            │              │
              解析器          任务依赖图         转换器       Petri 网       状态类图
```

### 模块结构

| 模块 | 目录 | 说明 |
|------|------|------|
| **JSON 解析** | [src/json/](src/json/) | 解析 JSON 格式的任务图配置 |
| **TDG** | [src/tdg/](src/tdg/) | 任务依赖图数据结构 |
| **PTPN** | [src/petri/](src/petri/) | 优先级时间 Petri 网定义 |
| **TDG2PN** | [src/tdg2pn/](src/tdg2pn/) | TDG 到 PTPN 的转换算法 |
| **分析** | [src/analysis/](src/analysis/) | 状态类可达性分析 |

## 项目结构

```
src/
├── json/           # JSON 解析器
│   ├── json.h
│   └── json.cpp
├── tdg/            # 任务依赖图
│   ├── tdg.h
│   └── tdg.cpp
├── petri/          # Petri 网
│   ├── petri.h
│   ├── petri.cpp
│   ├── graph.h
│   └── graph.cpp
├── tdg2pn/         # TDG → PTPN 转换
│   ├── tdg2pn.h
│   └── tdg2pn.cpp
├── analysis/       # 可达性分析
│   ├── state.h
│   └── reachability.cpp
└── main.cpp        # 主程序入口
```

## 构建步骤

### Step 1: 安装依赖

**macOS:**
```bash
brew install boost ninja cmake
```

**Linux:**
```bash
sudo apt install libboost-all-dev ninja-build cmake
```

### Step 2: 构建项目

```bash
# 使用 Ninja 构建（推荐）
cmake -B build -G Ninja
cmake --build build
```

### Step 3: 运行测试

```bash
./build/test/ptpn_test
```

## 使用方法

### 命令行选项

```bash
./build/PTPN -f <input.json> [选项]
```

| 选项 | 说明 |
|------|------|
| `-f, --file` | 输入 JSON 文件（必需） |
| `-m, --max-states` | 可达性图最大状态数（默认：无限制） |
| `-e, --export-dot` | 解析后导出 TDG DOT 文件用于验证 |
| `--tina` | 导出为 Tina .net 格式 |
| `--romeo` | 导出为 Romeo XML 格式 |
| `-v, --version` | 显示版本信息 |

### 示例

```bash
# 基本用法
./build/PTPN -f example/task_graph.json

# 导出 DOT 便于调试
./build/PTPN -f example/task_graph.json --export-dot

# 限制状态数量
./build/PTPN -f example/task_graph.json -m 1000
```

## 输入格式 (JSON)

### 基本结构

```json
{
  "graph": {
    "name": "TaskGraph"
  },
  "configuration": {
    "num_cpus": 2,
    "cores_per_cpu": 4,
    "shared_locks": ["lock1", "lock2"]
  },
  "nodes": [
    {
      "id": "TaskA",
      "type": "periodic",
      "priority": 97,
      "core": 0,
      "time": [[3, 8]],
      "period": [100, 100],
      "locks": ["lock1"]
    },
    {
      "id": "TaskB",
      "type": "aperiodic",
      "priority": 98,
      "core": 1,
      "time": [[2, 5]],
      "locks": []
    }
  ],
  "edges": [
    {"source": "TaskA", "target": "TaskB", "label": "50", "style": "dashed"}
  ]
}
```

### 节点类型

| 类型 | 说明 | 必需字段 |
|------|------|----------|
| `periodic` | 周期任务 | `priority`, `core`, `time`, `period`, `locks` |
| `aperiodic` | 非周期任务 | `priority`, `core`, `time`, `locks` |
| `fork` | 分支节点 | 无 |
| `join` | 汇合节点 | 无 |
| `empty` | 空节点 | 无 |

### 字段说明

- `id`: 节点唯一标识符
- `type`: 节点类型 (`periodic` | `aperiodic` | `fork` | `join` | `empty`)
- `priority`: 任务优先级（数值越大优先级越高）
- `core`: 分配的核心 ID（从 0 开始）
- `time`: 执行时间区间 `[[start, end]]`
- `period`: 周期 `[@lower, @upper]`（仅 periodic 类型）
- `locks`: 使用的锁列表

## 输出说明

运行后生成：

- `ptpn.dot` - PTPN 结构图
- 可选：`*.dot` - 如果使用 `--export-dot`，导出 TDG 可视化图

## 核心算法

1. **TDG → PTPN 转换**: 将任务依赖图转换为优先级时间 Petri 网
2. **状态类可达性图构建**: 使用 DBM（差分边界矩阵）处理时间约束，按核心和优先级选择迁移
3. **DBM 最小化**: Floyd-Warshall 算法检查约束一致性
4. **抢占处理**: 分离 Z1/Z2 分别处理不可挂起/可挂起迁移的时间约束

## 状态类分析

状态类 = (M, Z1, Z2)，其中：
- `M`: 标识向量（令牌分布）
- `Z1`: 不可挂起迁移时间约束的 DBM
- `Z2`: 可挂起迁移时间约束的 DBM

DBM 表示时钟约束，使用 Floyd-Warshall 算法最小化。

## 依赖

- **C++17** 或更高版本
- **CMake 3.14+**
- **Boost** 库: `graph`, `filesystem`, `thread`, `date_time`
- **CLI11** 命令行解析
- **spdlog** 日志库
- **nlohmann/json** JSON 解析

## License

MIT License