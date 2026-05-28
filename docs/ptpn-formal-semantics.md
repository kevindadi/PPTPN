# PTPN 网发生规则的形式化定义

> 基于对项目的理解整理，待用户确认后可用于验证实现正确性

## 1. 网结构

PTPN 是一个九元组 $(P, T, F, \text{pri}, \text{core}, \text{susp}, \alpha, \beta, M_0)$：

- $P$：库所集合（Places）
- $T$：变迁集合（Transitions），分为：
  - **任务变迁**（Task Transitions）：$\text{core} \geq 0$，参与调度
  - **控制变迁**（Control Transitions）：$\text{core} < 0$，直接使能即触发，不参与调度决策
- $F \subseteq (P \times T) \cup (T \times P)$：流关系
- $\text{pri}: T \rightarrow \mathbb{N}$：优先级函数，数值越大优先级越高
- $\text{core}: T \rightarrow \mathbb{Z}$：核心分配函数，$\text{core} \geq 0$ 表示物理核心编号，$\text{core} < 0$ 表示控制变迁
- $\text{susp}: T \rightarrow \{0, 1\}$：可挂起标志，$1$ 表示可挂起，$0$ 表示不可挂起
- $\alpha: T \rightarrow \mathbb{N}$：最早激发时间（earliest firing time）
- $\beta: T \rightarrow \mathbb{N} \cup \{\infty\}$：最迟激发时间（latest firing time）
- $M_0$：初始标识

## 2. 状态定义

状态 $S$ 是一个四元组 $(M, C, E, X)$：

- $M$：当前标识（marking），$M: P \rightarrow \mathbb{N}$
- $C$：时钟向量，$C[t] = (lb_t, ub_t, s_t)$，其中：
  - $lb_t$：时钟下界（lower bound）
  - $ub_t$：时钟上界（upper bound）
  - $s_t \in \{\text{UNACTIVE}, \text{ACTIVE}, \text{SUSPENDED}\}$：时钟状态
- $E$：使能变迁集合，$E \subseteq T$
- $X$：活跃变迁集合，$X \subseteq T$（正在执行的）

时钟语义：
- $\text{UNACTIVE}$：变迁不在任何集合中，时钟不计时
- $\text{ACTIVE}$：变迁正在执行，时钟正常流逝
- $\text{SUSPENDED}$：变迁被挂起，时钟冻结

## 3. 使能条件

变迁 $t \in T$ 在标识 $M$ 下使能，当且仅当：
$$\forall p \in ^\bullet t: M(p) \geq 1$$
（所有前置库所都至少有一个 token）

控制变迁 $t$（$\text{core} < 0$）使能即触发，无时间约束。

## 4. 调度规则

### 4.1 活跃/挂起选择

对于每个核心 $c \in \mathbb{N}$，从使能变迁集合 $E_c = \{t \in E \mid \text{core}(t) = c\}$ 中选择：
$$X_c = \underset{t \in E_c}{\text{argmax}} \; \text{pri}(t)$$

（如果多个变迁优先级相同，选择其中一个，剩余的不在 $X$ 中也不在 $S$ 中）

对于每个非活跃但使能的变迁 $t \in E \setminus X$：
- 如果 $\exists a \in X$ 且 $\text{core}(a) = \text{core}(t)$ 且 $\text{pri}(a) > \text{pri}(t)$ 且 $\text{susp}(t) = 1$：
  - $t$ 进入挂起集 $S$
- 否则 $t$ 保持 UNACTIVE（既不在 $X$ 也不在 $S$）

### 4.2 挂起语义（分离记录模型）

当变迁 $t$ 从 ACTIVE 变为 SUSPENDED 时：
- 记录当前已消耗时间 $c_t$ 到时钟中
- 恢复时，$lb$ 从 $c_t$ 继续，而非从 0 开始

形式化：
$$\text{suspend}(t, c_t): C[t].s_t \leftarrow \text{SUSPENDED}, \; C[t].lb \leftarrow c_t$$

## 5. 时间推进

令 $\text{min\_ub} = \min_{t \in X} C[t].ub$（所有活跃变迁的最紧上界）

时间推进 $\Delta$ 后：
- 对所有 $t \in X$（活跃变迁）：
  $$C[t].lb \leftarrow C[t].lb + \Delta$$
  $$C[t].ub \leftarrow C[t].ub + \Delta$$
- 对所有 $t \in S$（挂起变迁）：$C$ 保持不变

注意：这里的上界推进方式有争议，详见第 8 节。

## 6. 变迁激发条件

变迁 $t$ 可以激发，当且仅当：
1. $t \in X$（变迁在活跃集合中）
2. 满足时间窗口：$\max(\alpha(t), C[t].lb) \leq \min(\beta(t), C[t].ub)$

激发时：
1. 更新标识：$M' = M - ^\bullet t + t^\bullet$
2. 重置时钟：$C'[t] \leftarrow (0, \beta(t), \text{UNACTIVE})$
3. 重新计算 $E', X', S'$

## 7. 抢占与恢复

当高优先级变迁 $a$ 在核心 $c$ 上激活时：
1. 对于同一核心 $c$ 上任何活跃的低优先级可挂起变迁 $t \in X_c \setminus \{a\}$：
   - 执行 $\text{suspend}(t, C[t].lb)$，记录冻结时间
2. $a$ 进入活跃集合 $X_c$

当高优先级变迁 $a$ 完成或离开活跃集合时：
1. 对于同一核心 $c$ 上挂起的可挂起变迁 $t \in S_c$：
   - 如果 $\nexists a' \in X_c$ 且 $\text{pri}(a') > \text{pri}(t)$：
   - 执行 $\text{restore}(t)$：$C[t].s_t \leftarrow \text{ACTIVE}$，时钟继续从 $C[t].lb$ 流逝

## 8. 争议点（待用户确认）

### 8.1 lower_bound 语义

**问题**：$lb$ 的含义是什么？

**理解 A**：$lb$ 是"从使能开始的绝对时间"
- 时间推进时：$lb$ 和 $ub$ 都增加
- 激发时间计算：$\max(\alpha, lb)$

**理解 B**：$lb$ 是"已消耗的执行时间"
- 时间推进时：$lb$ 不变（因为已经消耗），$ub$ 减少（deadline 逼近）
- 激发时间计算：$\alpha + lb$（必须等最早时间 + 已消耗时间）

当前实现使用理解 A（$lb$ 和 $ub$ 都推进）。

### 8.2 挂起时是否记录 WCET

**问题**：`make_suspended(current_time, original_wcet)` 是否正确？

PTPN 中每个变迁有固定的 WCET（通过 $\beta$ 隐含）。挂起时：
- 记录当前时间 $c_t$
- 恢复时从 $c_t$ 继续

**关键问题**：恢复后，变迁还能执行多久？
- 如果 WCET 是硬性的，恢复后只能执行剩余时间：$wcet - c_t$
- 如果 WCET 可以重新计时，恢复后完整执行 $wcet$

当前实现假设恢复后继续执行剩余部分（第一种理解）。

### 8.3 活跃集合确定时机

**问题**：$X$（活跃）是在时间推进前还是推进后确定？

当前实现：
1. 从 $M$ 计算 $E$
2. 从 $E$ 计算 $X$ 和 $S$
3. 时间推进（$X$ 中的时钟前进）
4. 检查 $X$ 中哪些可以激发

另一种理解：
1. 时间推进到边界
2. 在推进后的状态下重新计算 $E, X, S$
3. 检查 $X$ 中哪些可以激发

两种方式在抢占场景下可能有不同结果。