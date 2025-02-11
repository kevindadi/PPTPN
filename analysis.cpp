#include "analysis.h"
#include <numeric>
#include <QMessageBox>

std::pair<std::set<ScgVertexD>, std::set<ScgVertexD>> find_task_vertex(SCG& Scg, std::string task_f, std::string task_l) {
    const std::string entry_mark = task_f + "entry";
    const std::string exit_mark = task_l + "exit";

    std::set<ScgVertexD> entry_vertices, exit_vertices;

    for (auto [vi, vi_end] = boost::vertices(Scg); vi != vi_end; ++vi) {
        const std::string &vertex_id = Scg[*vi].id;

        if (vertex_id.find(entry_mark) != std::string::npos) {
            entry_vertices.insert(*vi);
        }
        else if (vertex_id.find(exit_mark) != std::string::npos) {
            exit_vertices.insert(*vi);
        }
    }

    if (entry_vertices.empty()) {
        QMessageBox::critical(nullptr, "错误", "未发现该任务:" + QString::fromStdString(task_f));
        BOOST_LOG_TRIVIAL(warning) << "Missing vertices for task: " << task_f;
    } else if (exit_vertices.empty()) {
        QMessageBox::critical(nullptr, "错误", "未发现该任务:" + QString::fromStdString(task_l));
        BOOST_LOG_TRIVIAL(warning) << "Missing vertices for task: " << task_l;
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
    // TOOD: 判断名字是否在内
    auto [start_vertices, end_vertices] = find_task_vertex(scg, start_task, exit_task);

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
    std::string exit_flag = exit_task + "exit";
    // 提交任务到线程池
    for (const auto& [s, e] : vertex_pairs) {
        futures.push_back(
            pool.enqueue([&scg, s, e, exit_flag]() {
                return calculate_wcet(scg, s, e, exit_flag).first;
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

std::vector<std::string> check_deadlock(SCG& scg) {
    std::vector<std::string> deadlock_info;
    std::vector<ScgVertexD> no_successors;
    typedef boost::graph_traits<SCG>::vertex_iterator vertex_iter;
    vertex_iter vi, vi_end;
    for (boost::tie(vi, vi_end) = boost::vertices(scg); vi != vi_end; ++vi) {
        if (boost::out_degree(*vi, scg) == 0) {
            no_successors.push_back(*vi);
        }
    }
    if (no_successors.empty()) {
        deadlock_info.push_back("未发现死锁");
    } else {
        for (const auto &v : no_successors) {
            deadlock_info.push_back(scg[v].id);
        }
    }
    return deadlock_info;
}
