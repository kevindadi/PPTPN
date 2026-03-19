//! DBM (Difference Bound Matrix)
//!
//! 差分界矩阵表示时间约束，DBM[i][j] 表示 x_i - x_j <= bound
//! x_0 为参考时钟

use crate::error::DbmError;
use crate::ptpn::INF;
use std::collections::HashSet;
use std::fmt;

/// 无穷大时间常量 (用于 DBM)
pub const INF_TIME: i32 = i32::MAX;

/// DBM 差分界矩阵
#[derive(Clone)]
pub struct Dbm {
    matrix: Vec<Vec<i32>>,
    clock_count: usize,
    frozen_clocks: HashSet<usize>,
}

impl Dbm {
    pub fn new(size: usize) -> Self {
        let mut matrix = vec![vec![INF_TIME; size]; size];
        for i in 0..size {
            matrix[i][i] = 0;
        }
        if size > 1 {
            for i in 1..size {
                matrix[i][0] = INF_TIME;
                matrix[0][i] = 0;
            }
        }
        Self {
            matrix,
            clock_count: size,
            frozen_clocks: HashSet::new(),
        }
    }

    pub fn size(&self) -> usize {
        self.clock_count
    }

    fn check_index(&self, i: usize, j: usize) -> Result<(), DbmError> {
        if i >= self.clock_count || j >= self.clock_count {
            return Err(DbmError::IndexOutOfRange {
                i,
                j,
                size: self.clock_count,
            });
        }
        Ok(())
    }

    pub fn set_constraint(&mut self, i: usize, j: usize, bound: i32) {
        if i < self.clock_count && j < self.clock_count {
            self.matrix[i][j] = bound;
        }
    }

    pub fn get_constraint(&self, i: usize, j: usize) -> i32 {
        if i >= self.clock_count || j >= self.clock_count {
            return INF_TIME;
        }
        self.matrix[i][j]
    }

    pub fn is_consistent(&self) -> bool {
        if self.clock_count == 0 {
            return true;
        }
        let mut temp = self.clone();
        temp.minimize();
        for i in 0..temp.clock_count {
            if temp.matrix[i][i] < 0 {
                return false;
            }
        }
        true
    }

    pub fn minimize(&mut self) {
        if self.clock_count == 0 {
            return;
        }
        for k in 0..self.clock_count {
            for i in 0..self.clock_count {
                if self.matrix[i][k] == INF_TIME {
                    continue;
                }
                for j in 0..self.clock_count {
                    if self.matrix[k][j] == INF_TIME {
                        continue;
                    }
                    let new_bound = self.matrix[i][k].saturating_add(self.matrix[k][j]);
                    if self.matrix[i][j] == INF_TIME || new_bound < self.matrix[i][j] {
                        self.matrix[i][j] = new_bound;
                    }
                }
            }
        }
    }

    pub fn add_clock(&mut self) -> usize {
        let new_idx = self.clock_count;
        self.resize(self.clock_count + 1);
        new_idx
    }

    pub fn resize(&mut self, new_size: usize) {
        if new_size == self.clock_count {
            return;
        }
        let old_size = self.clock_count;
        self.clock_count = new_size;
        self.matrix.resize(new_size, vec![INF_TIME; new_size]);
        for row in &mut self.matrix {
            row.resize(new_size, INF_TIME);
        }
        for i in old_size..new_size {
            self.initialize_clock(i);
        }
    }

    fn initialize_clock(&mut self, clock_idx: usize) {
        if clock_idx >= self.clock_count {
            return;
        }
        self.matrix[clock_idx][clock_idx] = 0;
        if clock_idx == 0 {
            for i in 1..self.clock_count {
                self.matrix[0][i] = 0;
                self.matrix[i][0] = INF_TIME;
            }
        } else {
            self.matrix[clock_idx][0] = INF_TIME;
            self.matrix[0][clock_idx] = 0;
            for i in 1..self.clock_count {
                if i != clock_idx {
                    self.matrix[clock_idx][i] = INF_TIME;
                    self.matrix[i][clock_idx] = INF_TIME;
                }
            }
        }
    }

    pub fn elapse_time(&mut self, delta: i32) {
        if delta <= 0 {
            return;
        }
        for i in 1..self.clock_count {
            if !self.frozen_clocks.contains(&i) {
                if self.matrix[i][0] != INF_TIME {
                    self.matrix[i][0] = INF_TIME;
                }
            }
        }
        self.minimize();
    }

    pub fn reset_clock(&mut self, clock_idx: usize) {
        if clock_idx >= self.clock_count || clock_idx == 0 {
            return;
        }
        self.matrix[clock_idx][0] = 0;
        self.matrix[0][clock_idx] = 0;
        self.minimize();
    }

    pub fn remove_clock(&mut self, clock_idx: usize) {
        if clock_idx >= self.clock_count || clock_idx == 0 {
            return;
        }
        self.frozen_clocks.remove(&clock_idx);
        let mut new_matrix = vec![vec![0; self.clock_count - 1]; self.clock_count - 1];
        for i in 0..self.clock_count {
            if i == clock_idx {
                continue;
            }
            let new_i = if i < clock_idx { i } else { i - 1 };
            for j in 0..self.clock_count {
                if j == clock_idx {
                    continue;
                }
                let new_j = if j < clock_idx { j } else { j - 1 };
                new_matrix[new_i][new_j] = self.matrix[i][j];
            }
        }
        self.matrix = new_matrix;
        self.clock_count -= 1;
        let mut new_frozen = HashSet::new();
        for &idx in &self.frozen_clocks {
            if idx < clock_idx {
                new_frozen.insert(idx);
            } else if idx > clock_idx {
                new_frozen.insert(idx - 1);
            }
        }
        self.frozen_clocks = new_frozen;
    }

    pub fn restrict_for_firing(&self, transition_id: usize, alpha: i32, beta: i32) -> Dbm {
        let clock_idx = transition_id + 1;
        if clock_idx >= self.clock_count {
            return self.clone();
        }
        let mut result = self.clone();
        let raw = result.get_constraint(0, clock_idx);
        let current_lower = if raw == INF_TIME {
            i32::MIN
        } else {
            raw.checked_neg().unwrap_or(i32::MIN)
        };
        if alpha > current_lower {
            result.set_constraint(0, clock_idx, alpha.checked_neg().unwrap_or(i32::MIN));
        }
        let current_upper = result.get_constraint(clock_idx, 0);
        if beta != INF_TIME && (current_upper == INF_TIME || beta < current_upper) {
            result.set_constraint(clock_idx, 0, beta);
        }
        result.minimize();
        if result.is_empty() {
            return Dbm::new(0);
        }
        result
    }

    pub fn freeze_clock(&mut self, clock_idx: usize) {
        if clock_idx < self.clock_count && clock_idx != 0 {
            self.frozen_clocks.insert(clock_idx);
        }
    }

    pub fn unfreeze_clock(&mut self, clock_idx: usize) {
        self.frozen_clocks.remove(&clock_idx);
    }

    pub fn is_frozen(&self, clock_idx: usize) -> bool {
        self.frozen_clocks.contains(&clock_idx)
    }

    pub fn copy_clock_constraints(&self, clock_idx: usize, target: &mut Dbm) {
        if clock_idx >= self.clock_count {
            return;
        }
        if clock_idx >= target.size() {
            target.resize(clock_idx + 1);
        }
        for i in 0..self.clock_count {
            if i < target.size() {
                target.set_constraint(clock_idx, i, self.matrix[clock_idx][i]);
                target.set_constraint(i, clock_idx, self.matrix[i][clock_idx]);
            }
        }
        if self.frozen_clocks.contains(&clock_idx) {
            target.freeze_clock(clock_idx);
        }
    }

    pub fn intersection(&self, other: &Dbm) -> Result<Dbm, DbmError> {
        if self.clock_count != other.clock_count {
            return Err(DbmError::SizeMismatchForIntersection {
                a: self.clock_count,
                b: other.clock_count,
            });
        }
        let mut result = Dbm::new(self.clock_count);
        for i in 0..self.clock_count {
            for j in 0..self.clock_count {
                let b1 = self.get_constraint(i, j);
                let b2 = other.get_constraint(i, j);
                let bound = if b1 == INF_TIME {
                    b2
                } else if b2 == INF_TIME {
                    b1
                } else {
                    b1.min(b2)
                };
                result.set_constraint(i, j, bound);
            }
        }
        result.minimize();
        Ok(result)
    }

    pub fn is_empty(&self) -> bool {
        !self.is_consistent()
    }

    pub fn prune(&mut self) {
        self.minimize();
    }
}

impl Default for Dbm {
    fn default() -> Self {
        Self::new(0)
    }
}

impl PartialEq for Dbm {
    fn eq(&self, other: &Self) -> bool {
        if self.clock_count != other.clock_count || self.frozen_clocks != other.frozen_clocks {
            return false;
        }
        for i in 0..self.clock_count {
            for j in 0..self.clock_count {
                if self.matrix[i][j] != other.matrix[i][j] {
                    return false;
                }
            }
        }
        true
    }
}

impl Eq for Dbm {}

impl PartialOrd for Dbm {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for Dbm {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        if self.clock_count != other.clock_count {
            return self.clock_count.cmp(&other.clock_count);
        }
        for i in 0..self.clock_count {
            for j in 0..self.clock_count {
                let a = self.matrix[i][j];
                let b = other.matrix[i][j];
                if a != b {
                    if a == INF_TIME {
                        return std::cmp::Ordering::Greater;
                    }
                    if b == INF_TIME {
                        return std::cmp::Ordering::Less;
                    }
                    return a.cmp(&b);
                }
            }
        }
        std::cmp::Ordering::Equal
    }
}

impl fmt::Debug for Dbm {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Dbm(size={})", self.clock_count)
    }
}
