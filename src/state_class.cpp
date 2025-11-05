#include "state_class.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace state_class {

DBM::DBM(size_t size) : clock_count_(size) {
    if (size > 0) {
        matrix_.resize(size, std::vector<int>(size, INF_TIME));
        // 初始化：x_i - x_i <= 0
        for (size_t i = 0; i < size; ++i) {
            matrix_[i][i] = 0;
        }
        // 初始化：x_i - x_0 <= INF, x_0 - x_i <= 0 (x_0 为参考时钟)
        if (size > 1) {
            for (size_t i = 1; i < size; ++i) {
                matrix_[i][0] = INF_TIME;  // x_i - x_0 <= INF
                matrix_[0][i] = 0;          // x_0 - x_i <= 0
            }
        }
    }
}

void DBM::check_index(size_t i, size_t j) const {
    if (i >= clock_count_ || j >= clock_count_) {
        throw std::out_of_range("DBM index out of range");
    }
}

void DBM::set_constraint(size_t i, size_t j, int bound) {
    check_index(i, j);
    matrix_[i][j] = bound;
}

int DBM::get_constraint(size_t i, size_t j) const {
    check_index(i, j);
    return matrix_[i][j];
}

bool DBM::is_consistent() const {
    if (clock_count_ == 0) return true;
    
    // 检查对角线上是否有负值（表示矛盾）
    for (size_t i = 0; i < clock_count_; ++i) {
        if (matrix_[i][i] < 0) {
            return false;
        }
    }
    
    // 使用 Floyd-Warshall 检查是否存在负环
    DBM temp = *this;
    temp.minimize();
    
    // 如果最小化后对角线上有负值，说明存在负环
    for (size_t i = 0; i < clock_count_; ++i) {
        if (temp.matrix_[i][i] < 0) {
            return false;
        }
    }
    
    return true;
}

void DBM::minimize() {
    if (clock_count_ == 0) return;
    
    // Floyd-Warshall 算法：计算所有时钟对之间的最短路径
    for (size_t k = 0; k < clock_count_; ++k) {
        for (size_t i = 0; i < clock_count_; ++i) {
            if (matrix_[i][k] == INF_TIME) continue;
            
            for (size_t j = 0; j < clock_count_; ++j) {
                if (matrix_[k][j] == INF_TIME) continue;
                
                int new_bound = matrix_[i][k] + matrix_[k][j];
                if (matrix_[i][j] == INF_TIME || new_bound < matrix_[i][j]) {
                    matrix_[i][j] = new_bound;
                }
            }
        }
    }
}

size_t DBM::add_clock() {
    size_t new_idx = clock_count_;
    resize(clock_count_ + 1);
    return new_idx;
}

void DBM::resize(size_t new_size) {
    if (new_size == clock_count_) return;
    
    size_t old_size = clock_count_;
    clock_count_ = new_size;
    
    matrix_.resize(new_size);
    for (auto& row : matrix_) {
        row.resize(new_size, INF_TIME);
    }
    
    for (size_t i = old_size; i < new_size; ++i) {
        initialize_clock(i);
    }
}

void DBM::initialize_clock(size_t clock_idx) {
    if (clock_idx >= clock_count_) return;
    
    // x_i - x_i <= 0
    matrix_[clock_idx][clock_idx] = 0;
    
    // 与新时钟相关的约束
    if (clock_idx == 0) {
        // 参考时钟：x_0 - x_i <= 0 (对于所有 i)
        for (size_t i = 1; i < clock_count_; ++i) {
            matrix_[0][i] = 0;
            matrix_[i][0] = INF_TIME;
        }
    } else {
        // 新时钟：x_i - x_0 <= INF, x_0 - x_i <= 0
        matrix_[clock_idx][0] = INF_TIME;
        matrix_[0][clock_idx] = 0;
        
        // 与其他时钟的关系：初始化为无约束
        for (size_t i = 1; i < clock_count_; ++i) {
            if (i != clock_idx) {
                matrix_[clock_idx][i] = INF_TIME;
                matrix_[i][clock_idx] = INF_TIME;
            }
        }
    }
}

void DBM::elapse_time(int delta) {
    if (delta <= 0) return;
    
    // 时间推进：所有时钟同步增加 delta
    // 这相当于放宽所有上界约束（相对于参考时钟）
    // x_i - x_0 <= INF 保持不变
    // 实际上，时间推进不需要修改矩阵，因为 DBM 表示的是相对时间差
    // 但如果需要显式推进，可以增加所有相对于参考时钟的上界
    // 这里我们保持 DBM 的语义：时间推进通过重置时钟来实现
}

void DBM::reset_clock(size_t clock_idx) {
    check_index(clock_idx, clock_idx);
    
    // 重置时钟：x_clock_idx = 0
    // 这意味着 x_clock_idx - x_0 <= 0 且 x_0 - x_clock_idx <= 0
    if (clock_idx == 0) {
        // 重置参考时钟没有意义
        return;
    }
    
    matrix_[clock_idx][0] = 0;      // x_clock_idx - x_0 <= 0
    matrix_[0][clock_idx] = 0;      // x_0 - x_clock_idx <= 0
    
    // 重新最小化以传播约束
    minimize();
}

DBM DBM::intersection(const DBM& other) const {
    if (clock_count_ != other.clock_count_) {
        throw std::invalid_argument("DBM sizes must match for intersection");
    }
    
    DBM result(clock_count_);
    
    // 取每个约束的最小值（更紧的约束）
    for (size_t i = 0; i < clock_count_; ++i) {
        for (size_t j = 0; j < clock_count_; ++j) {
            int bound1 = matrix_[i][j];
            int bound2 = other.matrix_[i][j];
            
            if (bound1 == INF_TIME) {
                result.matrix_[i][j] = bound2;
            } else if (bound2 == INF_TIME) {
                result.matrix_[i][j] = bound1;
            } else {
                result.matrix_[i][j] = std::min(bound1, bound2);
            }
        }
    }
    
    // 检查一致性并最小化
    result.minimize();
    
    return result;
}

bool DBM::is_empty() const {
    return !is_consistent();
}

void DBM::prune() {
    // 最小化已经实现了剪枝的功能
    minimize();
}

bool DBM::contains(const DBM& other) const {
    if (clock_count_ != other.clock_count_) {
        return false;
    }
    
    // this 包含 other 当且仅当 other 的所有约束都比 this 更紧
    for (size_t i = 0; i < clock_count_; ++i) {
        for (size_t j = 0; j < clock_count_; ++j) {
            int this_bound = matrix_[i][j];
            int other_bound = other.matrix_[i][j];
            
            // 如果 other 的约束更紧，则 this 不包含 other
            if (other_bound != INF_TIME && 
                (this_bound == INF_TIME || other_bound < this_bound)) {
                return false;
            }
        }
    }
    
    return true;
}

std::string DBM::to_string() const {
    if (clock_count_ == 0) {
        return "DBM(empty)";
    }
    
    std::ostringstream oss;
    oss << "DBM(size=" << clock_count_ << "):\n";
    oss << "   ";
    for (size_t j = 0; j < clock_count_; ++j) {
        oss << std::setw(8) << "x" << j;
    }
    oss << "\n";
    
    for (size_t i = 0; i < clock_count_; ++i) {
        oss << "x" << i << " ";
        for (size_t j = 0; j < clock_count_; ++j) {
            if (matrix_[i][j] == INF_TIME) {
                oss << std::setw(8) << "∞";
            } else {
                oss << std::setw(8) << matrix_[i][j];
            }
        }
        oss << "\n";
    }
    
    return oss.str();
}

bool DBM::operator==(const DBM& other) const {
    if (clock_count_ != other.clock_count_) {
        return false;
    }
    
    for (size_t i = 0; i < clock_count_; ++i) {
        for (size_t j = 0; j < clock_count_; ++j) {
            if (matrix_[i][j] != other.matrix_[i][j]) {
                return false;
            }
        }
    }
    
    return true;
}

bool DBM::operator<(const DBM& other) const {
    if (clock_count_ != other.clock_count_) {
        return clock_count_ < other.clock_count_;
    }
    
    for (size_t i = 0; i < clock_count_; ++i) {
        for (size_t j = 0; j < clock_count_; ++j) {
            if (matrix_[i][j] != other.matrix_[i][j]) {
                if (matrix_[i][j] == INF_TIME) return false;
                if (other.matrix_[i][j] == INF_TIME) return true;
                return matrix_[i][j] < other.matrix_[i][j];
            }
        }
    }
    
    return false;
}

// ==================== StateClass 实现 ====================

bool StateClass::operator==(const StateClass& other) const {
    if (marking != other.marking) return false;
    if (!(Z1 == other.Z1)) return false;
    if (!(Z2 == other.Z2)) return false;
    return true;
}

bool StateClass::operator<(const StateClass& other) const {
    if (marking < other.marking) return true;
    if (other.marking < marking) return false;
    
    if (Z1 < other.Z1) return true;
    if (other.Z1 < Z1) return false;
    
    return Z2 < other.Z2;
}

std::string StateClass::to_string() const {
    std::ostringstream oss;
    oss << "StateClass(id=" << state_id << ", time=" << cumulative_time << ")\n";
    oss << "  Marking: [";
    for (size_t i = 0; i < marking.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << marking[i];
    }
    oss << "]\n";
    oss << "  Z1 (non-suspendable):\n" << Z1.to_string();
    oss << "  Z2 (suspendable):\n" << Z2.to_string();
    return oss.str();
}

} // namespace state_class
