#include "calcuate.h"

std::pair<std::set<ScgVertexD>, std::set<ScgVertexD>> find_task_vertex(SCG& Scg, std::string task_name) {
   // 构建要搜索的标记
  const std::string entry_mark = task_name + "entry";
  const std::string exit_mark = task_name + "exit";
  
  std::set<ScgVertexD> entry_vertices, exit_vertices;
  
  for (auto [vi, vi_end] = boost::vertices(Scg); vi != vi_end; ++vi) {
    const std::string &vertex_id = Scg[*vi].id;
    
    if (vertex_id.find(entry_mark) != std::string::npos) {
      entry_vertices.insert(*vi);
      BOOST_LOG_TRIVIAL(debug) << "Found entry vertex: " << vertex_id;
    } 
    else if (vertex_id.find(exit_mark) != std::string::npos) {
      exit_vertices.insert(*vi);
      BOOST_LOG_TRIVIAL(debug) << "Found exit vertex: " << vertex_id;
    }
  }
  
  if (entry_vertices.empty() || exit_vertices.empty()) {
    BOOST_LOG_TRIVIAL(warning) << "Missing vertices for task: " << task_name;
  }
  
  return std::make_pair(entry_vertices, exit_vertices);
}


void dfs_all_path(SCG& scg, ScgVertexD start, ScgVertexD end,
                                 std::vector<Path>& all_paths,
                                 Path& current_path,
                                 std::vector<bool>& visited,
                                 const std::string &exit_flag) {
  visited[start] = true;
  current_path.push_back(ScgEdgeD());

  // 检查是否到达目标
  if (start == end || scg[start].id.find(exit_flag) != std::string::npos) {
    all_paths.push_back(current_path);
  } else {
    // 递归探索邻居节点
    for (auto [ei, ei_end] = boost::out_edges(start, scg); ei != ei_end; ++ei) {
      ScgVertexD next = boost::target(*ei, scg);
      
      if (!visited[next]) {
        current_path.back() = *ei;
        dfs_all_path(scg, next, end, all_paths, current_path, visited, exit_flag);
      }
    }
  }

  // 回溯
  current_path.pop_back();
  visited[start] = false;
}

std::pair<int, std::vector<Path>> calculate_wcet(SCG &scg, ScgVertexD start, ScgVertexD end,
                                std::string exit_flag) {
  // 获取所有路径
  std::vector<Path> all_paths;
  {
    Path current_path;
    std::vector<bool> visited(num_vertices(scg), false);
    dfs_all_path(scg, start, end, all_paths, current_path, visited, exit_flag);
  }

  // 计算最大执行时间
  int max_weight = 0;
  std::vector<Path> wcet_paths;
  
  for (const auto& path : all_paths) {
    // 计算当前路径的执行时间
    int path_weight = std::accumulate(
      path.begin(), 
      path.end() - 1,
      0,
      [&scg](int sum, const ScgEdgeD& edge) {
        return sum + scg[edge].time.second;
      }
    );
    
    // 更新最大执行时间和对应路径
    if (path_weight > max_weight) {
      max_weight = path_weight;
      wcet_paths.clear();
      wcet_paths.push_back(Path(path.begin(), path.end() - 1));
    } else if (path_weight == max_weight) {
      wcet_paths.push_back(Path(path.begin(), path.end() - 1));
    }
  }

  return std::make_pair(max_weight, wcet_paths);
}


int task_wcet(SCG& scg, std::string start_task, std::string exit_task)
{
  auto [start_vertices, end_vertices] = find_task_vertex(scg, start_task);
  
  // 创建所有可能的起始-结束顶点对
  std::vector<std::pair<ScgVertexD, ScgVertexD>> vertex_pairs;
  vertex_pairs.reserve(start_vertices.size() * end_vertices.size());
  
  for (const auto& start : start_vertices) {
    for (const auto& end : end_vertices) {
      vertex_pairs.emplace_back(start, end);
    }
  }

  BOOST_LOG_TRIVIAL(info) << "Found " << vertex_pairs.size() << " vertex pairs";

  // 创建线程池
  const unsigned int thread_count = std::thread::hardware_concurrency();
  ThreadPool pool(thread_count);
  std::vector<std::future<int>> futures;
  futures.reserve(vertex_pairs.size());
  std::string exit_flag = start_task + "exit";
  // 提交任务到线程池
  for (const auto& [start, end] : vertex_pairs) {
    futures.push_back(
      pool.enqueue([&scg, start, end, exit_flag]() {
            return calculate_wcet(scg, start, end, exit_flag).first;
      })
    );
  }

  std::vector<int> wcet_results;
  wcet_results.reserve(futures.size());
  
  for (auto& future : futures) {
    wcet_results.push_back(future.get());
  }

  // 找出最大WCET
  auto max_wcet = *std::max_element(wcet_results.begin(), wcet_results.end());
  
  BOOST_LOG_TRIVIAL(info) << "Maximum WCET: " << max_wcet;
  return max_wcet;
}

// 检查死锁,排除周期任务的发生
bool check_deadlock(SCG& scg, std::vector<std::string>& period_task_lists) {
    typedef boost::graph_traits<SCG>::vertex_iterator vertex_iter;
    vertex_iter vi, vi_end;
    
    for (boost::tie(vi, vi_end) = boost::vertices(scg); vi != vi_end; ++vi) {
        // 跳过初始节点（初始节点的索引为0）
        if (*vi == 0) continue;
        
        auto out_degree = boost::out_degree(*vi, scg);
        
        // 如果出度为0是死锁
        if (out_degree == 0) {
            BOOST_LOG_TRIVIAL(warning) << "Deadlock detected at vertex: " << scg[*vi].id;
            return true;
        }
        
        // 检查后继边是否全是周期任务
        bool all_successors_are_periodic = true;
        for (auto [ei, ei_end] = boost::out_edges(*vi, scg); ei != ei_end; ++ei) {
            ScgVertexD target_vertex = boost::target(*ei, scg);
            const std::string& target_id = scg[target_vertex].id;
            
            // 如果后继任务不在周期任务列表中，标记为false
            if (std::find(period_task_lists.begin(), period_task_lists.end(), target_id) == period_task_lists.end()) {
                all_successors_are_periodic = false;
                break;
            }
        }
        
        // 如果所有后继边都是周期任务，也是死锁
        if (all_successors_are_periodic) {
            BOOST_LOG_TRIVIAL(warning) << "Deadlock detected at vertex with only periodic successors: " << scg[*vi].id;
            return true;
        }
    }
    
    // 如果没有检测到死锁，返回false
    return false;
}