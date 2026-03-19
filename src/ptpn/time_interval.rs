//! 时间区间
//!
//! 表示变迁的触发时间窗口 [α, β]

use std::fmt;

/// 无穷大时间常量
pub const INF: i32 = i32::MAX;

/// 时间区间 [earliest, latest]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TimeInterval {
    /// α(t) 最早可触发时间
    pub earliest: i32,
    /// β(t) 最晚可触发时间 (可为 INF)
    pub latest: i32,
}

impl TimeInterval {
    pub fn new(earliest: i32, latest: i32) -> Self {
        Self {
            earliest: earliest.max(0),
            latest: if latest < earliest && latest != INF {
                earliest
            } else {
                latest
            },
        }
    }

    pub fn zero() -> Self {
        Self {
            earliest: 0,
            latest: 0,
        }
    }

    pub fn is_valid(&self) -> bool {
        self.earliest >= 0 && (self.latest == INF || self.latest >= self.earliest)
    }

    pub fn contains(&self, time: i32) -> bool {
        time >= self.earliest && (self.latest == INF || time <= self.latest)
    }
}

impl Default for TimeInterval {
    fn default() -> Self {
        Self {
            earliest: 0,
            latest: INF,
        }
    }
}

impl fmt::Display for TimeInterval {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "[{}, ", self.earliest)?;
        if self.latest == INF {
            write!(f, "∞")?;
        } else {
            write!(f, "{}", self.latest)?;
        }
        write!(f, "]")
    }
}
