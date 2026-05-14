#include "state_class_graph.h"

#include <algorithm>
#include <boost/log/trivial.hpp>
#include <fstream>
#include <iomanip>
#include <map>
#include <queue>
#include <set>
#include <sstream>

namespace state_class {

std::string StateClassReachabilityGraph::format_marking(
    const std::vector<int>& marking) {
  std::string result = "[";
  for (size_t i = 0; i < marking.size(); ++i) {
    result += std::to_string(marking[i]);
    if (i < marking.size() - 1) {
      result += ", ";
    }
  }
  result += "]";
  return result;
}

std::string StateClassReachabilityGraph::format_transitions(
    const std::set<size_t>& trans_indices, bool detailed) const {
  if (trans_indices.empty()) {
    return "(无)";
  }

  std::string result;
  bool first = true;
  for (size_t t : trans_indices) {
    if (!first) {
      result += ", ";
    }
    first = false;

    if (detailed && t < ptpn_.num_transitions()) {
      const auto& trans = ptpn_.get_transition(t);
      result += "T" + std::to_string(t) + "(" + trans.name;
      result += ", 优先级=" + std::to_string(trans.priority);
      result += ", 核心=" + std::to_string(trans.core);
      result += trans.suspendable ? ", 可挂起" : "";
      result += ")";
    } else {
      result += "T" + std::to_string(t);
    }
  }
  return result;
}

std::string StateClassReachabilityGraph::format_places(
    const std::vector<int>& marking) const {
  std::string result = "[";
  bool first = true;
  for (size_t i = 0; i < marking.size(); ++i) {
    if (!first) {
      result += ", ";
    }
    first = false;

    if (i < ptpn_.num_places()) {
      const auto& place = ptpn_.get_place(i);
      result += "P" + std::to_string(i) + "(" + place.name +
                ")=" + std::to_string(marking[i]);
    } else {
      result += "P" + std::to_string(i) + "=" + std::to_string(marking[i]);
    }
  }
  result += "]";
  return result;
}

void StateClassReachabilityGraph::log_state_class_details(
    const StateClass& state, const std::string& prefix) const {
  BOOST_LOG_TRIVIAL(debug) << prefix
                           << "========== 状态类详情: ID=" << state.state_id
                           << " ==========";
  BOOST_LOG_TRIVIAL(debug) << prefix
                           << "库所信息: " << format_places(state.marking);
  BOOST_LOG_TRIVIAL(debug) << prefix
                           << "使能变迁: " << format_transitions(state.enabled);
  if (!state.suspended.empty()) {
    BOOST_LOG_TRIVIAL(debug)
        << prefix << "挂起变迁: " << format_transitions(state.suspended);
  }
  BOOST_LOG_TRIVIAL(debug) << prefix << "累计时间: " << state.cumulative_time;

  BOOST_LOG_TRIVIAL(debug) << prefix << "Z1 (不可挂起变迁):";
  std::string z1_str = state.Z1.to_string();
  std::istringstream z1_stream(z1_str);
  std::string z1_line;
  while (std::getline(z1_stream, z1_line)) {
    BOOST_LOG_TRIVIAL(debug) << prefix << "  " << z1_line;
  }

  BOOST_LOG_TRIVIAL(debug) << prefix << "Z2 (可挂起变迁):";
  std::string z2_str = state.Z2.to_string();
  std::istringstream z2_stream(z2_str);
  std::string z2_line;
  while (std::getline(z2_stream, z2_line)) {
    BOOST_LOG_TRIVIAL(debug) << prefix << "  " << z2_line;
  }

  BOOST_LOG_TRIVIAL(debug) << prefix
                           << "==========================================";
}

StateClassReachabilityGraph::StateClassReachabilityGraph(
    const matrix_ptpn::MatrixPTPN& ptpn)
    : ptpn_(ptpn), next_state_id_(0), pruning_enabled_(false) {}

size_t StateClassReachabilityGraph::build(size_t max_states) {
  stats_ = Statistics();
  state_to_vertex_.clear();

  StateClass s0 = create_initial_state_class();
  StateClass canonical_s0 = canonicalize(s0);

  StateClassVertex s0_vertex = find_or_add_vertex(s0);
  initial_vertex_ = s0_vertex;
  stats_.total_states++;

  std::map<std::tuple<std::vector<int>, DBM, DBM>, StateClassVertex> uniq;
  uniq[{s0.marking, s0.Z1, s0.Z2}] = s0_vertex;

  std::queue<StateClass> Q;
  Q.push(canonical_s0);

  size_t iteration = 0;
  while (!Q.empty() && stats_.total_states < max_states) {
    iteration++;
    StateClass cur = Q.front();
    Q.pop();

    StateClassVertex u = find_or_add_vertex(cur);

    log_state_class_details(cur,
                            "[状态 " + std::to_string(cur.state_id) + "] ");

    // 2.1 每个核心内挑选"最高优先级"候选
    std::vector<size_t> chosen = select_per_core(cur.enabled);
    stats_.enabled_transitions_count += chosen.size();

    // 2.2 抢占：若 chosen 引入更高优先级，则冻结同核低优先级且可挂起的时钟
    StateClass scheduled = cur.copy();
    apply_preemption(chosen, scheduled);

    // 2.3 最大化时间推进：推进到任一未冻结时钟的最小上界
    double dt = 0;
    if (maximal_time_elapse(scheduled, dt)) {
      BOOST_LOG_TRIVIAL(debug) << "  执行最大化时间推进: dt = " << dt;
    }

    // 检查 DBM 是否为空（仅在启用剪枝时跳过）
    if (pruning_enabled_ && scheduled.Z1.is_empty()) {
      BOOST_LOG_TRIVIAL(debug) << "  [剪枝] Z1为空,跳过此状态";
      stats_.pruned_states_count++;
      continue;
    } else if (!pruning_enabled_ && scheduled.Z1.is_empty()) {
      BOOST_LOG_TRIVIAL(debug) << "  [警告] Z1为空,但剪枝已禁用,继续处理";
    }

    // 2.4 对每个被选中的变迁尝试发生（跨核可产生多条出边）
    size_t fired_count = 0;
    for (size_t t : chosen) {
      auto [ok, nxt, tau] = fire_with_dbm(t, scheduled);

      if (!ok) {
        if (pruning_enabled_) {
          BOOST_LOG_TRIVIAL(debug)
              << "  " << format_transitions({t}, false) << ": 触发失败";
          stats_.pruned_states_count++;
          continue;
        } else {
          BOOST_LOG_TRIVIAL(debug)
              << "  " << format_transitions({t}, false)
              << ": 触发失败 [警告] 剪枝已禁用,继续处理此变迁";
          continue;
        }
      }

      BOOST_LOG_TRIVIAL(debug) << "  " << format_transitions({t}, false)
                               << " -> 后继状态: ID=" << nxt.state_id;

      // 2.5 去重/包含剪枝
      auto key = std::make_tuple(nxt.marking, nxt.Z1, nxt.Z2);
      StateClassVertex v;

      if (uniq.find(key) != uniq.end()) {
        v = uniq[key];
        BOOST_LOG_TRIVIAL(debug) << "  [已存在] 使用已有状态";
      } else {
        // 规范化后继状态
        StateClass canonical_nxt = canonicalize(nxt);
        v = find_or_add_vertex(nxt);
        Q.push(canonical_nxt);
        uniq[key] = v;
        stats_.total_states++;
        BOOST_LOG_TRIVIAL(debug) << "  [新状态] 添加到图和队列";
        // 输出新状态的详细信息
        log_state_class_details(
            nxt, "[新状态 " + std::to_string(nxt.state_id) + "] ");
      }

      // 2.6 添加出边，边上记录 (t, τ)；τ 为触发时的累计时间戳
      TransitionEdge edge(static_cast<int>(t), tau);
      boost::add_edge(u, v, edge, graph_);
      stats_.total_transitions++;
      fired_count++;
    }

    BOOST_LOG_TRIVIAL(info) << "[STATE_CLASS] 状态 " << cur.state_id
                            << ": 选择了 " << chosen.size() << " 个候选变迁, "
                            << "成功触发 " << fired_count << " 个, "
                            << "队列大小: " << Q.size() << ", "
                            << "总状态数: " << stats_.total_states;
  }

  BOOST_LOG_TRIVIAL(info) << "[STATE_CLASS] 构建完成: 总迭代次数=" << iteration
                          << ", 总状态数=" << stats_.total_states;

  return stats_.total_states;
}

StateClass StateClassReachabilityGraph::create_initial_state_class() {
  StateClass initial;
  initial.marking = ptpn_.get_marking();
  initial.state_id = next_state_id_++;
  initial.cumulative_time = 0.0;

  size_t num_transitions = ptpn_.num_transitions();

  initial.Z1.resize(num_transitions + 1);  // +1 为参考时钟
  initial.Z2.resize(num_transitions + 1);

  // 使用新的方法计算使能和时钟
  compute_enabled_and_clocks(initial);

  // 输出初始状态的详细信息
  log_state_class_details(initial, "[初始状态] ");

  BOOST_LOG_TRIVIAL(debug) << "[STATE_CLASS] 创建初始状态:";
  BOOST_LOG_TRIVIAL(debug) << "  标识: " << format_marking(initial.marking);
  BOOST_LOG_TRIVIAL(debug) << "  使能变迁: "
                           << format_transitions(initial.enabled);
  if (!initial.suspended.empty()) {
    BOOST_LOG_TRIVIAL(debug)
        << "  挂起变迁: " << format_transitions(initial.suspended);
  }

  return initial;
}

void StateClassReachabilityGraph::explore_successors(
    const StateClass& current_state, std::set<StateClass>& visited) {}

bool StateClassReachabilityGraph::is_transition_enabled(
    const StateClass& state, size_t trans_idx) const {
  return matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, trans_idx);
}

std::pair<int, int> StateClassReachabilityGraph::get_transition_time_bounds(
    const StateClass& state, size_t trans_idx) const {
  const auto& transition = ptpn_.get_transition(trans_idx);
  int earliest = transition.time_interval.earliest;
  int latest = transition.time_interval.latest;

  // 考虑 DBM 约束
  // 这里简化处理:直接使用变迁的时间区间
  // 实际应用中需要结合 Z1 和 Z2 的约束

  return {earliest, latest};
}

std::pair<DBM, DBM> StateClassReachabilityGraph::time_advance(
    const StateClass& state) const {
  // 时间推进:放宽所有上界约束(相对于参考时钟)
  // 这意味着所有时钟可以同步增长,直到下一个变迁触发
  // Time advance: relax upper bound constraints (relative to reference clock)
  // This allows all clocks to grow synchronously until next transition fires

  DBM z1_up = state.Z1;
  DBM z2_up = state.Z2;

  // 获取不变量约束
  // Get invariant constraints
  DBM invariants = get_invariants_for(state.marking);

  // 时间推进:对于不可挂起变迁,放宽上界约束
  // 但保持下界约束不变(因为时间不能倒退)
  // 重要:如果 earliest == latest,不能放宽上界！
  // Time advance: relax upper bound constraints for non-suspendable transitions
  // But keep lower bound constraints unchanged (time cannot go backwards)
  // Important: if earliest == latest, cannot relax upper bound!

  size_t num_clocks = z1_up.size();
  size_t num_transitions = ptpn_.num_transitions();

  size_t relaxed_count = 0;

  // 对于 Z1:放宽不可挂起变迁的上界
  // 但需要检查对应的变迁是否有 earliest == latest 的情况
  // For Z1: relax upper bound for non-suspendable transitions
  // But need to check if corresponding transition has earliest == latest
  for (size_t i = 1; i < num_clocks && i <= num_transitions; ++i) {
    size_t trans_idx = i - 1;  // 时钟索引 i 对应变迁 trans_idx

    // 检查对应的变迁是否使能
    // Check if corresponding transition is enabled
    bool is_enabled = false;
    if (trans_idx < num_transitions) {
      is_enabled =
          matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, trans_idx);
    }

    if (!is_enabled) {
      continue;  // 变迁未使能,跳过
    }

    // 检查时钟是否被冻结(挂起状态)
    // Check if clock is frozen (suspended state)
    if (z1_up.is_frozen(i)) {
      BOOST_LOG_TRIVIAL(debug)
          << "    时钟" << i << "(" << format_transitions({trans_idx}, false)
          << "): 被冻结,不参与时间推进";
      continue;  // 冻结时钟不参与时间推进
    }

    // 检查是否是精确时间约束(earliest == latest)
    // Check if exact time constraint (earliest == latest)
    const auto& transition = ptpn_.get_transition(trans_idx);
    bool is_exact_time =
        (transition.time_interval.earliest == transition.time_interval.latest &&
         transition.time_interval.latest != matrix_ptpn::INF);

    // 如果是最精确时间约束且不可挂起,不能放宽上界(必须精确触发)
    // If exact time constraint and non-suspendable, cannot relax upper bound
    // (must fire exactly)
    if (is_exact_time && !transition.suspendable) {
      BOOST_LOG_TRIVIAL(debug)
          << "    时钟" << i << "(" << format_transitions({trans_idx}, false)
          << "): 精确时间约束,不放宽上界";
      continue;  // 保持精确约束,不进行时间推进
    }

    // 对于非精确时间约束,可以放宽上界
    // For non-exact time constraints, can relax upper bound
    int current_upper = z1_up.get_constraint(i, 0);
    if (current_upper != INF_TIME) {
      // 放宽上界约束,允许时间推进
      // Relax upper bound constraint, allow time advance
      z1_up.set_constraint(i, 0, INF_TIME);
      relaxed_count++;
      BOOST_LOG_TRIVIAL(debug)
          << "    时钟" << i << "(" << format_transitions({trans_idx}, false)
          << "): 放宽上界 " << current_upper << " -> INF";
    }
  }

  BOOST_LOG_TRIVIAL(debug) << "  时间推进: 放宽了 " << relaxed_count
                           << " 个时钟的上界";

  // 对于 Z2:可挂起变迁在时间推进时不受限制
  // Z2 的约束保持不变(因为可挂起变迁可以被暂停)

  // 计算 Z1 与不变量的交集
  if (invariants.size() > 0 && z1_up.size() == invariants.size()) {
    z1_up = z1_up.intersection(invariants);
  }

  // 最小化 DBM
  z1_up.minimize();
  z2_up.minimize();

  return {z1_up, z2_up};
}

bool StateClassReachabilityGraph::is_suspended(
    size_t trans_idx, const std::vector<size_t>& enabled) const {
  const auto& transition = ptpn_.get_transition(trans_idx);

  if (!transition.suspendable) {
    return false;
  }

  int trans_core = transition.core;
  int trans_priority = transition.priority;

  for (size_t other_t : enabled) {
    if (other_t == trans_idx) continue;

    const auto& other_trans = ptpn_.get_transition(other_t);
    if (other_trans.core == trans_core && !other_trans.suspendable &&
        other_trans.priority > trans_priority) {
      return true;  // 被挂起
    }
  }

  return false;
}

bool StateClassReachabilityGraph::check_dbm_time_intersection(
    const DBM& z1, size_t trans_idx) const {
  const auto& transition = ptpn_.get_transition(trans_idx);

  // 检查 Z1 中的时钟约束是否与 [alpha(t), beta(t)] 有交集
  size_t clock_idx = trans_idx + 1;  // +1 因为索引 0 是参考时钟

  if (clock_idx >= z1.size()) {
    return false;
  }

  int alpha = transition.time_interval.earliest;
  int beta = transition.time_interval.latest == matrix_ptpn::INF
                 ? INF_TIME
                 : transition.time_interval.latest;

  // 使用restrict_for_firing检查交集
  DBM restricted = restrict_for_firing(z1, trans_idx);
  return !restricted.is_empty();
}

DBM StateClassReachabilityGraph::restrict_for_firing(const DBM& z,
                                                     size_t trans_idx) const {
  // 限制DBM以反映变迁的触发时间窗口 [α, β]

  const auto& transition = ptpn_.get_transition(trans_idx);
  int alpha = transition.time_interval.earliest;
  int beta = transition.time_interval.latest == matrix_ptpn::INF
                 ? INF_TIME
                 : transition.time_interval.latest;

  return z.restrict_for_firing(trans_idx, alpha, beta);
}

double StateClassReachabilityGraph::compute_firing_time(
    const DBM& z1_up, size_t trans_idx) const {
  const auto& transition = ptpn_.get_transition(trans_idx);

  size_t clock_idx = trans_idx + 1;
  if (clock_idx >= z1_up.size()) {
    return static_cast<double>(transition.time_interval.earliest);
  }

  int alpha = transition.time_interval.earliest;
  int beta = transition.time_interval.latest == matrix_ptpn::INF
                 ? INF_TIME
                 : transition.time_interval.latest;

  // 从 DBM 中获取下界
  int dbm_lower =
      -z1_up.get_constraint(0, clock_idx);  // x_clock - x_0 >= dbm_lower

  // 取满足下界的最大时间(alpha 和 dbm_lower 的最大值)
  int firing_time_int = std::max(alpha, dbm_lower);

  // 确保不超过上界
  if (beta != INF_TIME && firing_time_int > beta) {
    firing_time_int = beta;
  }

  return static_cast<double>(firing_time_int);
}

StateClass StateClassReachabilityGraph::canonicalize(
    const StateClass& state) const {
  StateClass canonical = state;

  // 规范化 DBM:最小化
  canonical.Z1.minimize();
  canonical.Z2.minimize();

  return canonical;
}

void StateClassReachabilityGraph::recompute_suspension(
    StateClass& state) const {
  // 重新计算所有变迁的挂起/恢复状态

  // 1. 找出所有使能的变迁
  std::set<size_t> enabled;
  for (size_t t = 0; t < ptpn_.num_transitions(); ++t) {
    if (matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, t)) {
      enabled.insert(t);
    }
  }

  state.enabled = enabled;

  // 2. 检查每个使能变迁的挂起状态
  std::set<size_t> suspended;
  std::vector<size_t> enabled_vec(enabled.begin(), enabled.end());

  for (size_t t : enabled) {
    if (is_suspended(t, enabled_vec)) {
      suspended.insert(t);

      // 如果变迁从非挂起变为挂起,冻结时钟(从Z1移动到Z2)
      if (state.suspended.find(t) == state.suspended.end()) {
        BOOST_LOG_TRIVIAL(debug) << "    " << format_transitions({t}, false)
                                 << ": 变为挂起状态,冻结时钟";
        size_t clock_idx = t + 1;
        state.Z1.copy_clock_constraints(clock_idx, state.Z2);
        state.Z1.freeze_clock(clock_idx);
        state.Z2.freeze_clock(clock_idx);
      }
    } else {
      // 如果变迁从挂起变为非挂起,解冻时钟(从Z2移回Z1)
      if (state.suspended.find(t) != state.suspended.end()) {
        BOOST_LOG_TRIVIAL(debug) << "    " << format_transitions({t}, false)
                                 << ": 变为非挂起状态,解冻时钟";
        size_t clock_idx = t + 1;
        state.Z2.copy_clock_constraints(clock_idx, state.Z1);
        state.Z1.unfreeze_clock(clock_idx);
        state.Z2.unfreeze_clock(clock_idx);
      }
    }
  }

  state.suspended = suspended;

  // 3. 更新DBM约束
  const_cast<StateClassReachabilityGraph*>(this)->update_dbm_constraints(state);
}

DBM StateClassReachabilityGraph::get_invariants_for(
    const std::vector<int>& marking) const {
  size_t num_transitions = ptpn_.num_transitions();
  DBM invariants(num_transitions + 1);

  return invariants;
}

StateClass StateClassReachabilityGraph::fire_transition(const StateClass& state,
                                                        size_t trans_idx,
                                                        double firing_time) {
  BOOST_LOG_TRIVIAL(debug) << "    触发变迁 "
                           << format_transitions({trans_idx}, false)
                           << " @时间 " << firing_time;

  StateClass new_state = state;

  // 更新标识
  new_state.marking =
      matrix_ptpn::MatrixPTPN::fire(state.marking, ptpn_, trans_idx);

  BOOST_LOG_TRIVIAL(debug) << "      标识变化: "
                           << format_marking(state.marking) << " -> "
                           << format_marking(new_state.marking);

  // 更新时间:累积时间应该是当前状态时间 + 触发时间
  // 但注意:firing_time 是相对于当前状态的相对时间
  new_state.cumulative_time = state.cumulative_time + firing_time;

  const auto& transition = ptpn_.get_transition(trans_idx);
  size_t clock_idx = trans_idx + 1;  // +1 因为索引 0 是参考时钟

  // 重置触发变迁的时钟
  if (transition.suspendable) {
    BOOST_LOG_TRIVIAL(debug) << "      重置Z2中的时钟 " << clock_idx;
    new_state.Z2.reset_clock(clock_idx);
  } else {
    BOOST_LOG_TRIVIAL(debug) << "      重置Z1中的时钟 " << clock_idx;
    new_state.Z1.reset_clock(clock_idx);
  }

  // 更新 DBM 约束:为新的使能变迁添加时间约束
  update_dbm_constraints(new_state);

  return new_state;
}

void StateClassReachabilityGraph::update_dbm_constraints(StateClass& state) {
  size_t num_transitions = ptpn_.num_transitions();

  // 首先,确保 Z1 和 Z2 的大小正确
  if (state.Z1.size() < num_transitions + 1) {
    state.Z1.resize(num_transitions + 1);
  }
  if (state.Z2.size() < num_transitions + 1) {
    state.Z2.resize(num_transitions + 1);
  }

  // 获取使能变迁
  std::vector<size_t> enabled;
  for (size_t t = 0; t < ptpn_.num_transitions(); ++t) {
    if (matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, t)) {
      enabled.push_back(t);
    }
  }

  size_t cleared_count = 0;
  size_t initialized_count = 0;

  // 先清除所有不再使能的变迁的约束(重置时钟)
  for (size_t t = 0; t < num_transitions; ++t) {
    bool is_enabled = false;
    for (size_t e : enabled) {
      if (e == t) {
        is_enabled = true;
        break;
      }
    }

    if (!is_enabled) {
      // 变迁不再使能,重置时钟并清除约束
      size_t clock_idx = t + 1;

      if (clock_idx < state.Z1.size()) {
        state.Z1.reset_clock(clock_idx);
        cleared_count++;
      }
      if (clock_idx < state.Z2.size()) {
        state.Z2.reset_clock(clock_idx);
      }

      // 移除冻结状态
      state.Z1.unfreeze_clock(clock_idx);
      state.Z2.unfreeze_clock(clock_idx);
    }
  }

  if (cleared_count > 0) {
    BOOST_LOG_TRIVIAL(debug)
        << "    清除了 " << cleared_count << " 个不再使能变迁的约束";
  }

  // 为每个使能变迁设置时间约束
  for (size_t trans_idx : enabled) {
    const auto& transition = ptpn_.get_transition(trans_idx);
    size_t clock_idx = trans_idx + 1;

    // 检查时钟是否刚刚被重置(值为0,且约束为初始状态)
    bool clock_just_reset = false;
    bool clock_exists = false;

    if (transition.suspendable) {
      if (clock_idx < state.Z2.size()) {
        clock_exists = true;
        int upper = state.Z2.get_constraint(clock_idx, 0);
        int lower = -state.Z2.get_constraint(0, clock_idx);
        // 如果时钟是初始状态(上界为INF,下界为0)
        if (upper == INF_TIME && lower == 0) {
          clock_just_reset = true;
        }
      }
    } else {
      if (clock_idx < state.Z1.size()) {
        clock_exists = true;
        int upper = state.Z1.get_constraint(clock_idx, 0);
        int lower = -state.Z1.get_constraint(0, clock_idx);
        // 如果时钟是初始状态(上界为INF,下界为0)
        if (upper == INF_TIME && lower == 0) {
          clock_just_reset = true;
        }
      }
    }

    // 如果时钟不存在或刚刚被重置(新使能),设置初始约束
    if (!clock_exists || clock_just_reset) {
      initialized_count++;
      BOOST_LOG_TRIVIAL(debug)
          << "    初始化" << format_transitions({trans_idx}, false)
          << "的时钟约束: [" << transition.time_interval.earliest << ", "
          << (transition.time_interval.latest == matrix_ptpn::INF
                  ? std::string("∞")
                  : std::to_string(transition.time_interval.latest))
          << "]";

      // 设置时间约束:x_clock - x_0 >= earliest, x_clock - x_0 <= latest
      // 在 DBM 中:x_0 - x_clock <= -earliest (即 x_clock - x_0 >= earliest)
      //            x_clock - x_0 <= latest

      if (transition.suspendable) {
        // 可挂起变迁:约束在 Z2 中
        // 设置下界约束(最早时间)
        if (transition.time_interval.earliest > 0) {
          state.Z2.set_constraint(0, clock_idx,
                                  -transition.time_interval.earliest);
        } else {
          // 如果没有最早时间约束,清除下界(设为0)
          state.Z2.set_constraint(0, clock_idx, 0);
        }
        // 设置上界约束(最晚时间)
        if (transition.time_interval.latest != matrix_ptpn::INF) {
          state.Z2.set_constraint(clock_idx, 0,
                                  transition.time_interval.latest);
        } else {
          // 如果没有最晚时间约束,设为无穷
          state.Z2.set_constraint(clock_idx, 0, INF_TIME);
        }
      } else {
        // 不可挂起变迁:约束在 Z1 中
        // 设置下界约束(最早时间)
        if (transition.time_interval.earliest > 0) {
          state.Z1.set_constraint(0, clock_idx,
                                  -transition.time_interval.earliest);
        } else {
          // 如果没有最早时间约束,清除下界(设为0)
          state.Z1.set_constraint(0, clock_idx, 0);
        }
        // 设置上界约束(最晚时间)
        if (transition.time_interval.latest != matrix_ptpn::INF) {
          state.Z1.set_constraint(clock_idx, 0,
                                  transition.time_interval.latest);
        } else {
          // 如果没有最晚时间约束,设为无穷
          state.Z1.set_constraint(clock_idx, 0, INF_TIME);
        }
      }
    }
    // 如果时钟没有被重置,保持现有约束(时间推进会更新它们)
  }

  if (initialized_count > 0) {
    BOOST_LOG_TRIVIAL(debug)
        << "    初始化了 " << initialized_count << " 个新使能变迁的时钟约束";
  }

  state.Z1.minimize();
  state.Z2.minimize();
}

bool StateClassReachabilityGraph::should_prune(
    const StateClass& state, const std::set<StateClass>& visited) const {
  if (state.Z1.is_empty() || state.Z2.is_empty()) {
    BOOST_LOG_TRIVIAL(info)
        << "    状态 " << state.state_id << " 被剪枝,因为 Z1 或 Z2 为空";
    return true;
  }

  return visited.find(state) != visited.end();
}

StateClassVertex StateClassReachabilityGraph::find_or_add_vertex(
    const StateClass& state) {
  auto it = state_to_vertex_.find(state);
  if (it != state_to_vertex_.end()) {
    return it->second;
  }

  StateClass new_state = state;
  new_state.state_id = next_state_id_++;
  StateClassVertex v = boost::add_vertex(new_state, graph_);
  state_to_vertex_[new_state] = v;
  return v;
}

bool StateClassReachabilityGraph::save_to_dot(
    const std::string& file_path) const {
  try {
    std::ofstream out(file_path);
    if (!out.is_open()) {
      return false;
    }

    out << "digraph StateClassGraph {\n";
    out << "  rankdir=LR;\n";
    out << "  node [shape=box];\n\n";

    typedef boost::graph_traits<StateClassGraph>::vertex_iterator
        StateClassVertexIterator;
    StateClassVertexIterator vi, vi_end;
    for (std::tie(vi, vi_end) = boost::vertices(graph_); vi != vi_end; ++vi) {
      const StateClass& state = boost::get(boost::vertex_name, graph_, *vi);
      out << "  s" << state.state_id << " [label=\"";
      out << "State " << state.state_id << "\\n";
      out << "M: [";
      for (size_t i = 0; i < state.marking.size(); ++i) {
        if (i > 0) out << ", ";
        out << state.marking[i];
      }
      out << "]\\n";
      out << "Time: " << state.cumulative_time;
      out << "\"];\n";
    }

    out << "\n";

    typedef boost::graph_traits<StateClassGraph>::edge_iterator
        StateClassEdgeIterator;
    StateClassEdgeIterator ei, ei_end;
    for (std::tie(ei, ei_end) = boost::edges(graph_); ei != ei_end; ++ei) {
      StateClassVertex src = boost::source(*ei, graph_);
      StateClassVertex tgt = boost::target(*ei, graph_);
      const TransitionEdge& edge = boost::get(boost::edge_name, graph_, *ei);
      const StateClass& src_state = boost::get(boost::vertex_name, graph_, src);
      const StateClass& tgt_state = boost::get(boost::vertex_name, graph_, tgt);

      out << "  s" << src_state.state_id << " -> s" << tgt_state.state_id;
      out << " [label=\"T" << edge.transition_id << "\\n@" << edge.firing_time
          << "\"];\n";
    }

    out << "}\n";
    out.close();

    return true;
  } catch (...) {
    return false;
  }
}

bool StateClassReachabilityGraph::save_to_json(
    const std::string& file_path) const {
  try {
    std::ofstream out(file_path);
    if (!out.is_open()) {
      return false;
    }

    out << "{\n";
    out << "  \"states\": [\n";

    typedef boost::graph_traits<StateClassGraph>::vertex_iterator
        StateClassVertexIterator;
    StateClassVertexIterator vi, vi_end;
    bool first_state = true;
    for (std::tie(vi, vi_end) = boost::vertices(graph_); vi != vi_end; ++vi) {
      const StateClass& state = boost::get(boost::vertex_name, graph_, *vi);
      if (!first_state) out << ",\n";
      first_state = false;

      out << "    {\n";
      out << "      \"id\": " << state.state_id << ",\n";
      out << "      \"marking\": [";
      for (size_t i = 0; i < state.marking.size(); ++i) {
        if (i > 0) out << ", ";
        out << state.marking[i];
      }
      out << "],\n";
      out << "      \"cumulative_time\": " << std::fixed << std::setprecision(2)
          << state.cumulative_time << "\n";
      out << "    }";
    }

    out << "\n  ],\n";
    out << "  \"transitions\": [\n";

    typedef boost::graph_traits<StateClassGraph>::edge_iterator
        StateClassEdgeIterator;
    StateClassEdgeIterator ei, ei_end;
    bool first_trans = true;
    for (std::tie(ei, ei_end) = boost::edges(graph_); ei != ei_end; ++ei) {
      StateClassVertex src = boost::source(*ei, graph_);
      StateClassVertex tgt = boost::target(*ei, graph_);
      const TransitionEdge& edge = boost::get(boost::edge_name, graph_, *ei);
      const StateClass& src_state = boost::get(boost::vertex_name, graph_, src);
      const StateClass& tgt_state = boost::get(boost::vertex_name, graph_, tgt);

      if (!first_trans) out << ",\n";
      first_trans = false;

      out << "    {\n";
      out << "      \"source\": " << src_state.state_id << ",\n";
      out << "      \"target\": " << tgt_state.state_id << ",\n";
      out << "      \"transition_id\": " << edge.transition_id << ",\n";
      out << "      \"firing_time\": " << std::fixed << std::setprecision(2)
          << edge.firing_time << "\n";
      out << "    }";
    }

    out << "\n  ],\n";
    out << "  \"statistics\": {\n";
    out << "    \"total_states\": " << stats_.total_states << ",\n";
    out << "    \"total_transitions\": " << stats_.total_transitions << ",\n";
    out << "    \"enabled_transitions_count\": "
        << stats_.enabled_transitions_count << ",\n";
    out << "    \"pruned_states_count\": " << stats_.pruned_states_count
        << "\n";
    out << "  }\n";
    out << "}\n";

    out.close();
    return true;
  } catch (...) {
    return false;
  }
}

std::vector<size_t> StateClassReachabilityGraph::select_per_core(
    const std::set<size_t>& enabled) const {
  // 每个核心内挑选最高优先级候选
  // SELECT_PER_CORE: 选择每个核心内最高优先级的使能变迁

  std::map<int, size_t> best_per_core;  // core -> best transition index

  for (size_t t : enabled) {
    const auto& transition = ptpn_.get_transition(t);
    int core = transition.core;
    int priority = transition.priority;

    // 如果该核心还没有候选，或者当前变迁优先级更高（数值更大）
    if (best_per_core.find(core) == best_per_core.end() ||
        priority > ptpn_.get_transition(best_per_core[core]).priority) {
      best_per_core[core] = t;
    }
  }

  std::vector<size_t> chosen;
  for (const auto& [core, trans_idx] : best_per_core) {
    chosen.push_back(trans_idx);
  }

  BOOST_LOG_TRIVIAL(debug)
      << "  每核心最高优先级候选(" << chosen.size() << "): "
      << format_transitions(std::set<size_t>(chosen.begin(), chosen.end()),
                            false);

  return chosen;
}

void StateClassReachabilityGraph::apply_preemption(
    const std::vector<size_t>& chosen, StateClass& state) const {
  // 抢占：若 chosen 引入更高优先级，则冻结同核低优先级且可挂起的时钟
  // APPLY_PREEMPTION: 应用抢占逻辑

  state.suspended.clear();

  size_t num_transitions = ptpn_.num_transitions();
  for (size_t u = 0; u < num_transitions; ++u) {
    const auto& transition_u = ptpn_.get_transition(u);

    // 只有可挂起变迁才能被挂起
    if (!transition_u.suspendable) {
      continue;
    }

    // 检查 chosen 中是否有同核且更高优先级的变迁
    for (size_t t : chosen) {
      const auto& transition_t = ptpn_.get_transition(t);

      // 同核且更高优先级（数值更大）
      if (transition_t.core == transition_u.core &&
          transition_t.priority > transition_u.priority) {
        state.suspended.insert(u);

        // 冻结时钟（从Z1移动到Z2）
        size_t clock_idx = u + 1;
        if (clock_idx < state.Z1.size() && clock_idx < state.Z2.size()) {
          state.Z1.copy_clock_constraints(clock_idx, state.Z2);
          state.Z1.freeze_clock(clock_idx);
          state.Z2.freeze_clock(clock_idx);
        }

        BOOST_LOG_TRIVIAL(debug)
            << "    " << format_transitions({u}, false) << ": 被"
            << format_transitions({t}, false) << "抢占,冻结时钟";
        break;
      }
    }
  }
}

bool StateClassReachabilityGraph::maximal_time_elapse(StateClass& state,
                                                      double& dt) const {
  // 最大化时间推进：推进到任一未冻结时钟的最小上界
  // MAXIMAL_TIME_ELAPSE: 找到所有未冻结时钟的最小上界并推进

  int ub_star = INF_TIME;

  size_t num_clocks = state.Z1.size();
  size_t num_transitions = ptpn_.num_transitions();

  // 检查 Z1 中的未冻结时钟
  for (size_t i = 1; i < num_clocks && i <= num_transitions; ++i) {
    if (state.Z1.is_frozen(i)) {
      continue;
    }

    int ub = state.Z1.get_constraint(i, 0);
    if (ub != INF_TIME && ub < ub_star) {
      ub_star = ub;
    }
  }

  // 检查 Z2 中的未冻结时钟
  num_clocks = state.Z2.size();
  for (size_t i = 1; i < num_clocks && i <= num_transitions; ++i) {
    if (state.Z2.is_frozen(i)) {
      continue;
    }

    int ub = state.Z2.get_constraint(i, 0);
    if (ub != INF_TIME && ub < ub_star) {
      ub_star = ub;
    }
  }

  if (ub_star == INF_TIME || ub_star <= 0) {
    dt = 0;
    return false;
  }

  // 推进时间
  state.Z1.elapse_time(ub_star);
  state.Z2.elapse_time(ub_star);
  state.cumulative_time += ub_star;
  dt = ub_star;

  BOOST_LOG_TRIVIAL(debug) << "  最大化时间推进: dt = " << dt;

  return true;
}

std::tuple<bool, StateClass, double> StateClassReachabilityGraph::fire_with_dbm(
    size_t trans_idx, const StateClass& from_state) {
  // 发生变迁：触发窗口一致性检查、推进到触发边界、重置时钟等
  // FIRE_WITH_DBM: 在DBM约束下触发变迁

  StateClass to = from_state.copy();
  to.state_id = next_state_id_++;  // 分配新的状态ID

  const auto& transition = ptpn_.get_transition(trans_idx);
  int alpha = transition.time_interval.earliest;
  int beta = transition.time_interval.latest == matrix_ptpn::INF
                 ? INF_TIME
                 : transition.time_interval.latest;

  // 确定使用哪个DBM
  const DBM& targetZ = transition.suspendable ? to.Z2 : to.Z1;

  // 1) 触发窗口一致性检查
  DBM zcheck = restrict_for_firing(targetZ, trans_idx);

  // 仅在启用剪枝时检查并提前返回
  if (pruning_enabled_) {
    if (zcheck.is_empty() || !zcheck.is_consistent()) {
      BOOST_LOG_TRIVIAL(debug)
          << "    " << format_transitions({trans_idx}, false)
          << ": 触发窗口检查失败";
      return {false, StateClass(), 0.0};
    }
  } else {
    // 剪枝禁用时，即使检查失败也继续处理，但记录警告
    if (zcheck.is_empty() || !zcheck.is_consistent()) {
      BOOST_LOG_TRIVIAL(debug)
          << "    " << format_transitions({trans_idx}, false)
          << ": 触发窗口检查失败,但剪枝已禁用,继续处理";
    }
  }

  // 2) 推进到触发边界
  int delta = 0;
  if (alpha == 0 && beta == 0) {
    delta = 0;  // 立即触发
  } else if (alpha == beta) {
    delta = alpha;  // 精确时间
  } else {
    delta = alpha;  // 简化：推进到最早可触发时刻
  }

  if (delta > 0) {
    to.Z1.elapse_time(delta);
    to.Z2.elapse_time(delta);
    to.cumulative_time += delta;
  }

  double fire_time = to.cumulative_time;

  // 3) 在标识上发生变迁
  to.marking = matrix_ptpn::MatrixPTPN::fire(to.marking, ptpn_, trans_idx);

  if (to.marking.empty()) {
    BOOST_LOG_TRIVIAL(debug) << "    " << format_transitions({trans_idx}, false)
                             << ": 触发后标识为空";
    return {false, StateClass(), 0.0};
  }

  // 4) 重置已发生变迁的时钟，并从挂起集中移除
  size_t clock_idx = trans_idx + 1;
  if (transition.suspendable) {
    if (clock_idx < to.Z2.size()) {
      to.Z2.reset_clock(clock_idx);
    }
    to.suspended.erase(trans_idx);
  } else {
    if (clock_idx < to.Z1.size()) {
      to.Z1.reset_clock(clock_idx);
    }
  }

  // 5) 重新计算使能与时钟窗口
  compute_enabled_and_clocks(to);

  BOOST_LOG_TRIVIAL(debug) << "    " << format_transitions({trans_idx}, false)
                           << ": 成功触发, fire_time = " << fire_time;

  return {true, to, fire_time};
}

void StateClassReachabilityGraph::compute_enabled_and_clocks(
    StateClass& state) {
  // 计算使能集合和时钟初始化
  // COMPUTE_ENABLED_AND_CLOCKS: 计算使能集合并初始化时钟约束

  size_t num_transitions = ptpn_.num_transitions();

  // 使能集合
  std::set<size_t> new_enabled;
  for (size_t t = 0; t < num_transitions; ++t) {
    if (matrix_ptpn::MatrixPTPN::is_enabled(state.marking, ptpn_, t)) {
      new_enabled.insert(t);
    }
  }

  // DBM 尺寸与参考时钟
  state.Z1.resize(num_transitions + 1);
  state.Z2.resize(num_transitions + 1);

  // 保存旧的使能集合用于比较
  std::set<size_t> old_enabled = state.enabled;

  // 先清除所有不再使能的变迁的约束(重置时钟)
  std::set<size_t> to_remove;
  for (size_t t : old_enabled) {
    if (new_enabled.find(t) == new_enabled.end()) {
      to_remove.insert(t);
    }
  }

  for (size_t t : to_remove) {
    size_t clock_idx = t + 1;
    if (clock_idx < state.Z1.size()) {
      state.Z1.reset_clock(clock_idx);
    }
    if (clock_idx < state.Z2.size()) {
      state.Z2.reset_clock(clock_idx);
    }
    state.Z1.unfreeze_clock(clock_idx);
    state.Z2.unfreeze_clock(clock_idx);
  }

  // 更新使能集合
  state.enabled = new_enabled;

  // 对每个"当前时刻已使能"的变迁 t，施加时间窗约束
  // 只为新使能的变迁设置约束（如果时钟已经存在且不是刚重置的，则保持现有约束）
  for (size_t t : state.enabled) {
    const auto& transition = ptpn_.get_transition(t);
    int alpha = transition.time_interval.earliest;
    int beta = transition.time_interval.latest == matrix_ptpn::INF
                   ? INF_TIME
                   : transition.time_interval.latest;

    size_t clock_idx = t + 1;  // +1 因为索引 0 是参考时钟 x0

    // 检查时钟是否刚刚被重置（初始状态）
    bool clock_just_reset = false;
    if (transition.suspendable) {
      if (clock_idx < state.Z2.size()) {
        int upper = state.Z2.get_constraint(clock_idx, 0);
        int lower = -state.Z2.get_constraint(0, clock_idx);
        if (upper == INF_TIME && lower == 0) {
          clock_just_reset = true;
        }
      }
    } else {
      if (clock_idx < state.Z1.size()) {
        int upper = state.Z1.get_constraint(clock_idx, 0);
        int lower = -state.Z1.get_constraint(0, clock_idx);
        if (upper == INF_TIME && lower == 0) {
          clock_just_reset = true;
        }
      }
    }

    // 如果时钟刚刚被重置或者是新使能的变迁，设置初始约束
    // 检查是否是新使能的变迁（之前不在使能集合中）
    bool is_newly_enabled = old_enabled.find(t) == old_enabled.end();

    if (!is_newly_enabled && !clock_just_reset) {
      continue;  // 保持现有约束
    }

    if (!transition.suspendable) {
      // 不可挂起时钟放入 Z1
      // x_t - x0 ≤ β
      if (beta != INF_TIME) {
        state.Z1.set_constraint(clock_idx, 0, beta);
      }
      // x0 - x_t ≤ -α  (即 x_t - x0 ≥ α)
      state.Z1.set_constraint(0, clock_idx, -alpha);
    } else {
      // 可挂起放入 Z2
      if (beta != INF_TIME) {
        state.Z2.set_constraint(clock_idx, 0, beta);
      }
      state.Z2.set_constraint(0, clock_idx, -alpha);
    }
  }

  // Floyd-Warshall 收紧
  state.Z1.minimize();
  state.Z2.minimize();

  BOOST_LOG_TRIVIAL(debug) << "    计算使能和时钟: 使能="
                           << state.enabled.size() << "个变迁";
}

}  // namespace state_class
