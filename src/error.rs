//! 错误类型定义
//!
//! 使用 thiserror 提供结构化、可追溯的错误报告

use thiserror::Error;

/// 项目级错误类型
#[derive(Error, Debug)]
pub enum PtpnError {
    #[error("TDG 解析错误: {0}")]
    Tdg(#[from] TdgParseError),

    #[error("PTPN 错误: {0}")]
    Ptpn(#[from] PtpnErrorKind),

    #[error("DBM 错误: {0}")]
    Dbm(#[from] DbmError),

    #[error("可达图错误: {0}")]
    Reachability(#[from] ReachabilityError),

    #[error("IO 错误: {0}")]
    Io(#[from] std::io::Error),
}

/// TDG (Task Dependency Graph) 解析错误
#[derive(Error, Debug)]
pub enum TdgParseError {
    #[error("DOT 文件解析失败: {path} - {message}")]
    DotParse { path: String, message: String },

    #[error("Label 格式错误: 节点 '{node}' 的 label '{label}' 无效 - {reason}")]
    LabelFormat {
        node: String,
        label: String,
        reason: String,
    },

    #[error("Label 内容为空")]
    EmptyLabel,

    #[error("任务名不能为空")]
    EmptyTaskName,

    #[error("非周期任务参数不足: {name} - 需要至少 4 个参数")]
    InsufficientAperiodicParams { name: String },

    #[error("时间格式错误: '{times}' - {reason}")]
    TimeFormat { times: String, reason: String },

    #[error("无效的 label 格式: {0}")]
    InvalidLabelFormat(String),
}

/// PTPN 结构错误
#[derive(Error, Debug)]
pub enum PtpnErrorKind {
    #[error("无效的库所索引: {idx} (共 {total} 个库所)")]
    InvalidPlaceIndex { idx: usize, total: usize },

    #[error("无效的变迁索引: {idx} (共 {total} 个变迁)")]
    InvalidTransitionIndex { idx: usize, total: usize },

    #[error("标识向量长度 {actual} 与库所数量 {expected} 不匹配")]
    MarkingSizeMismatch { actual: usize, expected: usize },

    #[error("变迁未使能: 变迁 {trans_idx} 无法在当前标识下触发")]
    TransitionNotEnabled { trans_idx: usize },

    #[error("库所容量越界: 库所 {place_idx} 容量 {capacity} 被超出")]
    PlaceCapacityExceeded {
        place_idx: usize,
        capacity: i32,
    },

    #[error("Token 数量不能为负")]
    NegativeTokens,
}

/// DBM (差分界矩阵) 错误
#[derive(Error, Debug)]
pub enum DbmError {
    #[error("DBM 索引越界: ({i}, {j}) 超出大小 {size}")]
    IndexOutOfRange { i: usize, j: usize, size: usize },

    #[error("DBM 存在负环，约束不一致")]
    NegativeCycle,

    #[error("DBM 为空 (无解)")]
    Empty,

    #[error("交集操作要求 DBM 大小一致: {a} vs {b}")]
    SizeMismatchForIntersection { a: usize, b: usize },
}

/// 可达图构建错误
#[derive(Error, Debug)]
pub enum ReachabilityError {
    #[error("状态探索异常: {0}")]
    Exploration(String),

    #[error("初始状态创建失败: {0}")]
    InitialStateCreation(String),

    #[error("达到最大状态数限制: {0}")]
    MaxStatesReached(usize),
}
