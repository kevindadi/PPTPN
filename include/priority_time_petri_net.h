#ifndef PPTPN_INCLUDE_PRIORITY_TIME_PETRI_NET_H
#define PPTPN_INCLUDE_PRIORITY_TIME_PETRI_NET_H

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/graph_utility.hpp>
#include <boost/graph/graphviz.hpp>
#include <variant>

#include "clap.h"

namespace ptpn {
struct Place {
  int token = 0;
  int capacity = 1;
};

struct Transition {
  bool enable = false;
  bool handle = false;
  int priority = INT_MAX;
  int core = 0;
  int runtime = 0;
  std::pair<int, int> runtimes = {0, 0};
  std::pair<int, int> const_time = {0, 0};
};

enum class VertexType {
  Place,
  Transition,
};

enum VertexShape { Circle, Box };

struct Edge {
  std::string label;
  int weight = 1;
};


struct PTPNTransition {
  bool is_handle{false};
  bool is_random{false};
  int runtime{0};
  int priority{0};
  std::pair<int, int> const_time{0, 0};
  int c{0};  // 处理器资源分配

  PTPNTransition() = default;

  // 主构造函数
  PTPNTransition(bool is_handle, int priority, std::pair<int, int> time, int c,
                 bool is_random, int runtime = 0)
      : is_handle(is_handle),
        is_random(is_random),
        runtime(runtime),
        priority(priority),
        const_time(std::move(time)),
        c(c) {}

  // 基本变迁
  PTPNTransition(int priority, std::pair<int, int> time, int c)
      : PTPNTransition(false, priority, std::move(time), c, false) {}

  PTPNTransition(bool is_handle, int priority, std::pair<int, int> time, int c)
      : PTPNTransition(is_handle, priority, std::move(time), c, false) {}

  PTPNTransition(int priority, std::pair<int, int> time, int c, bool is_random)
      : PTPNTransition(false, priority, std::move(time), c, is_random) {}
};

struct PTPNVertex {
  std::string name, label, shape;
  int token = 0;
  bool enabled = false;
  PTPNTransition pnt;

  PTPNVertex() = default;


  // 类型判别
  bool is_place() const noexcept { return std::holds_alternative<Place>(node); }
  bool is_transition() const noexcept {
    return std::holds_alternative<Transition>(node);
  }

  // 类型转换
  Place &as_place() { return std::get<Place>(node); }
  Transition &as_transition() { return std::get<Transition>(node); }
  const Place &as_place() const { return std::get<Place>(node); }
  const Transition &as_transition() const { return std::get<Transition>(node); }

  template <typename T> class VertexAccessor {
  public:
    explicit VertexAccessor(Vertex &vertex) : vertex_(vertex) {}

    template <typename F> VertexAccessor &apply(F &&func) {
      if (std::holds_alternative<T>(vertex_.node)) {
        func(std::get<T>(vertex_.node));
      }
      return *this;
    }

  private:
    Vertex &vertex_;
  };

  auto as_place_accessor() { return VertexAccessor<Place>(*this); }
  auto as_transition_accessor() { return VertexAccessor<Transition>(*this); }

  inline VertexShape get_shape() const {
    return std::holds_alternative<Place>(node) ? VertexShape::Circle
                                               : VertexShape::Box;
  }
};

struct PTPNEdge {
  std::string label;
  int weight;
  std::pair<int, int> times;

  PTPNEdge() = default;
  PTPNEdge(const string& label, int weight, std::pair<int, int> times) : label(label), weight(weight), times(times) {}

};

class PriorityTPN {
private:
  PriorityTPNGraph graph;
  boost::dynamic_properties graph_dp;

public:
  PriorityTPN() = default;
  PriorityTPN(const PriorityTPN &) = default;
  PriorityTPN(PriorityTPN &&) = default;

  explicit PriorityTPN(const PriorityTPNGraph &g) : graph(g) {}
  explicit PriorityTPN(PriorityTPNGraph &&g) : graph(std::move(g)) {}


  for (auto [vi, vi_end] = vertices(ptpn); vi != vi_end; ++vi) {
    vertex_ptpn new_vertex = add_vertex(ptpn[*vi], scg_ptpn);
    vertex_map[*vi] = new_vertex;
  }
  
  for (auto [ei, ei_end] = edges(ptpn); ei != ei_end; ++ei) {
     add_edge(vertex_map[source(*ei, ptpn)],
            vertex_map[target(*ei, ptpn)],
            ptpn[*ei],
            scg_ptpn);
  }
    
  return std::make_unique<PTPN>(scg_ptpn);
} 
class PriorityTimePetriNet {
  // cpu 对应的库所
  vector<vertex_ptpn> cpus_place;
  // 锁对应的库所
  std::unordered_map<string, vertex_ptpn> locks_place;
  // 每个节点对应的原型 Petri 网结构
  std::unordered_map<string, vector<vertex_ptpn>> node_pn_map;
  // 每个任务的开始和结束库所
  std::map<string, pair<vertex_ptpn, vertex_ptpn>> node_start_end_map;
  // 任务节点对应的优先级结构
  std::unordered_map<string, vector<vector<vertex_ptpn>>> task_pn_map;

public: // 图映射
  // 初始化 Petri 网结构，决定网的表示形式
  void init();
  boost::dynamic_properties ptpn_dp;
  PTPN ptpn;
  // 周期函数的周期变迁ID
  std::vector<std::size_t> period_transitions_id;


public:

  // TDG_RAP 到优先级时间 Petri 网的主函数
  void transform_tdg_to_ptpn(TDG &tdg);
  // 验证Petri结构正确性
  bool verify_petri_net_structure();
  std::string save_ptpn_and_dot(const std::string &file_path);


  void add_edge(vertex_ptpn u, vertex_ptpn v, PTPN& pn, const string& label, int weight, std::pair<int, int> times) {
    boost::add_edge(u, v, PTPNEdge(label, weight, times), pn);
  }

  void add_edge(vertex_ptpn u, vertex_ptpn v, PTPN& pn) {
    boost::add_edge(u, v, pn);
  }
private:
  // cpu 对应的库所
  vector<ptpn_v_desc> cpus_place;
  // 锁对应的库所
  std::unordered_map<string, ptpn_v_desc> locks_place;
  // 每个节点对应的原型 Petri 网结构
  std::unordered_map<string, vector<ptpn_v_desc>> node_pn_map;
  // 每个任务的开始和结束库所
  std::map<string, pair<ptpn_v_desc, ptpn_v_desc>> node_start_end_map;
  // 任务节点对应的优先级结构
  std::unordered_map<string, vector<vector<ptpn_v_desc>>> task_pn_map;


private:
  void transform_vertices(TDG &tdg);
  // 节点映射函数
  std::pair<ptpn_v_desc, ptpn_v_desc> add_node_ptpn(NodeType node_type);
  std::pair<ptpn_v_desc, ptpn_v_desc> add_ap_node_ptpn(APeriodicTask &ap_task);
  std::pair<ptpn_v_desc, ptpn_v_desc> add_p_node_ptpn(PeriodicTask &p_task);
  // 看门狗网结构
  void add_monitor_ptpn(const string &task_name, int task_period_time,
                        ptpn_v_desc start, ptpn_v_desc end);
  // 建立任务抢占关系
  void add_preempt_task_ptpn(
      const std::unordered_map<int, vector<string>> &core_task,
      const std::unordered_map<string, TaskConfig> &tc,
      const std::unordered_map<string, NodeType> &nodes_type);
  // 对某个任务添加抢占路径
  void create_task_priority(const std::string &name, ptpn_v_desc preempt_vertex,
                            size_t handle_t, ptpn_v_desc start, ptpn_v_desc end,
                            NodeType task_type);
  // 对某个任务添加抢占变迁
  void create_hlf_task_priority(const std::string &name,
                                ptpn_v_desc preempt_vertex, size_t handle_t,
                                ptpn_v_desc h_start, ptpn_v_desc l_start,
                                ptpn_v_desc h_ready, int task_priority,
                                int task_core);
  // 节点命名 随机增加, != vertex_index_t
  int node_index = 0;

  void transform_edges(TDG &tdg);
  bool is_self_loop_edge(const string &source, const string &target);
  bool is_dashed_edge(const string &edge);
  void handle_self_loop_edge(TDG &tdg, TDG_RAP::edge_descriptor e,
                             const string &source_name);

  void handle_dashed_edge(const string &source_name, const string &target_name);
  void handle_normal_edge(const string &source_name, const string &target_name);

  ptpn_v_desc handle_locks(const TaskVertexsNames &names,
                           vector<ptpn_v_desc> &chain,
                           const vector<string> &locks,
                           const vector<pair<int, int>> &times, int priority,
                           int core);
  void add_resources_and_bindings(TDG &tdg);
  void log_network_info();

private:
  bool is_safe_net = true;

  void add_cpu_resource(int nums);
  void add_cpu_resource(int counts, int cores);
  // 创建锁资源库所
  void add_lock_resource(const set<string> &locks_name);
  // 任务绑定CPU资源
  void task_bind_cpu_resource(vector<NodeType> &all_task);
  // 任务绑定锁资源
  void task_bind_lock_resource(vector<NodeType> &all_task,
                               std::map<string, vector<string>> &task_locks);
};

ptpn_v_desc add_place(PriorityTPNGraph &graph, const std::string &name,
                      int token, int capacity);
ptpn_v_desc add_transition(PriorityTPNGraph &graph, const std::string &name,
                           int priority, int core,
                           std::pair<int, int> const_time, bool is_handle,
                           std::pair<int, int> runtimes, int runtime);

// 创建基本任务结构的辅助函数
struct BasicTaskChains {
  ptpn_v_desc entry, get_core, ready, exec, exit;
  vector<ptpn_v_desc> task_pt_chain;

  BasicTaskChains(PriorityTPNGraph &graph, const TaskVertexsNames &names,
                  int priority, int core, const pair<int, int> &exec_time) {
    entry = add_place(graph, names.entry, 0, 1);
    get_core = add_transition(graph, names.get_core, priority, core, {0, 0},
                              false, {0, 0}, 0);
    ready = add_place(graph, names.ready, 0, 1);
    exec = add_transition(graph, names.exec, priority, core, exec_time, false,
                          {0, 0}, 0);
    exit = add_place(graph, names.exit, 0, 1);

    add_edge(entry, get_core, graph);
    add_edge(get_core, ready, graph);
    add_edge(exec, exit, graph);

    task_pt_chain = {entry, get_core, ready};
  }
};

struct NewTaskChains {};
} // namespace ptpn

#endif
