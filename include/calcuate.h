#ifndef PPTPN_INCLUDE_CALCULATE_H
#define PPTPN_INCLUDE_CALCULATE_H

#include "state_class_graph.h"
#include <numeric>
#include <future>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <stdexcept>
#include <vector>
#include <memory>
#include <utility>
#include <algorithm>

// 从状态类图上找到包含任务A,B的Vertex
std::pair<std::set<ScgVertexD>, std::set<ScgVertexD>> find_task_vertex(SCG& scg, std::string task_name);
// 找到从A到B的所有路径
void dfs_all_path(SCG& scg, ScgVertexD start, ScgVertexD end,
                                   std::vector<Path> &all_path,
                                   Path &current_path,
                                   std::vector<bool> &visited,
                                   const std::string &exit_flag);
std::pair<int, std::vector<Path>> calculate_wcet(SCG &scg, ScgVertexD start, ScgVertexD end,
                                std::string exit_flag);

int task_wcet(SCG& scg, std::string start_task, std::string exit_task);
bool check_deadlock(SCG& scg, std::vector<string>& period_task_lists);

class ThreadPool {
public:
    explicit ThreadPool(size_t threads) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { 
                            return stop || !tasks.empty(); 
                        });
                        
                        if (stop && tasks.empty()) return;
                        
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) throw std::runtime_error("ThreadPool is stopped");
            tasks.emplace([task](){ (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker: workers) {
            worker.join();
        }
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop = false;
};

#endif // PPTPN_INCLUDE_CALCULATE_H 


