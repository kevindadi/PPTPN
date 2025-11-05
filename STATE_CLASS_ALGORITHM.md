# 优先级时间Petri网状态类生成算法伪代码
# Priority-Time Petri Net State Class Generation Algorithm Pseudocode

本文档描述了带挂起/恢复语义(suspend/resume semantics)的优先级时间Petri网(Priority-Time Petri Net, P-PTPN)的状态类生成算法.

---

## 1. DBM操作 / DBM Operations

### 1.1 数据结构 / Data Structures

```pseudocode
// DBM (Difference Bound Matrix) 表示时间约束区间
// DBM[i][j] 表示约束: x_i - x_j <= bound
// x_0 为参考时钟(通常固定为0)

struct DBM {
    matrix: int[n][n]     // 约束矩阵,n为时钟数量
    frozen: Set<int>      // 冻结时钟集合(挂起变迁的时钟)
}

struct StateClass {
    marking: int[]        // 标识向量 M
    Z1: DBM              // 不可挂起变迁的时间约束区间
    Z2: DBM              // 可挂起变迁的时间约束区间
    enabled: Set<int>    // 当前使能的变迁集合
    suspended: Set<int>  // 当前被挂起的变迁集合
    clock_map: Map<int, int>  // 变迁ID -> 时钟索引映射
}
```

### 1.2 添加时钟 / Add Clock

```pseudocode
function ADD_CLOCK(DBM z, transition_id: int) -> int:
    // 添加新时钟并初始化约束
    // Add new clock and initialize constraints
    
    clock_idx = z.size()
    z.resize(z.size() + 1)
    
    // 初始化: x_clock_idx - x_0 <= INF, x_0 - x_clock_idx <= 0
    // Initialize: x_clock_idx - x_0 <= INF, x_0 - x_clock_idx <= 0
    z.set_constraint(clock_idx, 0, INF)
    z.set_constraint(0, clock_idx, 0)
    
    // 设置自约束: x_i - x_i <= 0
    // Set self-constraint: x_i - x_i <= 0
    z.set_constraint(clock_idx, clock_idx, 0)
    
    return clock_idx
end function
```

### 1.3 移除时钟 / Remove Clock

```pseudocode
function REMOVE_CLOCK(DBM z, clock_idx: int):
    // 移除时钟(当变迁不再使能时)
    // Remove clock (when transition is no longer enabled)
    
    if clock_idx >= z.size() then
        return
    end if
    
    // 移除该时钟的所有约束
    // Remove all constraints for this clock
    
    // 方式1: 从矩阵中移除行列(重新构建)
    // Method 1: Remove row/column from matrix (rebuild)
    new_matrix = new int[z.size()-1][z.size()-1]
    
    for i = 0 to z.size()-1:
        for j = 0 to z.size()-1:
            if i < clock_idx and j < clock_idx then
                new_matrix[i][j] = z.matrix[i][j]
            else if i < clock_idx and j > clock_idx then
                new_matrix[i][j-1] = z.matrix[i][j]
            else if i > clock_idx and j < clock_idx then
                new_matrix[i-1][j] = z.matrix[i][j]
            else if i > clock_idx and j > clock_idx then
                new_matrix[i-1][j-1] = z.matrix[i][j]
            end if
        end for
    end for
    
    z.matrix = new_matrix
    z.size() = z.size() - 1
end function
```

### 1.4 冻结时钟 / Freeze Clock

```pseudocode
function FREEZE_CLOCK(DBM z1, DBM z2, clock_idx: int, transition_id: int):
    // 冻结时钟:将时钟从Z1移动到Z2(挂起变迁)
    // Freeze clock: move clock from Z1 to Z2 (suspend transition)
    
    // 获取当前时钟的约束
    // Get current constraints for the clock
    constraints = {}
    for i = 0 to z1.size()-1:
        constraints[i] = z1.get_constraint(clock_idx, i)
        constraints[-i] = z1.get_constraint(i, clock_idx)
    end for
    
    // 确保Z2有足够的时钟
    // Ensure Z2 has enough clocks
    if clock_idx >= z2.size() then
        z2.resize(clock_idx + 1)
    end if
    
    // 复制约束到Z2
    // Copy constraints to Z2
    for i = 0 to z1.size()-1:
        z2.set_constraint(clock_idx, i, constraints[i])
        z2.set_constraint(i, clock_idx, constraints[-i])
    end for
    
    // 在Z1中移除该时钟(或标记为无效)
    // Remove clock from Z1 (or mark as invalid)
    // 注意:实际实现中可能保留时钟但标记为冻结
    // Note: actual implementation may keep clock but mark as frozen
    
    // 标记为冻结状态
    // Mark as frozen state
    z2.frozen.add(clock_idx)
end function
```

### 1.5 解冻时钟 / Unfreeze Clock

```pseudocode
function UNFREEZE_CLOCK(DBM z1, DBM z2, clock_idx: int, transition_id: int):
    // 解冻时钟:将时钟从Z2移回Z1(恢复变迁)
    // Unfreeze clock: move clock from Z2 back to Z1 (resume transition)
    
    // 获取Z2中当前时钟的约束
    // Get current constraints from Z2
    constraints = {}
    for i = 0 to z2.size()-1:
        constraints[i] = z2.get_constraint(clock_idx, i)
        constraints[-i] = z2.get_constraint(i, clock_idx)
    end for
    
    // 确保Z1有足够的时钟
    // Ensure Z1 has enough clocks
    if clock_idx >= z1.size() then
        z1.resize(clock_idx + 1)
    end if
    
    // 复制约束到Z1
    // Copy constraints to Z1
    for i = 0 to z2.size()-1:
        z1.set_constraint(clock_idx, i, constraints[i])
        z1.set_constraint(i, clock_idx, constraints[-i])
    end for
    
    // 从冻结集合中移除
    // Remove from frozen set
    z2.frozen.remove(clock_idx)
end function
```

### 1.6 时间推进 / Time Elapse

```pseudocode
function TIME_ELAPSE(DBM z, delta: int):
    // 时间推进:所有非冻结时钟同步增加delta
    // Time elapse: all non-frozen clocks increase synchronously by delta
    
    if delta <= 0 then
        return
    end if
    
    // 对于非冻结时钟,放宽上界约束(相对于参考时钟)
    // For non-frozen clocks, relax upper bound constraints (relative to reference clock)
    for i = 1 to z.size()-1:
        if not z.frozen.contains(i) then
            // 放宽上界: x_i - x_0 <= INF
            // Relax upper bound: x_i - x_0 <= INF
            current_upper = z.get_constraint(i, 0)
            if current_upper != INF then
                z.set_constraint(i, 0, INF)
            end if
        end if
    end for
    
    // 最小化DBM以传播约束
    // Minimize DBM to propagate constraints
    CANONICALIZE(z)
end function
```

### 1.7 触发限制 / Restrict For Firing

```pseudocode
function RESTRICT_FOR_FIRING(DBM z, transition_id: int, 
                             alpha: int, beta: int) -> DBM:
    // 限制DBM以反映变迁的触发时间窗口 [α, β]
    // Restrict DBM to reflect transition's firing time window [α, β]
    
    clock_idx = transition_id + 1  // +1 for reference clock
    
    if clock_idx >= z.size() then
        return z  // 时钟不存在,返回原DBM
    end if
    
    // 创建新的DBM副本
    // Create new DBM copy
    result = z.copy()
    
    // 设置下界约束: x_clock - x_0 >= α  =>  x_0 - x_clock <= -α
    // Set lower bound: x_clock - x_0 >= α  =>  x_0 - x_clock <= -α
    current_lower = -result.get_constraint(0, clock_idx)
    if alpha > current_lower then
        result.set_constraint(0, clock_idx, -alpha)
    end if
    
    // 设置上界约束: x_clock - x_0 <= β
    // Set upper bound: x_clock - x_0 <= β
    current_upper = result.get_constraint(clock_idx, 0)
    if beta != INF and (current_upper == INF or beta < current_upper) then
        result.set_constraint(clock_idx, 0, beta)
    end if
    
    // 最小化并检查一致性
    // Minimize and check consistency
    CANONICALIZE(result)
    
    if result.is_empty() then
        return EMPTY_DBM  // 无解
    end if
    
    return result
end function
```

### 1.8 规范化 / Canonicalize

```pseudocode
function CANONICALIZE(DBM z):
    // 使用Floyd-Warshall算法最小化DBM
    // Minimize DBM using Floyd-Warshall algorithm
    
    n = z.size()
    
    // Floyd-Warshall: 计算所有时钟对之间的最短路径
    // Floyd-Warshall: compute shortest paths between all clock pairs
    for k = 0 to n-1:
        for i = 0 to n-1:
            if z.matrix[i][k] == INF then
                continue
            end if
            
            for j = 0 to n-1:
                if z.matrix[k][j] == INF then
                    continue
                end if
                
                new_bound = z.matrix[i][k] + z.matrix[k][j]
                if z.matrix[i][j] == INF or new_bound < z.matrix[i][j] then
                    z.matrix[i][j] = new_bound
                end if
            end for
        end for
    end for
    
    // 检查一致性(是否存在负环)
    // Check consistency (negative cycles)
    for i = 0 to n-1:
        if z.matrix[i][i] < 0 then
            // 存在矛盾,DBM为空
            // Contradiction exists, DBM is empty
            mark_as_empty(z)
        end if
    end for
end function
```

---

## 2. 触发/挂起/恢复规则 / Firing/Suspend/Resume Rules

### 2.1 检查变迁使能 / Check Transition Enabled

```pseudocode
function IS_ENABLED(state: StateClass, transition_id: int, net: MatrixPTPN) -> bool:
    // 检查变迁是否使能(基于标识)
    // Check if transition is enabled (based on marking)
    
    t = net.get_transition(transition_id)
    marking = state.marking
    
    // 检查所有输入库所是否有足够的token
    // Check if all input places have sufficient tokens
    for each place p in net.places:
        required = net.Pre[p][transition_id]
        if marking[p] < required then
            return false
        end if
    end for
    
    return true
end function
```

### 2.2 检查变迁挂起 / Check Transition Suspended

```pseudocode
function IS_SUSPENDED(state: StateClass, transition_id: int, 
                      enabled_transitions: Set<int>, net: MatrixPTPN) -> bool:
    // 检查变迁是否被挂起
    // Check if transition is suspended
    
    t = net.get_transition(transition_id)
    
    // 只有可挂起变迁才能被挂起
    // Only suspendable transitions can be suspended
    if not t.suspendable then
        return false
    end if
    
    core = t.core
    priority = t.priority
    
    // 检查同一核心上是否有更高优先级的不可挂起变迁使能
    // Check if higher priority non-suspendable transition on same core is enabled
    for each other_t in enabled_transitions:
        if other_t == transition_id then
            continue
        end if
        
        other_trans = net.get_transition(other_t)
        
        // 同一核心,不可挂起,且优先级更高(数值更小)
        // Same core, non-suspendable, and higher priority (smaller value)
        if other_trans.core == core and 
           not other_trans.suspendable and 
           other_trans.priority < priority then
            return true  // 被挂起 / Suspended
        end if
    end for
    
    return false
end function
```

### 2.3 触发变迁 / Fire Transition

```pseudocode
function FIRE_TRANSITION(state: StateClass, transition_id: int, 
                         firing_time: double, net: MatrixPTPN) -> StateClass:
    // 触发变迁并生成后继状态
    // Fire transition and generate successor state
    
    new_state = state.copy()
    t = net.get_transition(transition_id)
    clock_idx = transition_id + 1
    
    // 1. 更新标识: M' = M - Pre + Post
    // Update marking: M' = M - Pre + Post
    new_state.marking = state.marking.copy()
    for each place p in net.places:
        new_state.marking[p] -= net.Pre[p][transition_id]
        new_state.marking[p] += net.Post[transition_id][p]
        
        // 考虑库所容量限制
        // Consider place capacity constraints
        if net.places[p].capacity != INF then
            new_state.marking[p] = min(new_state.marking[p], 
                                       net.places[p].capacity)
        end if
    end for
    
    // 2. 更新时间
    // Update time
    new_state.cumulative_time = state.cumulative_time + firing_time
    
    // 3. 重置触发变迁的时钟
    // Reset clock of fired transition
    if t.suspendable then
        new_state.Z2.reset_clock(clock_idx)
    else
        new_state.Z1.reset_clock(clock_idx)
    end if
    
    // 4. 更新DBM约束(为新使能的变迁添加时间约束)
    // Update DBM constraints (add time constraints for newly enabled transitions)
    UPDATE_DBM_CONSTRAINTS(new_state, net)
    
    // 5. 重新计算挂起/恢复状态
    // Recompute suspension/resume status
    RECOMPUTE_SUSPENSION(new_state, net)
    
    return new_state
end function
```

### 2.4 更新挂起/恢复状态 / Update Suspension/Resume Status

```pseudocode
function RECOMPUTE_SUSPENSION(state: StateClass, net: MatrixPTPN):
    // 重新计算所有变迁的挂起/恢复状态
    // Recompute suspension/resume status for all transitions
    
    // 1. 找出所有使能的变迁
    // Find all enabled transitions
    enabled = {}
    for t = 0 to net.num_transitions()-1:
        if IS_ENABLED(state, t, net) then
            enabled.add(t)
        end if
    end for
    
    state.enabled = enabled
    
    // 2. 检查每个使能变迁的挂起状态
    // Check suspension status for each enabled transition
    suspended = {}
    for each t in enabled:
        if IS_SUSPENDED(state, t, enabled, net) then
            suspended.add(t)
            
            // 如果变迁从非挂起变为挂起,冻结时钟
            // If transition changes from non-suspended to suspended, freeze clock
            clock_idx = t + 1
            if not state.suspended.contains(t) then
                FREEZE_CLOCK(state.Z1, state.Z2, clock_idx, t)
            end if
        else
            // 如果变迁从挂起变为非挂起,解冻时钟
            // If transition changes from suspended to non-suspended, unfreeze clock
            clock_idx = t + 1
            if state.suspended.contains(t) then
                UNFREEZE_CLOCK(state.Z1, state.Z2, clock_idx, t)
            end if
        end if
    end for
    
    state.suspended = suspended
end function
```

### 2.5 更新DBM约束 / Update DBM Constraints

```pseudocode
function UPDATE_DBM_CONSTRAINTS(state: StateClass, net: MatrixPTPN):
    // 为所有使能变迁设置时间约束
    // Set time constraints for all enabled transitions
    
    num_transitions = net.num_transitions()
    
    // 确保DBM大小正确
    // Ensure DBM size is correct
    if state.Z1.size() < num_transitions + 1 then
        state.Z1.resize(num_transitions + 1)
    end if
    if state.Z2.size() < num_transitions + 1 then
        state.Z2.resize(num_transitions + 1)
    end if
    
    // 为每个使能变迁设置时间约束
    // Set time constraints for each enabled transition
    for each t in state.enabled:
        clock_idx = t + 1
        trans = net.get_transition(t)
        alpha = trans.time_interval.earliest
        beta = trans.time_interval.latest
        
        if trans.suspendable then
            // 可挂起变迁:约束在Z2中
            // Suspendable transition: constraints in Z2
            if alpha > 0 then
                state.Z2.set_constraint(0, clock_idx, -alpha)
            else
                state.Z2.set_constraint(0, clock_idx, 0)
            end if
            
            if beta != INF then
                state.Z2.set_constraint(clock_idx, 0, beta)
            else
                state.Z2.set_constraint(clock_idx, 0, INF_TIME)
            end if
        else
            // 不可挂起变迁:约束在Z1中
            // Non-suspendable transition: constraints in Z1
            if alpha > 0 then
                state.Z1.set_constraint(0, clock_idx, -alpha)
            else
                state.Z1.set_constraint(0, clock_idx, 0)
            end if
            
            if beta != INF then
                state.Z1.set_constraint(clock_idx, 0, beta)
            else
                state.Z1.set_constraint(clock_idx, 0, INF_TIME)
            end if
        end if
    end for
    
    // 最小化DBM
    // Minimize DBM
    CANONICALIZE(state.Z1)
    CANONICALIZE(state.Z2)
end function
```

---

## 3. 状态类生成算法 / State Class Generation Algorithm

### 3.1 主算法(BFS) / Main Algorithm (BFS)

```pseudocode
function BUILD_STATE_CLASS_GRAPH(net: MatrixPTPN, max_states: int) -> Graph:
    // 构建状态类可达性图(BFS探索)
    // Build state class reachability graph (BFS exploration)
    
    graph = new Graph()
    queue = new Queue()
    visited = new Set()
    
    // 1. 创建初始状态类
    // Create initial state class
    init_state = CREATE_INITIAL_STATE_CLASS(net)
    canonical_init = CANONICALIZE_STATE(init_state)
    
    queue.enqueue(init_state)
    visited.add(canonical_init)
    graph.add_vertex(init_state)
    
    stats = {total_states: 1, total_transitions: 0}
    
    // 2. BFS探索
    // BFS exploration
    while not queue.is_empty() and stats.total_states < max_states:
        current_state = queue.dequeue()
        current_vertex = graph.get_vertex(current_state)
        
        // 2.1 时间推进:计算可达的时间封闭区间
        // Time advance: compute reachable time-closed intervals
        (z1_up, z2_up) = TIME_ADVANCE_STATE(current_state, net)
        
        if z1_up.is_empty() then
            stats.pruned_states++
            continue  // Z1为空,无法继续
        end if
        
        // 2.2 找出所有使能的变迁
        // Find all enabled transitions
        enabled = {}
        for t = 0 to net.num_transitions()-1:
            if IS_ENABLED(current_state, t, net) then
                enabled.add(t)
            end if
        end for
        
        // 2.3 对每个使能变迁,检查是否可触发
        // For each enabled transition, check if it can fire
        for each t in enabled:
            // 检查是否被挂起
            // Check if suspended
            if IS_SUSPENDED(current_state, t, enabled, net) then
                continue  // 被挂起,不能触发
            end if
            
            // 确定使用哪个DBM(Z1或Z2)
            // Determine which DBM to use (Z1 or Z2)
            trans = net.get_transition(t)
            if trans.suspendable then
                z_target = z2_up
            else
                z_target = z1_up
            end if
            
            // 计算触发时间窗口的交集
            // Compute intersection of firing time windows
            alpha = trans.time_interval.earliest
            beta = trans.time_interval.latest
            z_firing = RESTRICT_FOR_FIRING(z_target, t, alpha, beta)
            
            if z_firing.is_empty() then
                stats.pruned_states++
                continue  // 无交集,无法触发
            end if
            
            // 选择触发时间(通常取最小合法时间)
            // Select firing time (usually minimum legal time)
            firing_time = COMPUTE_FIRING_TIME(z_firing, t, alpha, beta)
            
            // 2.4 触发变迁并生成后继状态
            // Fire transition and generate successor state
            succ_state = FIRE_TRANSITION(current_state, t, firing_time, net)
            
            if succ_state.marking.is_invalid() then
                stats.pruned_states++
                continue  // 无效状态
            end if
            
            // 2.5 规范化后继状态
            // Canonicalize successor state
            canonical_succ = CANONICALIZE_STATE(succ_state)
            
            // 2.6 检查是否已访问
            // Check if already visited
            if not visited.contains(canonical_succ) then
                // 新状态,添加到图和队列
                // New state, add to graph and queue
                succ_vertex = graph.add_vertex(succ_state)
                edge = new TransitionEdge(t, firing_time)
                graph.add_edge(current_vertex, succ_vertex, edge)
                
                visited.add(canonical_succ)
                queue.enqueue(succ_state)
                stats.total_states++
                stats.total_transitions++
            else
                // 已存在的状态,只添加边
                // Existing state, only add edge
                succ_vertex = graph.find_vertex(canonical_succ)
                edge = new TransitionEdge(t, firing_time)
                graph.add_edge(current_vertex, succ_vertex, edge)
                stats.total_transitions++
            end if
        end for
    end while
    
    return graph
end function
```

### 3.2 创建初始状态类 / Create Initial State Class

```pseudocode
function CREATE_INITIAL_STATE_CLASS(net: MatrixPTPN) -> StateClass:
    // 创建初始状态类
    // Create initial state class
    
    state = new StateClass()
    state.marking = net.get_initial_marking()
    state.state_id = 0
    state.cumulative_time = 0.0
    
    num_transitions = net.num_transitions()
    
    // 初始化DBM大小(+1为参考时钟)
    // Initialize DBM size (+1 for reference clock)
    state.Z1.resize(num_transitions + 1)
    state.Z2.resize(num_transitions + 1)
    
    // 更新DBM约束
    // Update DBM constraints
    UPDATE_DBM_CONSTRAINTS(state, net)
    
    // 计算初始使能和挂起状态
    // Compute initial enabled and suspended states
    RECOMPUTE_SUSPENSION(state, net)
    
    return state
end function
```

### 3.3 时间推进状态 / Time Advance State

```pseudocode
function TIME_ADVANCE_STATE(state: StateClass, net: MatrixPTPN) -> (DBM, DBM):
    // 计算时间推进后的DBM(Z1_up, Z2_up)
    // Compute DBM after time advance (Z1_up, Z2_up)
    
    z1_up = state.Z1.copy()
    z2_up = state.Z2.copy()
    
    // 获取不变约束(如果有)
    // Get invariant constraints (if any)
    invariants = GET_INVARIANTS(state.marking, net)
    
    num_clocks = z1_up.size()
    num_transitions = net.num_transitions()
    
    // 对于Z1:放宽不可挂起变迁的上界约束
    // For Z1: relax upper bound constraints for non-suspendable transitions
    for i = 1 to num_clocks-1:
        if i > num_transitions then
            continue
        end if
        
        trans_idx = i - 1
        trans = net.get_transition(trans_idx)
        
        // 检查变迁是否使能
        // Check if transition is enabled
        if not IS_ENABLED(state, trans_idx, net) then
            continue
        end if
        
        // 检查是否是精确时间约束(earliest == latest)
        // Check if exact time constraint (earliest == latest)
        is_exact = (trans.time_interval.earliest == trans.time_interval.latest and
                   trans.time_interval.latest != INF)
        
        // 如果是最精确时间约束且不可挂起,不能放宽上界
        // If exact time constraint and non-suspendable, cannot relax upper bound
        if is_exact and not trans.suspendable then
            continue  // 保持精确约束
        end if
        
        // 放宽上界约束,允许时间推进
        // Relax upper bound constraint, allow time advance
        current_upper = z1_up.get_constraint(i, 0)
        if current_upper != INF_TIME then
            z1_up.set_constraint(i, 0, INF_TIME)
        end if
    end for
    
    // 对于Z2:可挂起变迁在时间推进时不受限制(保持不变)
    // For Z2: suspendable transitions are not restricted during time advance
    
    // 计算Z1与不变量的交集
    // Compute intersection of Z1 with invariants
    if invariants.size() > 0 and z1_up.size() == invariants.size() then
        z1_up = z1_up.intersection(invariants)
    end if
    
    // 最小化DBM
    // Minimize DBM
    CANONICALIZE(z1_up)
    CANONICALIZE(z2_up)
    
    return (z1_up, z2_up)
end function
```

### 3.4 计算触发时间 / Compute Firing Time

```pseudocode
function COMPUTE_FIRING_TIME(z: DBM, transition_id: int, 
                             alpha: int, beta: int) -> double:
    // 计算变迁的触发时间(通常取最小合法时间)
    // Compute firing time (usually minimum legal time)
    
    clock_idx = transition_id + 1
    
    if clock_idx >= z.size() then
        return double(alpha)  // 默认返回最早时间
    end if
    
    // 从DBM中获取下界
    // Get lower bound from DBM
    dbm_lower = -z.get_constraint(0, clock_idx)  // x_clock - x_0 >= dbm_lower
    
    // 取满足下界的最大时间(alpha和dbm_lower的最大值)
    // Take maximum of alpha and dbm_lower
    firing_time_int = max(alpha, dbm_lower)
    
    // 确保不超过上界
    // Ensure not exceeding upper bound
    if beta != INF and firing_time_int > beta then
        firing_time_int = beta
    end if
    
    return double(firing_time_int)
end function
```

### 3.5 规范化状态类 / Canonicalize State Class

```pseudocode
function CANONICALIZE_STATE(state: StateClass) -> StateClass:
    // 规范化状态类(最小化DBM)
    // Canonicalize state class (minimize DBM)
    
    canonical = state.copy()
    
    // 最小化Z1和Z2
    // Minimize Z1 and Z2
    CANONICALIZE(canonical.Z1)
    CANONICALIZE(canonical.Z2)
    
    return canonical
end function
```

### 3.6 DFS变体(可选) / DFS Variant (Optional)

```pseudocode
function BUILD_STATE_CLASS_GRAPH_DFS(net: MatrixPTPN, max_states: int) -> Graph:
    // DFS版本的状态类生成算法
    // DFS variant of state class generation algorithm
    
    graph = new Graph()
    visited = new Set()
    
    init_state = CREATE_INITIAL_STATE_CLASS(net)
    canonical_init = CANONICALIZE_STATE(init_state)
    
    graph.add_vertex(init_state)
    visited.add(canonical_init)
    
    stats = {total_states: 1, total_transitions: 0}
    
    // DFS递归探索
    // DFS recursive exploration
    DFS_EXPLORE(init_state, graph, visited, net, stats, max_states)
    
    return graph
end function

function DFS_EXPLORE(state: StateClass, graph: Graph, 
                     visited: Set, net: MatrixPTPN, 
                     stats: Stats, max_states: int):
    // DFS递归探索函数
    // DFS recursive exploration function
    
    if stats.total_states >= max_states then
        return
    end if
    
    current_vertex = graph.get_vertex(state)
    
    // 时间推进
    // Time advance
    (z1_up, z2_up) = TIME_ADVANCE_STATE(state, net)
    
    if z1_up.is_empty() then
        return
    end if
    
    // 找出所有使能变迁
    // Find all enabled transitions
    enabled = {}
    for t = 0 to net.num_transitions()-1:
        if IS_ENABLED(state, t, net) then
            enabled.add(t)
        end if
    end for
    
    // 探索每个使能变迁
    // Explore each enabled transition
    for each t in enabled:
        if IS_SUSPENDED(state, t, enabled, net) then
            continue
        end if
        
        trans = net.get_transition(t)
        z_target = trans.suspendable ? z2_up : z1_up
        
        alpha = trans.time_interval.earliest
        beta = trans.time_interval.latest
        z_firing = RESTRICT_FOR_FIRING(z_target, t, alpha, beta)
        
        if z_firing.is_empty() then
            continue
        end if
        
        firing_time = COMPUTE_FIRING_TIME(z_firing, t, alpha, beta)
        succ_state = FIRE_TRANSITION(state, t, firing_time, net)
        
        if succ_state.marking.is_invalid() then
            continue
        end if
        
        canonical_succ = CANONICALIZE_STATE(succ_state)
        
        if not visited.contains(canonical_succ) then
            succ_vertex = graph.add_vertex(succ_state)
            edge = new TransitionEdge(t, firing_time)
            graph.add_edge(current_vertex, succ_vertex, edge)
            
            visited.add(canonical_succ)
            stats.total_states++
            stats.total_transitions++
            
            // 递归探索
            // Recursive exploration
            DFS_EXPLORE(succ_state, graph, visited, net, stats, max_states)
        else
            succ_vertex = graph.find_vertex(canonical_succ)
            edge = new TransitionEdge(t, firing_time)
            graph.add_edge(current_vertex, succ_vertex, edge)
            stats.total_transitions++
        end if
    end for
end function
```

---

## 4. 辅助函数 / Helper Functions

### 4.1 获取不变约束 / Get Invariants

```pseudocode
function GET_INVARIANTS(marking: int[], net: MatrixPTPN) -> DBM:
    // 获取当前标识的不变约束(如果有)
    // Get invariant constraints for current marking (if any)
    
    num_transitions = net.num_transitions()
    invariants = new DBM(num_transitions + 1)
    
    // 这里可以添加库所不变约束的逻辑
    // Place invariant constraints can be added here
    
    return invariants
end function
```

### 4.2 检查时间交集 / Check Time Intersection

```pseudocode
function CHECK_TIME_INTERSECTION(z: DBM, transition_id: int, 
                                  alpha: int, beta: int) -> bool:
    // 检查DBM时间约束与[α, β]是否有交集
    // Check if DBM time constraints intersect with [α, β]
    
    clock_idx = transition_id + 1
    
    if clock_idx >= z.size() then
        return false
    end if
    
    // 从DBM中获取约束
    // Get constraints from DBM
    dbm_upper = z.get_constraint(clock_idx, 0)  // x_clock - x_0 <= dbm_upper
    dbm_lower = -z.get_constraint(0, clock_idx)  // x_clock - x_0 >= dbm_lower
    
    // 检查交集:[α, β] 与 [dbm_lower, dbm_upper] 是否有交集
    // Check intersection: [α, β] ∩ [dbm_lower, dbm_upper]
    intersect_lower = max(alpha, dbm_lower)
    
    if beta == INF then
        intersect_upper = dbm_upper
    else
        intersect_upper = min(beta, dbm_upper == INF_TIME ? beta : dbm_upper)
    end if
    
    return intersect_lower <= intersect_upper
end function
```

---

## 5. 算法复杂度 / Algorithm Complexity

- **时间复杂度**: O(|S| × |T| × |C|³),其中:
  - |S|: 状态类数量
  - |T|: 变迁数量
  - |C|: 时钟数量(通常为|T|+1)
  - DBM最小化(Floyd-Warshall)为O(|C|³)

- **空间复杂度**: O(|S| × |C|²),用于存储状态类和DBM矩阵

---

## 6. 关键特性 / Key Features

1. **双DBM结构**: Z1用于不可挂起变迁,Z2用于可挂起变迁
   - **Dual DBM structure**: Z1 for non-suspendable transitions, Z2 for suspendable transitions

2. **优先级调度**: 高优先级不可挂起变迁可以挂起低优先级可挂起变迁
   - **Priority scheduling**: High-priority non-suspendable transitions can suspend low-priority suspendable transitions

3. **时间推进**: 仅非冻结时钟参与时间推进
   - **Time elapse**: Only non-frozen clocks participate in time elapse

4. **状态规范化**: 使用Floyd-Warshall算法最小化DBM以保证状态唯一性
   - **State canonicalization**: Minimize DBM using Floyd-Warshall to ensure state uniqueness

