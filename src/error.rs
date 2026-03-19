//! 错误类型定义
//!
//! 使用 thiserror 提供结构化、可追溯的错误报告

use thiserror::Error;

/// 项目级错误类型
#[derive(Error, Debug)]
pub enum PtpnError {
    #[error("TDG 解析错误: {0}")]
    Tdg(#[from] TdgParseError),

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

