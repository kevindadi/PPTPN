//! 优先级时间 Petri 网 (P-PTPN) 分析
//!
//! 基于 [R-PTPN](https://github.com/kevindadi/R-PTPN) 库，支持 TDG 解析与状态类图构建

pub mod converter;
pub mod error;
pub mod tdg;

pub use converter::tdg_to_ptpn;
pub use error::{PtpnError, TdgParseError};
pub use tdg::{parse_dot_file, Tdg};
