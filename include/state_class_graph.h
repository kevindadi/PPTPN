#ifndef PPTPN_INCLUDE_STATE_CLASS_GRAPH_H
#define PPTPN_INCLUDE_STATE_CLASS_GRAPH_H

#include <algorithm>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/graph_utility.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <string>
#include <thread>
#include <mutex>
#include <future>
#include <queue>
#include <unordered_map>
#include <utility>
#include "priority_time_petri_net.h"

using namespace boost;



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

  bool operator!=(const Marking& other) const {
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
    if (mark != other.mark) return mark < other.mark;

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
  std::unique_ptr<PTPN> init_ptpn;
  // 初始标识
  Marking init_mark;
  // 初始状态类
  StateClass init_state_class;
  std::set<StateClass> sc_sets;
  ScgVertexMap scg_vertex_map;
private:
  void set_state_class(const StateClass &state_class);
  // 获得每个状态类下可调度的变迁集
  std::vector<SchedT> get_sched_transitions(const StateClass &state_class);
  // 发生变迁产生新的状态类
  StateClass fire_transition(const StateClass &sc, SchedT transition);

  ScgVertexD add_scg_vertex(StateClass sc);
  bool is_transition_enabled(const PTPN& ptpn, vertex_ptpn v);
  // 获取初始状态下的等待时间集合
  StateClass get_initial_state_class(const PTPN& source_ptpn);
  // 获取使能的变迁
  std::vector<vertex_ptpn> get_enabled_transitions();
  // 计算变迁的发生时间域
  std::pair<int, int> calculate_fire_time_domain(const std::vector<vertex_ptpn>& enabled_t_s);
  // 获取符合发生时间域的变迁
  std::vector<SchedT> get_time_satisfied_transitions(const std::vector<vertex_ptpn>& enabled_t_s, const std::pair<int, int>& fire_time);  
  // 应用优先级规则
  void apply_priority_rules(std::vector<SchedT>& sched_T);
  void apply_priority_rules(std::vector<std::size_t>& enabled_t);
  // 获取使能的变迁的前置变迁
  std::pair<std::vector<std::size_t>, std::vector<std::size_t>> get_enabled_transitions_with_history();
  // 更新变迁的等待时间
  void update_transition_times(const std::vector<std::size_t>& enabled_t, 
                           const SchedT& transition);  
  // 执行变迁
  void execute_transition(const SchedT& transition);
  // 获取新标识和新使能变迁
  std::pair<Marking, std::vector<std::size_t>> get_new_marking_and_enabled();
  // 计算新的等待时间集合
  std::set<T_wait> calculate_new_wait_times(const std::vector<std::size_t>& old_enabled_t, const std::vector<std::size_t>& new_enabled_t);

public:
  // 构造函数
  StateClassGraph(const PTPN& source_ptpn) 
  : init_ptpn(deep_copy_graph(source_ptpn)) {
      init_state_class = get_initial_state_class(source_ptpn);
  }
  // 状态类图,insert唯一的stateclass
  SCG scg; 

  // 生成状态类图主函数
  void generate_state_class();
  void generate_state_class_with_thread(int num_threads = std::thread::hardware_concurrency());

private:
  std::mutex scg_mutex;  // 保护状态类图的互斥锁
  std::mutex queue_mutex; // 保护待处理队列的互斥锁
  std::condition_variable cv; // 条件变量用于线程同步
  std::queue<StateClass> pending_states; // 待处理的状态类队列
  bool processing_complete = false; // 处理完成标志
  std::vector<std::unique_ptr<PTPN>> thread_ptpns;
  // 新增的私有方法
  void worker_thread(int thread_id);
  void process_state_class(const StateClass& state_class, std::unique_ptr<PTPN>& local_ptpn);
  bool get_next_state(StateClass& state);
  void add_new_state(const StateClass& new_state);
};
#endif // PPTPN_INCLUDE_STATE_CLASS_GRAPH_H

