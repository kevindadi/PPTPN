#ifndef PPTPN_INCLUDE_PRIORITY_TIME_PETRI_NET_H
#define PPTPN_INCLUDE_PRIORITY_TIME_PETRI_NET_H

#include "clap.h"
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/graph_utility.hpp>
#include <boost/graph/graphviz.hpp>

struct Place {
  std::string name, label;
  std::string shape = "circle";
  int token = 0;
};

struct Transition {
  std::string name, label;
  std::string shape = "box";
  bool enable = false;
  bool handle = false;
  int runtime = 0;
  int priority = INT_MAX;
  std::pair<int, int> const_time = {0, 0};
};

enum PetriNetElement {
  Place,
  Transition,
};

struct PetriNetEdge {
  std::string label;
};

struct TPetriNetTransition {
  bool is_handle{false};
  int runtime{0};
  int priority{0};
  std::pair<int, int> const_time{0, 0};
  int c{0};  // 处理器资源分配
  
  TPetriNetTransition() = default;
  TPetriNetTransition(bool is_handle, int runtime, int priority,
                      std::pair<int, int> const_time, int core = 0)
      : is_handle(is_handle),
        runtime(runtime),
        priority(priority),
        const_time(std::move(const_time)),
        c(core) {}

  // 非处理变迁
  TPetriNetTransition(int runtime, int priority, std::pair<int, int> const_time,
                      int core)
      : TPetriNetTransition(false, runtime, priority, std::move(const_time), core) {}
};

struct TPetriNetElement {
  std::string name;
  std::string label;
  std::string shape;
  int token{0};
  bool enabled{false};
  TPetriNetTransition pnt;

  TPetriNetElement() = default;
  TPetriNetElement(const std::string &name, int token)
      : name(name),
        label(name),
        shape("circle"),
        token(token),
        enabled(false) {}

  TPetriNetElement(const std::string &name, bool enable, TPetriNetTransition pnt)
      : name(name),
        label(name),
        shape("box"),
        enabled(enable),
        pnt(std::move(pnt)) {}
};

struct TPetriNetEdge {
  std::string label;
  std::pair<int, int> weight;  // 发射时间区间 [min_weight, max_weight]
  int priority;                // 低层次优先级移除
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

  // 委处理变迁
  PTPNTransition(bool is_handle, int priority, std::pair<int, int> time, int c)
      : PTPNTransition(is_handle, priority, std::move(time), c, false) {}

  // 随机变迁
  PTPNTransition(int priority, std::pair<int, int> time, int c, bool is_random)
      : PTPNTransition(false, priority, std::move(time), c, is_random) {}
};

struct PTPNVertex {
  std::string name, label, shape;
  int token = 0;
  bool enabled = false;
  PTPNTransition pnt;

  PTPNVertex() = default;

  PTPNVertex(const std::string &name, int token) : name(name), token(token) {
    label = name;
    shape = "circle";
    enabled = false;
    pnt = {};
  }

  PTPNVertex(const std::string &name, PTPNTransition pnt)
      : name(name), pnt(std::move(pnt)) {
    enabled = false;
    label = name;
    shape = "box";
    token = 0;
  }
};

struct PTPNEdge {
  std::string label;
  std::pair<int, int> weight;
};

typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS,
                              PTPNVertex, PTPNEdge, TDG_RAP_P>
    PTPN;
typedef boost::graph_traits<PTPN>::vertex_descriptor vertex_ptpn;

// 深拷贝PTPN的辅助函数
inline std::unique_ptr<PTPN> deep_copy_graph(const PTPN& ptpn) {

  PTPN scg_ptpn;
  std::unordered_map<vertex_ptpn, vertex_ptpn> vertex_map;

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
  // TDG_RAP 到优先级时间 Petri 网的主函数
  void transform_tdg_to_ptpn(TDG &tdg);
  
  vertex_ptpn add_place(PTPN& pn, const string& name, int token) {
    return boost::add_vertex(PTPNVertex(name, token), pn);
  }
  
  vertex_ptpn add_transition(PTPN& pn, const string& name, PTPNTransition pnt) {
    return boost::add_vertex(PTPNVertex(name, pnt), pn);
  }

  void add_edge(vertex_ptpn u, vertex_ptpn v, PTPN& pn) {
    boost::add_edge(u, v, pn);
  }

  // 节点映射函数
  pair<vertex_ptpn, vertex_ptpn> add_node_ptpn(NodeType node_type);
  pair<vertex_ptpn, vertex_ptpn> add_ap_node_ptpn(APeriodicTask &ap_task);
  pair<vertex_ptpn, vertex_ptpn> add_p_node_ptpn(PeriodicTask &p_task);
  // 看门狗网结构
  void add_monitor_ptpn(const string &task_name, int task_period_time,
                        vertex_ptpn start, vertex_ptpn end);
  // 建立任务抢占关系
  void add_preempt_task_ptpn(
      const std::unordered_map<int, vector<string>> &core_task,
      const std::unordered_map<string, TaskConfig> &tc,
      const std::unordered_map<string, NodeType> &nodes_type);
  // 对某个任务添加抢占路径
  void create_task_priority(const std::string &name, vertex_ptpn preempt_vertex,
                            size_t handle_t, vertex_ptpn start, vertex_ptpn end,
                            NodeType task_type);
  // 对某个任务添加抢占变迁
  void create_hlf_task_priority(const std::string &name,
                                vertex_ptpn preempt_vertex, size_t handle_t,
                                vertex_ptpn h_start, vertex_ptpn l_start,
                                vertex_ptpn h_ready, int task_priority,
                                int task_core);
  // 节点命名 随机增加, != vertex_index_t
  int node_index = 0;

private:
  void transform_vertices(TDG &tdg);
  void transform_edges(TDG &tdg);
  bool is_self_loop_edge(const string& source, const string& target);
  bool is_dashed_edge(const string& edge);
  void handle_self_loop_edge(TDG &tdg, TDG_RAP::edge_descriptor e, 
                             const string& source_name);

  void handle_dashed_edge(const string& source_name, 
                           const string& target_name);
  void handle_normal_edge(const string& source_name , 
                           const string& target_name);
  void add_resources_and_bindings(TDG &tdg);
  void log_network_info();

private:// 创建处理器资源库所
  void add_cpu_resource(int nums);
  // 创建锁资源库所
  void add_lock_resource(const set<string> &locks_name);
  // 任务绑定CPU资源
  void task_bind_cpu_resource(vector<NodeType> &all_task);
  // 任务绑定锁资源
  void task_bind_lock_resource(vector<NodeType> &all_task,
                               std::map<string, vector<string>> &task_locks);
};

#endif
