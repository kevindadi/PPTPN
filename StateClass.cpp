//
// Created by Kevin on 2023/7/28.
//
#include "StateClass.h"
#include <iostream>

void StateClass::print_current_mark(){
  for(const auto& l : mark.labels) {
    std::cout << l << " ";
  }
  std::cout << std::endl;
}

bool StateClass::operator==(const StateClass &other) {
  if (mark == other.mark) {
    std::set<std::size_t> h_diff, H_diff;
    std::set_symmetric_difference(t_sched.begin(), t_sched.end(),
                                  other.t_sched.begin(), other.t_sched.end(),
                                  std::inserter(h_diff, h_diff.begin()));
    std::set_symmetric_difference(handle_t_sched.begin(), handle_t_sched.end(),
                                  other.handle_t_sched.begin(), other.handle_t_sched.end(),
                                  std::inserter(H_diff, H_diff.begin()));

    if (H_diff.empty() && h_diff.empty()) {
      return true;
    } else {
      return false;
    }
  }else {
    return false;
  }
}
void StateClass::print_current_state() {
  std::cout << "current mark: ";
  for (const auto& l : mark.labels) {
    std::cout << l << " ";
  }
  std::cout << std::endl;
  std::cout << "current time domain: ";
  for (const auto& t : all_t) {
    std::cout << t.t << ": " << t.time << ";";
  }
  std::cout << std::endl;
  std::cout << "-------------------------" << std::endl;
}

std::string StateClass::to_scg_vertex() {
  std::string labels;
  std::string times;
  for (const auto& l : mark.labels) {
    labels.append(l);
  }
  for (const auto& t : all_t) {
    times.append(std::to_string(t.t))
         .append(":")
         .append(std::to_string(t.time)
         .append(";"));
  }
  return labels + times;
}

void StateClassGraph::write_to_dot(const std::string& scg_path) {
  boost::dynamic_properties dp_scg;
  boost::ref_property_map<SCG *, std::string> gname_scg(get_property(scg, boost::graph_name));
  dp_scg.property("name", gname_scg);
  dp_scg.property("node_id", get(&SCGVertex::id, scg));
  dp_scg.property("label", get(&SCGVertex::label, scg));
  dp_scg.property("label", get(&SCGEdge::label, scg));

  std::ofstream scg_f(scg_path);
  write_graphviz_dp(scg_f, scg, dp_scg);
}

ScgVertexD StateClassGraph::add_scg_vertex(StateClass sc) {
  ScgVertexD svd;
  if (scg_vertex_map.find(sc) != scg_vertex_map.end()) {
    svd = scg_vertex_map.find(sc)->second;
  } else {
    svd = boost::add_vertex(SCGVertex{sc.to_scg_vertex()}, scg);
    scg_vertex_map.insert(std::make_pair(sc, svd));
  }
  return svd;
}
