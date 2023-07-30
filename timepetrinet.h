//
// 时间 Petri 网
// Created by 张凯文 on 2023/6/26.
//

#ifndef PPTPN__TIMEPETRINET_H_
#define PPTPN__TIMEPETRINET_H_
#include "petrinet.h"
#include "StateClass.h"
#include <boost/chrono.hpp>
#include <boost/chrono/include.hpp>
#include <utility>

class TimePetriNet : public PetriNet{
 public:
  typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS,
      TPetriNetElement, TPetriNetEdge, graph_p> TPN;
  typedef boost::graph_traits<TPN>::vertex_descriptor vertex_tpn;

  void init_graph();
  virtual void construct_petri_net(const Config& config) override;
 private:
  boost::dynamic_properties tpn_dp;
  TPN time_petri_net;

  // element计数,保持唯一性
  int element_id = 0;
  // resource Vertex(core)
  std::vector<vertex_tpn> core_vertex;
  std::unordered_map<std::string, vertex_tpn> lock_vertex;

  std::unordered_map<std::size_t, boost::tuple<vertex_tpn, vertex_tpn>> id_start_end;
  std::unordered_map<std::string, std::pair<vertex_tpn, vertex_tpn>> id_str_start_end;
  std::unordered_map<std::size_t, std::string> id_name_map_me;
  std::unordered_map<std::string, std::vector<vertex_tpn>> single_task_node;
  std::unordered_map<std::string, std::vector<std::vector<vertex_tpn>>> multi_task_node;
  // DAG 图中任务的后继
  std::unordered_map<std::size_t, vertex_tpn> task_succ;
 private:
  vertex_tpn add_place(TPN& time_petri_net, std::string name, int token);
  vertex_tpn add_transition(TPN& time_petri_net, std::string name, bool enable, TPetriNetTransition tpnt);

  // 从配置文件中收集 task
  std::vector<std::pair<std::size_t, std::string>> task_name;
  bool collect_task(const Config& config);

  // 创建资源
  bool create_core_vertex(int num);
  bool create_lock_vertex(const Config& config);
  // 绑定资源
  void bind_task_core(const Config& config);
  void task_bind_lock(const Config& config);
  // 创建优先级
  void bind_task_priority(Config& config);
  // 为高优先级创建抢占序列
  void create_priority_task(const Config& config,
                            const std::string& name,
                            vertex_tpn preempt_node,
                            int handle_t,
                            vertex_tpn start,
                            vertex_tpn end);

  public:
    typename boost::property_map<TPN, boost::vertex_index_t>::type index
        = get(boost::vertex_index, time_petri_net);
    StateClass initial_state_class;
    StateClass get_initial_state_class();
    std::set<StateClass> scg;
    // 重新初始化Petri网
    void set_state_class(const StateClass& state_class);
    void generate_state_class();
    std::vector<SchedT> get_sched_t(StateClass& state);
    StateClass fire_transition(const StateClass& sc, SchedT transition);

  // 成员函数用于保存数据到文件
  void saveDataToFile(const std::string& filename) {
    std::ofstream outputFile(filename);
    if (outputFile.is_open()) {
      for (const auto& value : scg) {
        outputFile << "scg mark: ";
        for (const auto& m : value.mark.labels) {
          outputFile << m << " ";
        }
        std::cout << std::endl;
        for (const auto t : value.all_t) {
          outputFile << t.t << " " << t.time <<" ";
        }
        std::cout << std::endl;
      }
      outputFile.close();
    } else {
      std::cout << "Error: Unable to open file." << std::endl;
    }
  }
};

#endif //PPTPN__TIMEPETRINET_H_
