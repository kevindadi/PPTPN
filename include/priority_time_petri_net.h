//
// Created by 张凯文 on 2024/3/22.
//

#ifndef PPTPN_INCLUDE_PRIORITY_TIME_PETRI_NET_H
#define PPTPN_INCLUDE_PRIORITY_TIME_PETRI_NET_H

#include "clap.h"
#include "state_class_graph.h"

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

struct PTPNVertex {
  std::string name, label, shape;
  int token = 0;
  bool enabled = false;
  PTPNTransition pnt;

  PTPNVertex() = default;

  PTPNVertex(const std::string &name, int token) : name(name), token(token)
  {
    label = name;
    shape = "circle";
    enabled = false;
    pnt = {};
  }

  PTPNVertex(const std::string &name, PTPNTransition pnt)
      : name(name), pnt(std::move(pnt))
  {
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

class PriorityTimePetriNet {
  boost::dynamic_properties ptpn_dp;
  PTPN ptpn;

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

  // TDG_RAP 到优先级时间 Petri 网的主函数
  void transform_tdg_to_ptpn(TDG& tdg);
  // 创建处理器资源库所
  void add_cpu_resource(int nums);
  // 创建锁资源库所
  void add_lock_resource(const set<string>& locks_name);
  // 任务绑定CPU资源
  void task_bind_cpu_resource(vector<NodeType> &all_task);
  // 任务绑定锁资源
  void task_bind_lock_resource(vector<NodeType> &all_task, std::map<string, vector<string>> &task_locks);

  // 节点映射函数
  pair<vertex_ptpn, vertex_ptpn> add_node_ptpn(NodeType node_type);
  pair<vertex_ptpn, vertex_ptpn> add_ap_node_ptpn(APeriodicTask &ap_task);
  pair<vertex_ptpn, vertex_ptpn> add_p_node_ptpn(PeriodicTask &p_task);
  // 看门狗网结构
  void add_monitor_ptpn(vertex_ptpn start, vertex_ptpn end);
  // 建立任务抢占关系
  void add_preempt_task_ptpn();
  // 节点命名 随机增加, != vertex_index_t
  int node_index = 0;
public: // 状态图生成


public: // 性能分析和死锁检测

};


#endif //PPTPN_INCLUDE_PRIORITY_TIME_PETRI_NET_H
