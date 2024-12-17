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

// 状态类图中的边，包含使能的变迁
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
  // 周期变迁 Names和Id
  std::vector<std::string> period_transitions;
  std::vector<std::size_t> period_transitions_id;
  // 构造函数
  StateClassGraph(const PTPN& source_ptpn, std::vector<std::size_t> period_transitions_id) 
  : init_ptpn(deep_copy_graph(source_ptpn)), period_transitions_id(period_transitions_id) {
      init_state_class = get_initial_state_class(source_ptpn);
  }
  // 状态类图,insert唯一的stateclass
  SCG scg; 

  // 生成状态类图主函数
  void generate_state_class();
  void generate_state_class_with_thread(int num_threads = std::thread::hardware_concurrency());

  // 死锁StateClass
  std::set<StateClass> deadlock_sc_sets;

private:
  std::mutex scg_mutex;  // 保护图结构的互斥锁
  std::mutex queue_mutex;  // 保护队列的互斥锁
  std::condition_variable cv;
  std::atomic<int> active_threads{0};
  std::atomic<bool> processing_complete{false};
  std::vector<std::unique_ptr<PTPN>> thread_ptpns;
  std::queue<StateClass> pending_states;
  // 并发generate实现
  std::vector<SchedT> get_sched_transitions(const StateClass &state_class, PTPN& local_ptpn);
  StateClass fire_transition(const StateClass &sc, SchedT transition, PTPN& local_ptpn);
  void process_state_class(const StateClass& state_class, PTPN& local_ptpn);
  void worker_thread(int thread_id);
  bool get_next_state(StateClass& states);
  void add_new_state(const StateClass& new_state);

  void set_state_class(const StateClass &state_class, PTPN& local_ptpn);
  std::vector<vertex_ptpn> get_enabled_transitions(PTPN& local_ptpn);
  std::pair<std::vector<std::size_t>, std::vector<std::size_t>> 
  get_enabled_transitions_with_history(PTPN& local_ptpn);
  std::pair<int, int> calculate_fire_time_domain(
      const std::vector<vertex_ptpn>& enabled_t_s, 
      PTPN& local_ptpn);
  void execute_transition(const SchedT& transition, PTPN& local_ptpn);
  std::pair<Marking, std::vector<std::size_t>> 
  get_new_marking_and_enabled(PTPN& local_ptpn);
  std::set<T_wait> calculate_new_wait_times(
      const std::vector<std::size_t>& old_enabled_t,
      const std::vector<std::size_t>& new_enabled_t,
      PTPN& local_ptpn);
  void apply_priority_rules(std::vector<SchedT>& sched_T, PTPN& local_ptpn);
  void apply_priority_rules(std::vector<std::size_t>& enabled_t, PTPN& local_ptpn);
  void update_transition_times(const std::vector<std::size_t>& enabled_t, 
                            const SchedT& transition, PTPN& local_ptpn);
  std::vector<SchedT> get_time_satisfied_transitions(   
      const std::vector<vertex_ptpn>& enabled_t_s,
      const std::pair<int, int>& fire_time, PTPN& local_ptpn);  
};


class WorkQueue {
private:
    std::deque<StateClass> queue;
    std::mutex mutex;
    
public:
    bool try_steal(StateClass& state) {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return false;
        state = std::move(queue.back());
        queue.pop_back();
        return true;
    }
    
    void push(StateClass state) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push_front(std::move(state));
    }
    
    bool try_pop(StateClass& state) {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return false;
        state = std::move(queue.front());
        queue.pop_front();
        return true;
    }
};

class WorkStealing {
private:
    std::vector<WorkQueue> queues;
    std::atomic<bool> done{false};
    
public:
    explicit WorkStealing(size_t n) : queues(n) {}
    
    void push(size_t id, StateClass state) {
        queues[id].push(std::move(state));
    }
    
    bool try_pop(size_t id, StateClass& state) {
        // 先尝试从自己的队列中获取任务
        if (queues[id].try_pop(state)) return true;
        
        // 否则尝试从其他队列偷取任务
        for (size_t i = 0; i < queues.size(); ++i) {
            if (i == id) continue;
            if (queues[i].try_steal(state)) return true;
        }
        return false;
    }
    
    void finish() { done = true; }
    bool is_done() const { return done; }
};

template<typename T>
class ObjectPool {
    std::vector<std::unique_ptr<T>> objects;
    std::vector<T*> free_objects;
    std::mutex mutex;
    
public:
    T* acquire() {
        std::lock_guard<std::mutex> lock(mutex);
        if (free_objects.empty()) {
            objects.push_back(std::make_unique<T>());
            return objects.back().get();
        }
        T* obj = free_objects.back();
        free_objects.pop_back();
        return obj;
    }
    
    void release(T* obj) {
        std::lock_guard<std::mutex> lock(mutex);
        free_objects.push_back(obj);
    }
};
#endif // PPTPN_INCLUDE_STATE_CLASS_GRAPH_H
