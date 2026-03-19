//! DOT 文件解析
//!
//! 解析 TDG 格式的 DOT 文件，提取节点和边的信息

use crate::error::{PtpnError, TdgParseError};
use crate::tdg::task_types::{
    AperiodicTask, DistTask, EmptyTask, NodeType, PeriodicTask, SyncTask, TaskType,
};
use regex::Regex;
use std::collections::HashMap;
use std::fs;
use std::path::Path;
use tracing::info;

/// 解析时间区间字符串 [a,b] 为 (a, b)
pub fn parse_time_vec(times: &str) -> Result<Vec<i32>, TdgParseError> {
    if times.trim().is_empty() {
        return Err(TdgParseError::TimeFormat {
            times: times.to_string(),
            reason: "时间字符串不能为空".to_string(),
        });
    }

    let re = Regex::new(r"\[(\d+),(\d+)\]").map_err(|e| TdgParseError::TimeFormat {
        times: times.to_string(),
        reason: format!("正则表达式错误: {}", e),
    })?;

    let mut values = Vec::new();
    for cap in re.captures_iter(times) {
        let a: i32 = cap[1].parse().map_err(|_| TdgParseError::TimeFormat {
            times: times.to_string(),
            reason: format!("无法解析数字: {}", &cap[1]),
        })?;
        let b: i32 = cap[2].parse().map_err(|_| TdgParseError::TimeFormat {
            times: times.to_string(),
            reason: format!("无法解析数字: {}", &cap[2]),
        })?;
        values.push(a);
        values.push(b);
    }

    if values.is_empty() {
        return Err(TdgParseError::TimeFormat {
            times: times.to_string(),
            reason: "未找到有效的时间区间 [a,b]".to_string(),
        });
    }

    Ok(values)
}

/// 检查是否为时间区间格式
fn is_time_range(s: &str) -> bool {
    s.trim().starts_with('[') && s.contains(',') && s.trim().ends_with(']')
}

/// 解析顶点 label
pub fn parse_vertex_label(node_id: &str, label: &str) -> Result<NodeType, TdgParseError> {
    if label.is_empty() {
        return Err(TdgParseError::EmptyLabel);
    }

    let re = Regex::new(r"\{([^}]*)\}")
        .map_err(|_| TdgParseError::InvalidLabelFormat(label.to_string()))?;

    let content = re
        .captures(label)
        .and_then(|c| c.get(1))
        .map(|m| m.as_str())
        .ok_or_else(|| TdgParseError::InvalidLabelFormat(label.to_string()))?;

    if content.is_empty() {
        return Err(TdgParseError::LabelFormat {
            node: node_id.to_string(),
            label: label.to_string(),
            reason: "label 内容为空".to_string(),
        });
    }

    let parts: Vec<&str> = content.split(';').map(|s| s.trim()).collect();

    if parts.is_empty() {
        return Err(TdgParseError::LabelFormat {
            node: node_id.to_string(),
            label: label.to_string(),
            reason: "未找到任务名".to_string(),
        });
    }

    let name = parts[0].to_string();
    if name.is_empty() {
        return Err(TdgParseError::EmptyTaskName);
    }

    // 特殊节点处理
    if parts.len() <= 2 {
        if name.starts_with("Wait") {
            return Ok(NodeType::Sync(SyncTask { name, time: (0, 0) }));
        } else if name.starts_with("Dist") {
            return Ok(NodeType::Dist(DistTask { name, time: (0, 0) }));
        } else if name.starts_with("Empty") {
            return Ok(NodeType::Empty(EmptyTask { name }));
        }
    }

    // 尝试解析周期任务 (parts[1] 为 [a,b] 格式)
    let has_period = parts.len() > 1 && is_time_range(parts[1]);

    if has_period {
        // 周期任务: {name;[period];priority;core;[exec_time]}
        let period_times = parse_time_vec(parts[1])?;
        let task_period_time = (period_times[0], period_times[1]);

        let time_values = parse_time_vec(parts[4])?;
        let mut task_times = Vec::new();
        for i in (0..time_values.len()).step_by(2) {
            if i + 1 < time_values.len() {
                task_times.push((time_values[i], time_values[i + 1]));
            }
        }

        let mut task_locks = Vec::new();
        let mut is_lock = false;
        if parts.len() >= 6 {
            is_lock = true;
            for lock_token in parts[5].split(',') {
                task_locks.push(lock_token.trim().to_string());
            }
        }

        let task_type = if name.starts_with("Interrupt") {
            TaskType::Period
        } else if name.starts_with("Sporadic") {
            TaskType::Aperiod
        } else {
            TaskType::Period
        };

        let core: i32 = parts[3].parse().map_err(|_| TdgParseError::LabelFormat {
            node: node_id.to_string(),
            label: label.to_string(),
            reason: format!("无法解析 core: {}", parts[3]),
        })?;
        let priority: i32 = parts[2].parse().map_err(|_| TdgParseError::LabelFormat {
            node: node_id.to_string(),
            label: label.to_string(),
            reason: format!("无法解析 priority: {}", parts[2]),
        })?;

        return Ok(NodeType::Periodic(PeriodicTask {
            name,
            core,
            priority,
            time: task_times,
            is_lock,
            lock: task_locks,
            task_type,
            period_time: task_period_time,
        }));
    }

    // 非周期任务: {name;priority;core;[exec_time]}
    if parts.len() < 4 {
        return Err(TdgParseError::InsufficientAperiodicParams { name });
    }

    let time_values = parse_time_vec(parts[3])?;
    let mut task_times = Vec::new();
    for i in (0..time_values.len()).step_by(2) {
        if i + 1 < time_values.len() {
            task_times.push((time_values[i], time_values[i + 1]));
        }
    }

    let mut task_locks = Vec::new();
    let mut is_lock = false;
    if parts.len() >= 5 {
        is_lock = true;
        for lock_token in parts[4].split(',') {
            task_locks.push(lock_token.trim().to_string());
        }
    }

    let core: i32 = parts[2].parse().map_err(|_| TdgParseError::LabelFormat {
        node: node_id.to_string(),
        label: label.to_string(),
        reason: format!("无法解析 core: {}", parts[2]),
    })?;
    let priority: i32 = parts[1].parse().map_err(|_| TdgParseError::LabelFormat {
        node: node_id.to_string(),
        label: label.to_string(),
        reason: format!("无法解析 priority: {}", parts[1]),
    })?;

    Ok(NodeType::Aperiodic(AperiodicTask {
        name,
        core,
        priority,
        time: task_times,
        is_lock,
        lock: task_locks,
        task_type: TaskType::Normal,
    }))
}

/// 边信息
#[derive(Debug, Clone)]
pub struct EdgeInfo {
    pub label: String,
    pub style: String,
}

/// TDG 图结构
#[derive(Debug)]
pub struct Tdg {
    pub num_cpus: i32,
    pub cores_per_cpu: i32,
    pub nodes_type: HashMap<String, NodeType>,
    pub vertexes_type: HashMap<String, TdgVertexType>,
    pub tasks_type: HashMap<String, TaskType>,
    pub all_task: Vec<NodeType>,
    pub tasks_priority: HashMap<String, i32>,
    pub period_task: Vec<(String, String, i32)>,
    pub lock_set: std::collections::HashSet<String>,
    pub task_locks_map: HashMap<String, Vec<String>>,
    pub tasks_config: HashMap<String, TaskConfig>,
    pub edges: Vec<(String, String, EdgeInfo)>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TdgVertexType {
    Task,
    Sync,
    Dist,
    Empty,
}

#[derive(Debug, Clone)]
pub struct TaskConfig {
    pub core: i32,
    pub priority: i32,
    pub times: Vec<(i32, i32)>,
    pub locks: Vec<String>,
}

/// 解析 DOT 文件
///
/// 使用简单的文本解析，不依赖 graphviz 库
pub fn parse_dot_file(
    path: impl AsRef<Path>,
    num_cpus: i32,
    cores_per_cpu: i32,
) -> Result<Tdg, PtpnError> {
    let path = path.as_ref();
    let content = fs::read_to_string(path).map_err(|e| TdgParseError::DotParse {
        path: path.display().to_string(),
        message: e.to_string(),
    })?;

    parse_dot_content(&content, num_cpus, cores_per_cpu)
}

/// 解析 DOT 内容
pub fn parse_dot_content(
    content: &str,
    num_cpus: i32,
    cores_per_cpu: i32,
) -> Result<Tdg, PtpnError> {
    let mut tdg = Tdg {
        num_cpus,
        cores_per_cpu,
        nodes_type: HashMap::new(),
        vertexes_type: HashMap::new(),
        tasks_type: HashMap::new(),
        all_task: Vec::new(),
        tasks_priority: HashMap::new(),
        period_task: Vec::new(),
        lock_set: std::collections::HashSet::new(),
        task_locks_map: HashMap::new(),
        tasks_config: HashMap::new(),
        edges: Vec::new(),
    };

    // 简单解析: 查找 node [label="..."] 和 edge (source -> target)
    let node_re = Regex::new(r#"(\w+)\s*\[[^]]*label\s*=\s*"([^"]*)"[^]]*\]"#).unwrap();
    let edge_re = Regex::new(r#"(\w+)\s*->\s*(\w+)\s*\[([^]]*)\]"#).unwrap();
    let edge_simple_re = Regex::new(r#"(\w+)\s*->\s*(\w+)\s*;"#).unwrap();

    for line in content.lines() {
        let line = line.trim();
        if line.starts_with("//") || line.is_empty() {
            continue;
        }

        // 解析节点
        if let Some(caps) = node_re.captures(line) {
            let node_id = caps[1].to_string();
            let label = caps[2].to_string();

            info!("[TDG] 解析节点: {} label={}", node_id, label);

            match parse_vertex_label(&node_id, &label) {
                Ok(node_type) => {
                    let name = node_type.name().to_string();
                    tdg.vertexes_type.insert(
                        name.clone(),
                        match &node_type {
                            NodeType::Periodic(_) | NodeType::Aperiodic(_) => TdgVertexType::Task,
                            NodeType::Sync(_) => TdgVertexType::Sync,
                            NodeType::Dist(_) => TdgVertexType::Dist,
                            NodeType::Empty(_) => TdgVertexType::Empty,
                        },
                    );

                    match &node_type {
                        NodeType::Periodic(t) => {
                            tdg.nodes_type.insert(name.clone(), node_type.clone());
                            tdg.all_task.push(node_type.clone());
                            tdg.tasks_priority.insert(name.clone(), t.priority);
                            tdg.tasks_type.insert(name.clone(), t.task_type);
                        }
                        NodeType::Aperiodic(t) => {
                            tdg.nodes_type.insert(name.clone(), node_type.clone());
                            tdg.all_task.push(node_type.clone());
                            tdg.tasks_priority.insert(name.clone(), t.priority);
                            tdg.tasks_type.insert(name.clone(), t.task_type);
                        }
                        _ => {
                            tdg.nodes_type.insert(name.clone(), node_type);
                        }
                    }
                }
                Err(e) => {
                    tracing::warn!("[TDG] 节点 {} 解析失败: {}", node_id, e);
                }
            }
        }

        // 解析边 (带属性)
        if let Some(caps) = edge_re.captures(line) {
            let source = caps[1].to_string();
            let target = caps[2].to_string();
            let attrs = caps[3].to_string();

            let mut label = String::new();
            let mut style = String::new();
            for attr in attrs.split(',') {
                let attr = attr.trim();
                if let Some((k, v)) = attr.split_once('=') {
                    let k = k.trim();
                    let v = v.trim().trim_matches('"');
                    if k == "xlabel" {
                        label = v.to_string();
                    } else if k == "style" {
                        style = v.to_string();
                    }
                }
            }
            tdg.edges.push((source, target, EdgeInfo { label, style }));
        }

        // 解析边 (无属性)
        if let Some(caps) = edge_simple_re.captures(line) {
            let source = caps[1].to_string();
            let target = caps[2].to_string();
            if !tdg
                .edges
                .iter()
                .any(|(s, t, _)| s == &source && t == &target)
            {
                tdg.edges.push((
                    source,
                    target,
                    EdgeInfo {
                        label: String::new(),
                        style: String::new(),
                    },
                ));
            }
        }
    }

    info!(
        "[TDG] 解析完成: {} 个节点, {} 条边",
        tdg.nodes_type.len(),
        tdg.edges.len()
    );

    Ok(tdg)
}

/// 分类优先级 (用于抢占关系)
pub fn classify_priority(tdg: &mut Tdg) -> HashMap<i32, Vec<String>> {
    let mut core_task: HashMap<i32, Vec<String>> = HashMap::new();

    for node_type in &tdg.all_task {
        let (name, core, priority, times) = match node_type {
            NodeType::Periodic(t) => (t.name.clone(), t.core, t.priority, t.time.clone()),
            NodeType::Aperiodic(t) => (t.name.clone(), t.core, t.priority, t.time.clone()),
            _ => continue,
        };

        core_task.entry(core).or_default().push(name.clone());

        tdg.tasks_config.insert(
            name,
            TaskConfig {
                core,
                priority,
                times,
                locks: Vec::new(),
            },
        );
    }

    core_task
}
