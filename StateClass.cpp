//
// Created by Kevin on 2023/7/28.
//
#include "StateClass.h"
#include <iostream>

void StateClass::print_current_mark() {
  for (const auto &l : mark.labels) {
    std::cout << l << " ";
  }
  std::cout << std::endl;
}

bool StateClass::operator==(const StateClass &other) {
  if (mark == other.mark) {
    //    std::set<std::size_t> h_diff, H_diff;
    //    std::set_symmetric_difference(t_sched.begin(), t_sched.end(),
    //                                  other.t_sched.begin(),
    //                                  other.t_sched.end(),
    //                                  std::inserter(h_diff, h_diff.begin()));
    //    std::set_symmetric_difference(handle_t_sched.begin(),
    //    handle_t_sched.end(),
    //                                  other.handle_t_sched.begin(),
    //                                  other.handle_t_sched.end(),
    //                                  std::inserter(H_diff, H_diff.begin()));
    //
    //    if (H_diff.empty() && h_diff.empty()) {
    //      return true;
    //    } else {
    //      return false;
    //    }
    if (all_t == other.all_t) {
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}
void StateClass::print_current_state() {
  std::cout << "current mark: ";
  for (const auto &l : mark.labels) {
    std::cout << l << " ";
  }
  std::cout << std::endl;
  std::cout << "current time domain: ";
  for (const auto &t : all_t) {
    std::cout << t.t << ": " << t.time << ";";
  }
  std::cout << std::endl;
  std::cout << "-------------------------" << std::endl;
}

std::string StateClass::to_scg_vertex() {
  std::string labels;
  std::string times;
  for (const auto &l : mark.labels) {
    labels.append(l);
  }
  for (const auto &t : all_t) {
    times.append(std::to_string(t.t))
        .append(":")
        .append(std::to_string(t.time).append(";"));
  }
  return labels + times;
}

void StateClassGraph::write_to_dot(const std::string &scg_path) {
  boost::dynamic_properties dp_scg;
  boost::ref_property_map<SCG *, std::string> gname_scg(
      get_property(scg, boost::graph_name));
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
void StateClassGraph::dfs_all_path(ScgVertexD start, ScgVertexD end,
                                   std::vector<Path> &all_path,
                                   Path &current_path,
                                   std::vector<bool> &visited,
                                   std::string &exit_flag) {
  visited[start] = true;
  current_path.push_back(ScgEdgeD());
  // std::cout << "dfs all path" << std::endl;
  //  Check if the current vertex is the goal
  if (start == end || (scg[start].label.find(exit_flag) != std::string::npos)) {
    // Add the current path to the vector of all paths
    // std::cout << "find one path" << std::endl;
    all_path.push_back(current_path);
  } else {
    // Recursively explore the neighbors of the current vertex
    boost::graph_traits<SCG>::out_edge_iterator ei, ei_end;
    // std::cout << "find target vertex" << std::endl;
    for (tie(ei, ei_end) = out_edges(start, scg); ei != ei_end; ++ei) {
      ScgVertexD next = target(*ei, scg);

      if (!visited[next]) {
        //                std::cout << "add next:" << g[next].name << std::endl;
        current_path.back() = *ei;
        dfs_all_path(next, end, all_path, current_path, visited, exit_flag);
      }
      // cout << current_path.back() << endl;
    }
  }

  // Backtrack by removing the current vertex from the current path and marking
  // it as unvisited
  current_path.pop_back();
  visited[start] = false;
}

std::pair<int, std::vector<Path>>
StateClassGraph::calculate_wcet(ScgVertexD &start, ScgVertexD &end,
                                std::string &exit_flag) {
  // Find all paths between v1 and v5
  std::vector<Path> all_paths;
  Path current_path;
  std::vector<bool> visited(num_vertices(scg), false);
  std::vector<Path> wcet_path;

  dfs_all_path(start, end, all_paths, current_path, visited, exit_flag);
  int max_weight = 0;
  // Print all paths

  for (auto path_it : all_paths) {
    int path_max = 0;
    for (Path::const_iterator it = path_it.begin(); it != path_it.end() - 1;
         ++it) {
      path_max += scg[*it].time.second;
    }
    max_weight = std::max(max_weight, path_max);
    if (path_max == max_weight) {
      // a equal b => wcet path
      Path path;
      path.assign(path_it.begin(), path_it.end() - 1);
      wcet_path.push_back(path);
    }
  }

  //    std::cout << max_weight << std::endl;
  return std::make_pair(max_weight, wcet_path);
}

std::set<ScgVertexD> StateClassGraph::find_task_vertex(std::string task_name) {
  std::set<ScgVertexD> result;
  for (const auto &v : scg_vertex_map) {
    if (v.first.mark.labels.find(task_name) != v.first.mark.labels.end()) {
      result.insert(v.second);
    }
  }
  return result;
}
