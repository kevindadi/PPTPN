//
// Created by 张凯文 on 2024/3/22.
//
#include "priority_time_petri_net.h"

vertex_ptpn add_place(PTPN& pn, const string& name, int token) {

}

void PriorityTimePetriNet::init() {
  ptpn_dp.property("node_id", get(&PTPNVertex::name, ptpn));
  ptpn_dp.property("label", get(&PTPNVertex::label, ptpn));
  ptpn_dp.property("shape", get(&PTPNVertex::shape, ptpn));
  ptpn_dp.property("label", get(&PTPNEdge::label, ptpn));

  boost::ref_property_map<PTPN *, std::string> gname_pn(
      get_property(ptpn, boost::graph_name));
  ptpn_dp.property("name", gname_pn);
}

// 转换主函数
// 首先对所有节点进行转换
// 然后绑定每个任务的 CPU
// 根据优先级,建立抢占变迁
// 绑定锁
void PriorityTimePetriNet::transform_tdg_to_ptpn(TDG& tdg) {

}

void PriorityTimePetriNet::add_lock_resource(set<string> locks_name) {
  if (locks_name.empty())
  {
    BOOST_LOG_TRIVIAL(info) << "TDG_RAP without locks!";
    return;
  }
  for (const auto& lock_name : locks_name)
  {
    PTPNVertex lock_place = {lock_name, 1};
    vertex_ptpn l = add_vertex(lock_place, ptpn);
    locks_place.insert(make_pair(lock_name, l));
  }
  BOOST_LOG_TRIVIAL(info) << "create lock resource!";
}

void PriorityTimePetriNet::add_cpu_resource(int nums) {
  for (int i = 0; i < nums; i++)
  {
    string cpu_name = "core" + std::to_string(i);
    // vertex_tpn c = add_vertex(PetriNetElement{core_name, core_name, "circle",
    // 1}, time_petri_net);
    PTPNVertex cpu_place = {cpu_name, 1};
    vertex_ptpn c = add_vertex(cpu_place, ptpn);
    cpus_place.push_back(c);
  }
  BOOST_LOG_TRIVIAL(info) << "create core resource!";
}

// 根据任务类型绑定不同位置的 CPU
void PriorityTimePetriNet::task_bind_cpu_resource(vector<NodeType> &all_task) {
  for (const auto& task : all_task) {
    if (holds_alternative<APeriodicTask>(task)) {
      auto ap_task = get<APeriodicTask>(task);
      int cpu_index = ap_task.core;
      auto task_pt_chains = node_pn_map.find(ap_task.name)->second;
      // Random -> Trigger -> Start -> Get CPU -> Run -> Drop CPU -> End
      add_edge(task_pt_chains[3], cpus_place[cpu_index], ptpn);
      add_edge(cpus_place[cpu_index], task_pt_chains[task_pt_chains.size() - 2], ptpn);
    }else if (holds_alternative<PeriodicTask>(task)) {
      auto p_task = get<PeriodicTask>(task);
      int cpu_index = p_task.core;
      auto task_pt_chains = node_pn_map.find(p_task.name)->second;
      // Start -> Get CPU -> Run -> Drop CPU -> End
      add_edge(task_pt_chains[1], cpus_place[cpu_index], ptpn);
      add_edge(cpus_place[cpu_index], task_pt_chains[task_pt_chains.size() - 2], ptpn);
    } else {
      continue;
    }
  }
}

// 根据任务种锁的数量和类型绑定

