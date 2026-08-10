//! Difference Bound Matrix (port of `src/analysis/dbm.cpp`).
//!
//! Represents clock constraints as inequalities `x_i - x_j <= c` with a dense
//! matrix. `INF_TIME` marks "no bound".

use std::fmt;
use std::sync::atomic::{AtomicUsize, Ordering};

/// Sentinel for "no upper bound" (+infinity) inside DBM matrices.
pub const INF_TIME: i32 = i32::MAX;

static G_DBM_MINIMIZE_CALLS: AtomicUsize = AtomicUsize::new(0);

pub fn reset_dbm_instrumentation() {
    G_DBM_MINIMIZE_CALLS.store(0, Ordering::Relaxed);
}

pub fn dbm_minimize_calls() -> usize {
    G_DBM_MINIMIZE_CALLS.load(Ordering::Relaxed)
}

#[derive(Debug, Clone, Default)]
pub struct DbmInstrumentation {
    pub minimize_calls: usize,
}

pub fn get_dbm_instrumentation() -> DbmInstrumentation {
    DbmInstrumentation {
        minimize_calls: dbm_minimize_calls(),
    }
}

fn safe_add_bound(lhs: i32, rhs: i32) -> i32 {
    if lhs == INF_TIME || rhs == INF_TIME {
        return INF_TIME;
    }
    if (rhs > 0 && lhs > i32::MAX - rhs) || (rhs < 0 && lhs < i32::MIN - rhs) {
        return if rhs > 0 { INF_TIME } else { i32::MIN };
    }
    lhs + rhs
}

fn dbm_offset(clock_count: usize, i: usize, j: usize) -> usize {
    i * clock_count + j
}

#[derive(Debug, Clone)]
pub struct DBM {
    matrix: Vec<i32>,
    clock_count: usize,
    /// Sorted, unique frozen clock indices.
    frozen_clocks: Vec<usize>,
}

impl DBM {
    pub fn new(size: usize) -> Self {
        let mut matrix = vec![INF_TIME; size * size];
        if size > 0 {
            for i in 0..size {
                matrix[i * size + i] = 0;
            }
            if size > 1 {
                for i in 1..size {
                    matrix[i * size + 0] = INF_TIME;
                    matrix[0 * size + i] = 0;
                }
            }
        }
        DBM {
            matrix,
            clock_count: size,
            frozen_clocks: Vec::new(),
        }
    }

    pub fn size(&self) -> usize {
        self.clock_count
    }

    fn check_index(&self, i: usize, j: usize) {
        assert!(
            i < self.clock_count && j < self.clock_count,
            "DBM index out of range"
        );
    }

    pub fn set_constraint(&mut self, i: usize, j: usize, bound: i32) {
        self.check_index(i, j);
        let idx = dbm_offset(self.clock_count, i, j);
        self.matrix[idx] = bound;
    }

    pub fn get_constraint(&self, i: usize, j: usize) -> i32 {
        self.check_index(i, j);
        self.matrix[dbm_offset(self.clock_count, i, j)]
    }

    pub fn is_consistent(&self) -> bool {
        if self.clock_count == 0 {
            return true;
        }
        for i in 0..self.clock_count {
            if self.matrix[dbm_offset(self.clock_count, i, i)] < 0 {
                return false;
            }
        }

        let mut temp = self.clone();
        temp.minimize();

        for i in 0..self.clock_count {
            if temp.matrix[dbm_offset(temp.clock_count, i, i)] < 0 {
                return false;
            }
        }
        true
    }

    /// Floyd-Warshall all-pairs shortest path closure.
    pub fn minimize(&mut self) {
        G_DBM_MINIMIZE_CALLS.fetch_add(1, Ordering::Relaxed);
        if self.clock_count == 0 {
            return;
        }

        let n = self.clock_count;
        for k in 0..n {
            for i in 0..n {
                let ik = dbm_offset(self.clock_count, i, k);
                if self.matrix[ik] == INF_TIME {
                    continue;
                }
                for j in 0..n {
                    let kj = dbm_offset(self.clock_count, k, j);
                    if self.matrix[kj] == INF_TIME {
                        continue;
                    }
                    let new_bound = safe_add_bound(self.matrix[ik], self.matrix[kj]);
                    let ij = dbm_offset(self.clock_count, i, j);
                    if self.matrix[ij] == INF_TIME || new_bound < self.matrix[ij] {
                        self.matrix[ij] = new_bound;
                    }
                }
            }
        }
    }

    pub fn minimize_and_check(&mut self) -> bool {
        self.minimize();
        for i in 0..self.clock_count {
            if self.matrix[dbm_offset(self.clock_count, i, i)] < 0 {
                return false;
            }
        }
        true
    }

    /// Tightens constraint (i, j) on an already-canonical DBM with O(n^2)
    /// incremental closure. Returns false when the tightening empties the zone.
    pub fn tighten(&mut self, i: usize, j: usize, bound: i32) -> bool {
        self.check_index(i, j);
        let n = self.clock_count;

        let ij = dbm_offset(self.clock_count, i, j);
        if self.matrix[ij] != INF_TIME && self.matrix[ij] <= bound {
            return true;
        }
        self.matrix[ij] = bound;

        let ji = self.matrix[dbm_offset(self.clock_count, j, i)];
        if ji != INF_TIME && safe_add_bound(bound, ji) < 0 {
            let diag = dbm_offset(self.clock_count, i, i);
            self.matrix[diag] = safe_add_bound(bound, ji);
            return false;
        }

        // Re-close: any pair (a, b) can only improve via a -> i -> j -> b.
        for a in 0..n {
            let ai = self.matrix[dbm_offset(self.clock_count, a, i)];
            if ai == INF_TIME {
                continue;
            }
            let a_via = safe_add_bound(ai, bound);
            if a_via == INF_TIME {
                continue;
            }
            for b in 0..n {
                let jb = self.matrix[dbm_offset(self.clock_count, j, b)];
                if jb == INF_TIME {
                    continue;
                }
                let candidate = safe_add_bound(a_via, jb);
                let ab = dbm_offset(self.clock_count, a, b);
                if self.matrix[ab] == INF_TIME || candidate < self.matrix[ab] {
                    self.matrix[ab] = candidate;
                }
            }
        }

        for a in 0..n {
            if self.matrix[dbm_offset(self.clock_count, a, a)] < 0 {
                return false;
            }
        }
        true
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
        let old_matrix = self.matrix.clone();

        self.clock_count = new_size;
        self.matrix = vec![INF_TIME; new_size * new_size];

        for i in 0..new_size {
            self.matrix[i * new_size + i] = 0;
        }
        if new_size > 1 {
            for i in 1..new_size {
                self.matrix[i * new_size + 0] = INF_TIME;
                self.matrix[0 * new_size + i] = 0;
            }
        }

        let preserved = old_size.min(new_size);
        for i in 0..preserved {
            for j in 0..preserved {
                self.matrix[i * new_size + j] = old_matrix[i * old_size + j];
            }
        }

        for i in old_size..new_size {
            self.initialize_clock(i);
        }
    }

    fn initialize_clock(&mut self, clock_idx: usize) {
        if clock_idx >= self.clock_count {
            return;
        }
        let n = self.clock_count;
        self.matrix[clock_idx * n + clock_idx] = 0;

        if clock_idx == 0 {
            for i in 1..n {
                self.matrix[0 * n + i] = 0;
                self.matrix[i * n + 0] = INF_TIME;
            }
        } else {
            self.matrix[clock_idx * n + 0] = INF_TIME;
            self.matrix[0 * n + clock_idx] = 0;

            for i in 1..n {
                if i != clock_idx {
                    self.matrix[clock_idx * n + i] = INF_TIME;
                    self.matrix[i * n + clock_idx] = INF_TIME;
                }
            }
        }
    }

    pub fn elapse_time(&mut self, delta: i32) {
        if delta <= 0 || self.clock_count == 0 {
            return;
        }
        let mut changed = false;
        for i in 1..self.clock_count {
            if self.is_frozen(i) {
                continue;
            }
            let current_upper = self.matrix[dbm_offset(self.clock_count, i, 0)];
            if current_upper != INF_TIME {
                self.matrix[dbm_offset(self.clock_count, i, 0)] = safe_add_bound(current_upper, delta);
                changed = true;
            }
            let current_lower = self.matrix[dbm_offset(self.clock_count, 0, i)];
            if current_lower != INF_TIME {
                self.matrix[dbm_offset(self.clock_count, 0, i)] = safe_add_bound(current_lower, -delta);
                changed = true;
            }
        }
        if changed {
            self.minimize();
        }
    }

    /// Releases all (unfrozen) clock lower bounds, letting time advance.
    pub fn future(&mut self) {
        if self.clock_count <= 1 {
            return;
        }
        let mut changed = false;
        for i in 1..self.clock_count {
            if self.is_frozen(i) {
                continue;
            }
            let lb = dbm_offset(self.clock_count, 0, i);
            if self.matrix[lb] != INF_TIME {
                self.matrix[lb] = INF_TIME;
                changed = true;
            }
        }
        if changed {
            self.minimize();
        }
    }

    pub fn reset_clock(&mut self, clock_idx: usize) {
        self.check_index(clock_idx, clock_idx);
        if clock_idx == 0 {
            return;
        }
        let mut changed = false;

        if self.matrix[dbm_offset(self.clock_count, 0, clock_idx)] != 0 {
            self.matrix[dbm_offset(self.clock_count, 0, clock_idx)] = 0;
            changed = true;
        }
        if self.matrix[dbm_offset(self.clock_count, clock_idx, 0)] != 0 {
            self.matrix[dbm_offset(self.clock_count, clock_idx, 0)] = 0;
            changed = true;
        }

        for k in 0..self.clock_count {
            let row0 = self.matrix[dbm_offset(self.clock_count, 0, k)];
            if self.matrix[dbm_offset(self.clock_count, clock_idx, k)] != row0 {
                self.matrix[dbm_offset(self.clock_count, clock_idx, k)] = row0;
                changed = true;
            }
            let col0 = self.matrix[dbm_offset(self.clock_count, k, 0)];
            if self.matrix[dbm_offset(self.clock_count, k, clock_idx)] != col0 {
                self.matrix[dbm_offset(self.clock_count, k, clock_idx)] = col0;
                changed = true;
            }
        }

        self.matrix[dbm_offset(self.clock_count, clock_idx, clock_idx)] = 0;

        if changed {
            self.minimize();
        }
    }

    pub fn forget_clock(&mut self, clock_idx: usize) {
        self.check_index(clock_idx, clock_idx);
        if clock_idx == 0 {
            return;
        }
        let mut changed = false;
        for i in 0..self.clock_count {
            if i != clock_idx {
                if self.matrix[dbm_offset(self.clock_count, clock_idx, i)] != INF_TIME {
                    self.matrix[dbm_offset(self.clock_count, clock_idx, i)] = INF_TIME;
                    changed = true;
                }
                if self.matrix[dbm_offset(self.clock_count, i, clock_idx)] != INF_TIME {
                    self.matrix[dbm_offset(self.clock_count, i, clock_idx)] = INF_TIME;
                    changed = true;
                }
            }
        }

        if self.matrix[dbm_offset(self.clock_count, clock_idx, 0)] != INF_TIME {
            self.matrix[dbm_offset(self.clock_count, clock_idx, 0)] = INF_TIME;
            changed = true;
        }
        if self.matrix[dbm_offset(self.clock_count, 0, clock_idx)] != 0 {
            self.matrix[dbm_offset(self.clock_count, 0, clock_idx)] = 0;
            changed = true;
        }
        if self.matrix[dbm_offset(self.clock_count, clock_idx, clock_idx)] != 0 {
            self.matrix[dbm_offset(self.clock_count, clock_idx, clock_idx)] = 0;
            changed = true;
        }

        self.unfreeze_clock(clock_idx);

        if changed {
            self.minimize();
        }
    }

    /// Element-wise minimum of two same-sized DBMs, then closure.
    pub fn intersection(&self, other: &DBM) -> DBM {
        assert!(
            self.clock_count == other.clock_count,
            "DBM sizes must match for intersection"
        );
        let mut result = DBM::new(self.clock_count);
        for i in 0..self.clock_count {
            for j in 0..self.clock_count {
                let b1 = self.matrix[dbm_offset(self.clock_count, i, j)];
                let b2 = other.matrix[dbm_offset(other.clock_count, i, j)];
                let idx = dbm_offset(result.clock_count, i, j);
                result.matrix[idx] = match (b1, b2) {
                    (INF_TIME, _) => b2,
                    (_, INF_TIME) => b1,
                    _ => b1.min(b2),
                };
            }
        }
        result.minimize();
        result
    }

    /// Classic k-extrapolation (ExtraM with a uniform bound). Rows/columns of
    /// frozen clocks are left untouched.
    pub fn extrapolate(&mut self, k: i32) {
        if self.clock_count == 0 || k < 0 {
            return;
        }
        let mut changed = false;
        for i in 0..self.clock_count {
            if self.is_frozen(i) {
                continue;
            }
            for j in 0..self.clock_count {
                if i == j || self.is_frozen(j) {
                    continue;
                }
                let ij = dbm_offset(self.clock_count, i, j);
                let bound = self.matrix[ij];
                if bound == INF_TIME {
                    continue;
                }
                if bound > k {
                    self.matrix[ij] = INF_TIME;
                    changed = true;
                } else if bound < -k {
                    self.matrix[ij] = -k;
                    changed = true;
                }
            }
        }
        if changed {
            self.minimize();
        }
    }

    pub fn is_empty(&self) -> bool {
        !self.is_consistent()
    }

    pub fn prune(&mut self) {
        self.minimize();
    }

    /// `other` subset of `self` (this zone contains the other zone).
    pub fn contains(&self, other: &DBM) -> bool {
        if self.clock_count != other.clock_count {
            return false;
        }
        for i in 0..self.clock_count {
            for j in 0..self.clock_count {
                let this_bound = self.matrix[dbm_offset(self.clock_count, i, j)];
                let other_bound = other.matrix[dbm_offset(other.clock_count, i, j)];
                if other_bound != INF_TIME
                    && (this_bound == INF_TIME || other_bound < this_bound)
                {
                    return false;
                }
            }
        }
        self.frozen_clocks == other.frozen_clocks
    }

    /// This zone subset of `other` (sound only when both are canonical).
    pub fn included_in(&self, other: &DBM) -> bool {
        if self.clock_count != other.clock_count {
            return false;
        }
        for i in 0..self.clock_count {
            for j in 0..self.clock_count {
                let this_bound = self.matrix[dbm_offset(self.clock_count, i, j)];
                let other_bound = other.matrix[dbm_offset(other.clock_count, i, j)];
                if other_bound == INF_TIME {
                    continue;
                }
                if this_bound == INF_TIME || this_bound > other_bound {
                    return false;
                }
            }
        }
        true
    }

    pub fn raw_matrix(&self) -> &[i32] {
        &self.matrix
    }

    pub fn frozen_clocks(&self) -> &[usize] {
        &self.frozen_clocks
    }

    pub fn remove_clock(&mut self, clock_idx: usize) {
        if clock_idx >= self.clock_count || clock_idx == 0 {
            return;
        }
        self.unfreeze_clock(clock_idx);

        let new_count = self.clock_count - 1;
        let mut new_matrix = vec![INF_TIME; new_count * new_count];

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
                new_matrix[new_i * new_count + new_j] = self.matrix[dbm_offset(self.clock_count, i, j)];
            }
        }

        self.matrix = new_matrix;
        self.clock_count = new_count;

        let mut new_frozen = Vec::new();
        for idx in &self.frozen_clocks {
            if *idx < clock_idx {
                new_frozen.push(*idx);
            } else if *idx > clock_idx {
                new_frozen.push(*idx - 1);
            }
        }
        self.frozen_clocks = new_frozen;
    }

    pub fn restrict_clock(&self, clock_idx: usize, alpha: i32, beta: i32) -> DBM {
        if clock_idx >= self.clock_count {
            return self.clone();
        }
        let mut result = self.clone();

        let current_lower = -result.get_constraint(0, clock_idx);
        if alpha > current_lower {
            result.set_constraint(0, clock_idx, -alpha);
        }

        let current_upper = result.get_constraint(clock_idx, 0);
        if beta != INF_TIME && (current_upper == INF_TIME || beta < current_upper) {
            result.set_constraint(clock_idx, 0, beta);
        }

        result.minimize();

        for i in 0..result.clock_count {
            if result.matrix[dbm_offset(result.clock_count, i, i)] < 0 {
                return DBM::new(0);
            }
        }
        result
    }

    pub fn constrain_upper_bound(&mut self, clock_idx: usize, beta: i32) {
        if clock_idx >= self.clock_count || beta == INF_TIME {
            return;
        }
        let current_upper = self.matrix[dbm_offset(self.clock_count, clock_idx, 0)];
        if current_upper != INF_TIME && beta < current_upper {
            self.matrix[dbm_offset(self.clock_count, clock_idx, 0)] = beta;
            self.minimize();
        }
    }

    pub fn synchronize_clocks(&mut self, clock_indices: &[usize]) {
        if clock_indices.len() < 2 {
            return;
        }
        let mut changed = false;
        for left in 0..clock_indices.len() {
            let first = clock_indices[left];
            if first >= self.clock_count {
                continue;
            }
            for right in left + 1..clock_indices.len() {
                let second = clock_indices[right];
                if second >= self.clock_count {
                    continue;
                }
                if self.matrix[dbm_offset(self.clock_count, first, second)] != 0 {
                    self.matrix[dbm_offset(self.clock_count, first, second)] = 0;
                    changed = true;
                }
                if self.matrix[dbm_offset(self.clock_count, second, first)] != 0 {
                    self.matrix[dbm_offset(self.clock_count, second, first)] = 0;
                    changed = true;
                }
            }
        }
        if changed {
            self.minimize();
        }
    }

    pub fn restrict_for_firing(&self, transition_id: usize, alpha: i32, beta: i32) -> DBM {
        self.restrict_clock(transition_id + 1, alpha, beta)
    }

    pub fn freeze_clock(&mut self, clock_idx: usize) {
        if clock_idx >= self.clock_count || clock_idx == 0 {
            return;
        }
        match self.frozen_clocks.binary_search(&clock_idx) {
            Ok(_) => {}
            Err(pos) => self.frozen_clocks.insert(pos, clock_idx),
        }
    }

    pub fn unfreeze_clock(&mut self, clock_idx: usize) {
        if let Ok(pos) = self.frozen_clocks.binary_search(&clock_idx) {
            self.frozen_clocks.remove(pos);
        }
    }

    pub fn is_frozen(&self, clock_idx: usize) -> bool {
        self.frozen_clocks.binary_search(&clock_idx).is_ok()
    }

    pub fn copy_clock_constraints(&self, clock_idx: usize, target: &mut DBM) {
        if clock_idx >= self.clock_count {
            return;
        }
        if clock_idx >= target.size() {
            target.resize(clock_idx + 1);
        }
        for i in 0..self.clock_count {
            if i < target.size() {
                target.set_constraint(clock_idx, i, self.matrix[dbm_offset(self.clock_count, clock_idx, i)]);
                target.set_constraint(i, clock_idx, self.matrix[dbm_offset(self.clock_count, i, clock_idx)]);
            }
        }
        if self.is_frozen(clock_idx) {
            target.freeze_clock(clock_idx);
        }
    }
}

impl Eq for DBM {}

impl PartialEq for DBM {
    fn eq(&self, other: &Self) -> bool {
        self.clock_count == other.clock_count
            && self.frozen_clocks == other.frozen_clocks
            && self.matrix == other.matrix
    }
}

impl PartialOrd for DBM {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for DBM {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        if self.clock_count != other.clock_count {
            return self.clock_count.cmp(&other.clock_count);
        }
        for (a, b) in self.matrix.iter().zip(other.matrix.iter()) {
            if a != b {
                if *a == INF_TIME {
                    return std::cmp::Ordering::Greater;
                }
                if *b == INF_TIME {
                    return std::cmp::Ordering::Less;
                }
                return a.cmp(b);
            }
        }
        self.frozen_clocks.cmp(&other.frozen_clocks)
    }
}

impl fmt::Display for DBM {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.clock_count == 0 {
            return write!(f, "DBM(empty)");
        }
        writeln!(f, "DBM(size={}):", self.clock_count)?;
        write!(f, "   ")?;
        for j in 0..self.clock_count {
            write!(f, "{:>8}", format!("x{}", j))?;
        }
        writeln!(f)?;
        for i in 0..self.clock_count {
            write!(f, "x{} ", i)?;
            for j in 0..self.clock_count {
                let value = self.matrix[dbm_offset(self.clock_count, i, j)];
                write!(
                    f,
                    "{:>8}",
                    if value == INF_TIME {
                        "inf".to_string()
                    } else {
                        value.to_string()
                    }
                )?;
            }
            writeln!(f)?;
        }
        Ok(())
    }
}
