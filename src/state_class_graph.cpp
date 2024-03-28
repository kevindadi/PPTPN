//
// Created by 张凯文 on 2024/3/22.
//

#include "state_class_graph.h"
#include <algorithm>
#include <future>
#include <iostream>
#include <string>

void StateClass::print_current_mark() {
  for (const auto &l : mark.labels) {
    std::cout << l << " ";
  }
  std::cout << std::endl;
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
    svd = boost::add_vertex(SCGVertex{sc.to_scg_vertex(), sc.to_scg_vertex()},
                            scg);
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
  if (start == end || (scg[start].id.find(exit_flag) != std::string::npos)) {
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

void StateClassGraph::dfs_all_path(ScgVertexD start, std::string &end,
                                   std::vector<Path> &all_path,
                                   Path &current_path,
                                   std::vector<bool> &visited,
                                   std::string &exit_flag) {
  visited[start] = true;
  current_path.push_back(ScgEdgeD());
  // std::cout << "dfs all path" << std::endl;
  //  Check if the current vertex is the goal
  if ((scg[start].id.find(end) != std::string::npos) ||
      (scg[start].id.find(exit_flag) != std::string::npos)) {
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
    for (auto it = path_it.begin(); it != path_it.end() - 1; ++it) {
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

  return std::make_pair(max_weight, wcet_path);
}

int StateClassGraph::only_calculate_wcet(ScgVertexD start, ScgVertexD end) {
  // Find all paths between v1 and v5
  std::vector<Path> all_paths;
  Path current_path;
  std::vector<bool> visited(num_vertices(scg), false);
  std::vector<Path> wcet_path;
  std::string exit_flag = "Bend";
  std::string end_flag = "Bexit";
  dfs_all_path(start, end_flag, all_paths, current_path, visited, exit_flag);
  int max_weight = 0;
  // Print all paths

  for (auto path_it : all_paths) {
    int path_max = 0;
    for (auto it = path_it.begin(); it != path_it.end() - 1; ++it) {
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

  return max_weight;
}

std::pair<std::set<ScgVertexD>, std::set<ScgVertexD>>
StateClassGraph::find_task_vertex(std::string task_name) {
  //  std::pair<std::set<ScgVertexD>,std::set<ScgVertexD>> result;
  std::set<ScgVertexD> start, end;
  std::string task_s = task_name + "entry";
  std::string task_e = task_name + "exit";
  BOOST_FOREACH (ScgVertexD v, boost::vertices(scg)) {
    if (scg[v].id.find(task_s) != std::string::npos) {
      start.insert(v);
    } else if (scg[v].id.find(task_e) != std::string::npos) {
      end.insert(v);
    } else {
    }
  }
  //  for (const auto &v : scg_vertex_map) {
  //    if (v.first.mark.labels.find(task_name) != v.first.mark.labels.end()) {
  //      result.insert(v.second);
  //    }
  //  }
  return std::make_pair(start, end);
}
bool StateClassGraph::check_deadlock() {
  std::vector<ScgVertexD> no_successors;
  typedef boost::graph_traits<SCG>::vertex_iterator vertex_iter;
  vertex_iter vi, vi_end;
  for (boost::tie(vi, vi_end) = boost::vertices(scg); vi != vi_end; ++vi) {
    ScgVertexD v = *vi;
    if (boost::out_degree(v, scg) == 0) {
      no_successors.push_back(v);
    }
  }
  if (no_successors.empty()) {
    std::cout << "????" << std::endl;
    return false;
  } else {
    for (const auto &v : no_successors) {
      std::cout << scg[v].id << std::endl;
    }
  }
  return true;
}
int StateClassGraph::task_wcet() {
  auto res = find_task_vertex("B");

  std::vector<std::pair<ScgVertexD, ScgVertexD>> task_s_e;
  std::vector<ScgVertexD> task_start;
  for (auto t1 = res.first.begin(); t1 != res.first.end(); ++t1) {
    task_start.push_back(*t1);
    for (auto t2 = res.second.begin(); t2 != res.second.end(); ++t2) {
      task_s_e.emplace_back(*t1, *t2);
    }
  }
  std::cout << task_start.size() << std::endl;
  std::vector<std::pair<int, std::vector<Path>>> all_wcet_paths;
  std::vector<int> all_wcet;
  std::vector<std::future<int>> futures;

  futures.reserve(task_start.size());
  for (auto &task : task_start) {
    futures.push_back(std::async(std::launch::async,
                                 &StateClassGraph::only_calculate_wcet, this,
                                 task, task));
  }

  all_wcet.reserve(futures.size());
  for (auto &future : futures) {
    all_wcet.push_back(future.get());
  }

  auto wcet =
      std::max_element(all_wcet.begin(), all_wcet.end(),
                       [](const int &a, const int &b) { return a < b; });
  std::cout << *wcet << std::endl;
  return *wcet;
}
