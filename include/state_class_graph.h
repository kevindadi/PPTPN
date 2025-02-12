#ifndef PPTPN_INCLUDE_STATE_CLASS_GRAPH_H
#define PPTPN_INCLUDE_STATE_CLASS_GRAPH_H

#include "priority_time_petri_net.h"
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/graph_utility.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace boost;
using namespace boost::multiprecision;

namespace scg {
// 时间约束类
class TimeConstraint {
public:
  TimeConstraint(int trans_id, double min_t, double max_t)
      : transition_id(trans_id), min_time(min_t), max_time(max_t) {}

  int transition_id;
  double min_time;
  double max_time;
};

// 状态类
class State {
public:
  using Marking = std::unordered_map<int, int>; // 标识向量 place_id -> token
  using TimeConstraints = std::vector<TimeConstraint>;
  using EnabledTransitions = std::vector<int>;
  using PriorityMap = std::map<int, int>; // transition_id -> priority_level

  State(const Marking &m, const TimeConstraints &tc,
        const EnabledTransitions &et)
      : marking(m), time_constraints(tc), enabled_transitions(et) {}

  Marking marking;
  TimeConstraints time_constraints;
  EnabledTransitions enabled_transitions;
  PriorityMap priority_levels;

  // 用于状态比较和等价性判断
  bool operator==(const State &other) const {
    return marking == other.marking &&
           time_constraints == other.time_constraints;
  }

  struct Hash {
    std::size_t operator()(const State &state) const {
      std::size_t seed = 0;
      for (const auto &[place, tokens] : state.marking) {
        boost::hash_combine(seed, place);
        boost::hash_combine(seed, tokens);
      }
      for (const auto &tc : state.time_constraints) {
        boost::hash_combine(seed, tc.transition_id);
        boost::hash_combine(seed, tc.min_time);
        boost::hash_combine(seed, tc.max_time);
      }
      for (int t : state.enabled_transitions) {
        boost::hash_combine(seed, t);
      }
      return seed;
    }
  };
};

// 图结构顶点属性
struct VertexProperties {
  std::string id;
  std::shared_ptr<State> state;
  std::string label;
};

// 图结构边属性
struct EdgeProperties {
  int transition_id;
  std::string label;
};

typedef boost::property<boost::graph_name_t, std::string> graph_scg;
typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS,
                              VertexProperties, EdgeProperties, graph_scg>
    StateGraph;
using Vertex = StateGraph::vertex_descriptor;
using Edge = StateGraph::edge_descriptor;
typedef std::vector<Vertex> Path;

struct TimeInterval {
  double min;
  double max;

  TimeInterval() : min(0), max(std::numeric_limits<double>::infinity()) {}
  TimeInterval(double min, double max) : min(min), max(max) {}

  TimeInterval intersect(const TimeInterval &other) const {
    return TimeInterval(std::max(min, other.min), std::min(max, other.max));
  }

  bool is_valid() const { return min <= max; }
};

class StateClassGraph {
public:
  StateClassGraph() { graph = std::make_unique<StateGraph>(); }
  StateClassGraph(const ptpn::PriorityTPNGraph &petri_net)
      : pn_graph(petri_net) {
    graph = std::make_unique<StateGraph>();
  }
  ptpn::PriorityTPNGraph pn_graph;

  // 导出为 DOT 格式
  void export_to_dot(const std::string &filename) {
    std::ofstream dot_file(filename);
    boost::write_graphviz(
        dot_file, *graph,
        [](std::ostream &out, const VertexProperties &vp) {
          out << "[label=\"" << vp.label << "\"]";
        },
        [](std::ostream &out, const EdgeProperties &ep) {
          out << "[label=\"" << ep.label << "\"]";
        });
  }

  void generate_state_class_graph();

private:
  std::shared_ptr<State> get_vertex_state(Vertex v);
  std::shared_ptr<State>
  compute_successor_state(const State &current_state, int transition,
                          const TimeInterval &firing_interval);
  std::vector<std::pair<int, TimeInterval>> filter_by_priority(
      const std::vector<std::pair<int, TimeInterval>> &fireable_trans);
  std::vector<int> get_enabled_transitions(State &state);
  std::vector<std::pair<int, TimeInterval>>
  get_fireable_transitions(const State &state,
                           const std::vector<int> &enabled_trans);
  std::shared_ptr<State> compute_initial_state();
  bool is_transition_enabled(int transition_id,
                             const std::unordered_map<int, int> &marking);

  void update_marking(std::unordered_map<int, int> &marking, int transition);
  std::vector<TimeConstraint>
  update_time_constraints(const State &current_state, int fired_transition,
                          const TimeInterval &firing_interval,
                          const std::vector<int> &new_enabled_transitions);
  std::vector<int>
  update_enabled_transitions(const std::unordered_map<int, int> &marking);

private:
  std::unique_ptr<StateGraph> graph;
  // 添加状态类
  Vertex add_state(const std::shared_ptr<State> &state) {
    // 创建顶点属性
    VertexProperties vp;
    vp.state = state;
    vp.label = generate_state_label(state);

    // 添加顶点
    Vertex v = boost::add_vertex(vp, *graph);
    return v;
  }

  // 添加状态转换边
  Edge add_edge(Vertex source, Vertex target, int transition_id) {
    // 创建边属性
    EdgeProperties ep;
    ep.transition_id = transition_id;
    ep.label = "t" + std::to_string(transition_id);

    // 添加边
    auto [e, success] = boost::add_edge(source, target, ep, *graph);
    return e;
  }

  // 生成状态标签
  std::string generate_state_label(const std::shared_ptr<State> &state) {
    std::stringstream ss;
    ss << "M:(";
    for (size_t i = 0; i < state->marking.size(); ++i) {
      if (i > 0)
        ss << ",";
      ss << state->marking[i];
    }
    ss << ")\\n";
    // 添加时间约束信息
    ss << "TC:{";
    for (const auto &tc : state->time_constraints) {
      ss << "t" << tc.transition_id << ":[" << tc.min_time << "," << tc.max_time
         << "]";
    }
    ss << "}";
    return ss.str();
  }
};

} // namespace scg

// 可发生变迁, 从中筛选可调度变迁
struct SchedT {
  std::size_t t;
  std::pair<int, int> time;

  // Define the less-than operator for SchedT
  bool operator<(const SchedT &other) const {
    // Compare the 't' member first
    if (t < other.t)
      return true;
    if (other.t < t)
      return false;

    // If 't' is equal, compare based on 'time'
    return time < other.time;
  }
};

// 可挂起变迁的等待时间
struct T_wait {
  std::size_t t;
  int time;

  // Define the less-than operator for SchedT
  bool operator<(const T_wait &other) const {
    // Compare the 't' member first
    if (t < other.t)
      return true;
    if (other.t < t)
      return false;

    // If 't' is equal, compare based on 'time'
    return time < other.time;
  }
  // 需要在T_wait中定义operator==以便于比较
  bool operator==(const T_wait &other) const {
    return t == other.t && time == other.time;
  }

  T_wait() = default;
  T_wait(std::size_t t, int time) : t(t), time(time) {}
};

// 状态类图的节点
struct SCGVertex {
  std::string id;
  std::string label;
};

// 状态类图中的编
struct SCGEdge {
  std::string label;
  std::pair<int, int> time;
};

struct Marking {
  std::set<std::size_t> indexes;
  std::set<std::string> labels;

  bool operator==(const Marking &other) const {
    if (indexes.size() != other.indexes.size()) {
      return false;
    }
    if (indexes == other.indexes) {
      return true;
    }
    return false;
  }
  bool operator<(const Marking &other) const { return indexes < other.indexes; }

  bool operator!=(const Marking &other) const {
    return indexes != other.indexes;
  }
};
//
class StateClass {
public:
  // 当前标识
  Marking mark;
  // 使能变迁和可挂起变迁的等待时间
  std::set<T_wait> all_t;

public:
  StateClass() = default;
  StateClass(Marking mark, std::set<T_wait> all_t)
      : mark(std::move(mark)), all_t(std::move(all_t)) {}

  std::string to_scg_vertex();
  bool operator==(const StateClass &other) const {
    return mark == other.mark && all_t == other.all_t;
  }

  bool operator<(const StateClass &other) const {
    if (mark != other.mark)
      return mark < other.mark;

    // If the mark members are equal, compare the all_t member
    return all_t < other.all_t;
  };
};

namespace std {
// 为Marking定义哈希函数
template <> struct hash<Marking> {
  std::size_t operator()(const Marking &m) const {
    std::size_t hash_val = 0;
    for (const auto &index : m.indexes) {
      hash_val ^= std::hash<std::size_t>()(index);
    }
    return hash_val;
  }
};

template <> struct hash<T_wait> {
  std::size_t operator()(const T_wait &t) const {
    return std::hash<std::size_t>()(t.t) ^ std::hash<int>()(t.time);
  }
};
} // namespace std

// 为StateClass定义哈希函数
struct StateClassHasher {
  std::size_t operator()(const StateClass &k) const {
    // 计算mark的哈希值
    std::size_t mark_hash = std::hash<Marking>()(k.mark);

    // 计算all_t的哈希值
    std::size_t all_t_hash = 0;
    for (const auto &t : k.all_t) {
      all_t_hash ^= std::hash<T_wait>()(t);
    }

    return mark_hash ^ all_t_hash;
  }
};

// 为StateClass定义等价比较函数
struct StateClassEqual {
  bool operator()(const StateClass &a, const StateClass &b) const {
    return a == b;
  }
};

typedef boost::property<boost::graph_name_t, std::string> graph_scg;
typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS,
                              SCGVertex, SCGEdge, graph_scg>
    SCG;
typedef boost::graph_traits<SCG>::vertex_descriptor ScgVertexD;
typedef boost::graph_traits<SCG>::edge_descriptor ScgEdgeD;
typedef std::vector<ScgEdgeD> Path;
typedef std::unordered_map<StateClass, ScgVertexD, StateClassHasher,
                           StateClassEqual>
    ScgVertexMap;
class StateClassGraph {
private:
  // 初始网模型,用于并行加速
  ptpn::PriorityTPNGraph init_ptpn;
  // 初始标识
  Marking init_mark;
  // 初始状态类
  StateClass init_state_class;
  std::set<StateClass> sc_sets;
  ScgVertexMap scg_vertex_map;
  cpp_int global_time = 0;

private:
  void set_state_class(const StateClass &state_class);
  // 获得每个状态类下可调度的变迁集
  std::vector<SchedT> get_sched_transitions(const StateClass &state_class);
  // 发生变迁产生新的状态类
  StateClass fire_transition(const StateClass &sc, SchedT transition);

  ScgVertexD add_scg_vertex(StateClass sc);
  bool is_transition_enabled(const ptpn::PriorityTPNGraph &ptpn,
                             ptpn::ptpn_v_desc v);
  // 获取初始状态下的等待时间集合
  StateClass get_initial_state_class(const ptpn::PriorityTPNGraph &source_ptpn);
  // 获取使能的变迁
  std::vector<ptpn::ptpn_v_desc> get_enabled_transitions();
  // 计算变迁的发生时间域
  std::pair<int, int>
  calculate_fire_time_domain(const std::vector<ptpn::ptpn_v_desc> &enabled_t_s);
  // 获取符合发生时间域的变迁
  std::vector<SchedT> get_time_satisfied_transitions(
      const std::vector<ptpn::ptpn_v_desc> &enabled_t_s,
      const std::pair<int, int> &fire_time);
  // 应用优先级规则
  void apply_priority_rules(std::vector<SchedT> &sched_T);
  void apply_priority_rules(std::vector<std::size_t> &enabled_t);
  // 获取使能的变迁的前置变迁
  std::pair<std::vector<std::size_t>, std::vector<std::size_t>>
  get_enabled_transitions_with_history();
  // 更新变迁的等待时间
  void update_transition_times(const std::vector<std::size_t> &enabled_t,
                               const SchedT &transition);
  // 执行变迁
  void execute_transition(const SchedT &transition);
  // 获取新标识和新使能变迁
  std::pair<Marking, std::vector<std::size_t>> get_new_marking_and_enabled();
  // 计算新的等待时间集合
  std::set<T_wait>
  calculate_new_wait_times(const std::vector<std::size_t> &old_enabled_t,
                           const std::vector<std::size_t> &new_enabled_t);

public:
  // 构造函数
  StateClassGraph(ptpn::PriorityTPNGraph &source_ptpn) {
    init_ptpn = source_ptpn;
    init_state_class = get_initial_state_class(source_ptpn);
  }
  // 状态类图,insert唯一的stateclass
  SCG scg;

  // 生成状态类图主函数
  void generate_state_class();
};
#endif // PPTPN_INCLUDE_STATE_CLASS_GRAPH_H
