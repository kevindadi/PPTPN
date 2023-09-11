//
// 概率 Petri 网
// Created by 张凯文 on 2023/6/26.
//

#ifndef PPTPN__PROBPETRINET_H_
#define PPTPN__PROBPETRINET_H_
#include "GConfig.h"
#include "StateClass.h"
#include "petrinet.h"
#include <boost/chrono.hpp>
#include <boost/chrono/include.hpp>
typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS,
                              PTPNVertex, PTPNEdge, graph_p>
    PTPN;
typedef boost::graph_traits<PTPN>::vertex_descriptor vertex_ptpn;

class ProbPetriNet : public PetriNet
{
public:
  boost::dynamic_properties ptpn_dp;
  PTPN ptpn;
  void init();
  void collect_task(GConfig &config);
  int element_id = 0;
  std::vector<vertex_ptpn> core_vertex;
  std::unordered_map<std::string, vertex_ptpn> lock_vertex;

  std::unordered_map<std::string, std::vector<vertex_ptpn>> task_vertexes;
  std::unordered_map<std::string, std::vector<std::vector<vertex_ptpn>>>
      preempt_task_vertexes;

  // DAG 任务计时器的开始和结束
  std::unordered_map<std::string, std::vector<vertex_ptpn>> sub_task_timer;
  void create_core_vertex(int num);
  void bind_task_core(const GConfig &config);
  void create_lock_vertex(const GConfig &config);
  void task_bind_lock(const GConfig &config);
  void bind_task_priority(GConfig &config);
  void create_priority_task(const GConfig &config, const std::string &name,
                            vertex_ptpn preempt_vertex, size_t handle_t,
                            vertex_ptpn start, vertex_ptpn end);
  void construct_sub_ptpn(GConfig &config);
  void construct_glb_ptpn(GConfig &config);
  boost::chrono::time_point<boost::chrono::steady_clock> pt_a;

public:
  // State Class Graph
  typename boost::property_map<PTPN, boost::vertex_index_t>::type index =
      get(boost::vertex_index, ptpn);
  StateClassGraph state_class_graph;
  StateClass initial_state_class;
  StateClass get_initial_state_class();
  std::set<StateClass> scg;
  // 重新初始化Petri网
  void set_state_class(const StateClass &state_class);
  void generate_state_class();
  std::vector<SchedT> get_sched_t(StateClass &state);
  StateClass fire_transition(const StateClass &sc, SchedT transition);
};

#endif // PPTPN__PROBPETRINET_H_
