#include "priority_state_graph.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fstream>
#include <boost/graph/graphviz.hpp>
#include <queue>
#include <algorithm>
#include <unordered_set>

namespace priority_scg
{
    static std::shared_ptr<spdlog::logger> init_state_logger()
    {
        auto logger = spdlog::stdout_color_mt("state_class");
        logger->set_pattern("[%^%l%$] [%^STATE CLASS%$] %v");
        logger->set_level(spdlog::level::debug);
        return logger;
    }

    static auto state_logger = init_state_logger();

    PriorityStateClassGraph::PriorityStateClassGraph(const PriorityTPNGraph &petri_net)
        : petri_net(petri_net)
    {
        graph[boost::graph_bundle].name = "优先级时间Petri网状态类图";
    }

    // 获取初始标记下的状态类
    std::shared_ptr<PriorityStateClass> PriorityStateClassGraph::get_initial_state_class()
    {
        state_logger->debug("正在获取初始状态类...");
        Marking initial_marking;

        boost::graph_traits<PriorityTPNGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = boost::vertices(petri_net); vi != vi_end; ++vi)
        {
            const auto &vertex = petri_net[*vi];
            if (vertex.is_place())
            {
                const auto &place = vertex.as_place();
                if (place.token > 0)
                {
                    initial_marking[*vi] = place.token;
                    state_logger->debug("初始标记: 库所 {} 的 token 数量为 {}", vertex.name, place.token);
                }
            }
        }

        std::map<ptpn_v_desc, TimeInterval> enabled_runtimes;
        reset_petri_net(initial_marking);

        std::vector<ptpn_v_desc> enabled_transitions;
        for (boost::tie(vi, vi_end) = boost::vertices(petri_net); vi != vi_end; ++vi)
        {
            const auto &vertex = petri_net[*vi];

            if (vertex.is_transition())
            {
                bool is_enabled = true;
                boost::graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;

                for (boost::tie(ei, ei_end) = boost::in_edges(*vi, petri_net); ei != ei_end; ++ei)
                {
                    auto source = boost::source(*ei, petri_net);
                    const auto &edge = petri_net[*ei];
                    const auto &source_vertex = petri_net[source];

                    if (source_vertex.is_place())
                    {
                        const auto &place = source_vertex.as_place();
                        if (place.token < edge.weight)
                        {
                            is_enabled = false;
                            break;
                        }
                    }
                }

                if (is_enabled)
                {
                    enabled_transitions.push_back(*vi);

                    // 获取变迁的时间区间
                    const auto &transition = vertex.as_transition();
                    TimeInterval interval(transition.const_time.first, transition.const_time.second);
                    enabled_runtimes[*vi] = interval;

                    state_logger->debug("初始使能变迁: {} 时间区间 {}", vertex.name, interval.to_string());
                }
            }
        }

        auto filtered_transitions = filter_by_priority(enabled_transitions);
        std::map<ptpn_v_desc, TimeInterval> filtered_runtimes;
        for (const auto &t : filtered_transitions)
        {
            filtered_runtimes[t] = enabled_runtimes[t];
            const auto &vertex = petri_net[t];
            state_logger->debug("优先级过滤后的使能变迁: {}", vertex.name);
        }

        return std::make_shared<PriorityStateClass>(initial_marking, filtered_runtimes, std::map<ptpn_v_desc, TimeInterval>());
    }

    // 重置 Petri 网到指定的标记状态
    void PriorityStateClassGraph::reset_petri_net(const Marking &marking)
    {
        // 首先，将所有库所的token清零
        boost::graph_traits<PriorityTPNGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = boost::vertices(petri_net); vi != vi_end; ++vi)
        {
            auto &vertex = petri_net[*vi];
            if (vertex.is_place())
            {
                auto &place = vertex.as_place();
                place.token = 0;
            }
        }

        // 然后，设置marking中指定的token
        for (const auto &[place, tokens] : marking)
        {
            if (tokens > 0)
            {
                auto &vertex = petri_net[place];
                if (vertex.is_place())
                {
                    auto &place_node = vertex.as_place();
                    place_node.token = tokens;
                }
            }
        }
    }

    // 根据优先级过滤使能变迁
    std::vector<ptpn_v_desc> PriorityStateClassGraph::filter_by_priority(
        const std::vector<ptpn_v_desc> &enabled_transitions)
    {
        if (enabled_transitions.empty())
        {
            return {};
        }

        // 首先，按照优先级对变迁排序（数值越小优先级越高）
        std::vector<std::pair<ptpn_v_desc, int>> transitions_with_priority;
        for (const auto &t : enabled_transitions)
        {
            const auto &vertex = petri_net[t];
            if (vertex.is_transition())
            {
                const auto &transition = vertex.as_transition();
                transitions_with_priority.push_back({t, transition.priority});
            }
        }

        // 按优先级排序
        std::sort(transitions_with_priority.begin(), transitions_with_priority.end(),
                  [](const auto &a, const auto &b)
                  {
                      return a.second < b.second; // 优先级数值小的排前面
                  });

        // 获取最高优先级
        int highest_priority = transitions_with_priority.front().second;

        // 只保留具有最高优先级的变迁
        std::vector<ptpn_v_desc> filtered_transitions;
        for (const auto &[t, priority] : transitions_with_priority)
        {
            if (priority == highest_priority)
            {
                filtered_transitions.push_back(t);
            }
            else
            {
                break; // 后面的优先级都低于最高优先级，直接结束
            }
        }

        return filtered_transitions;
    }

    // 计算变迁的时间区间
    TimeInterval PriorityStateClassGraph::compute_time_interval(
        const PriorityStateClass &state, ptpn_v_desc transition)
    {
        // 从状态类获取变迁的运行时间区间
        if (state.is_transition_enabled(transition))
        {
            return state.get_enabled_runtime(transition);
        }
        else if (state.is_transition_suspended(transition))
        {
            return state.get_suspended_runtime(transition);
        }

        // 如果变迁既不是使能的也不是挂起的，返回无效区间
        return TimeInterval(1, 0); // 下界大于上界，表示无效区间
    }

    // 添加状态类到图中
    SCGVertex PriorityStateClassGraph::add_state(const PriorityStateClass &state)
    {
        // 计算状态的哈希值，用于快速查找
        std::size_t state_hash = state.hash();

        // 查找状态是否已在图中
        auto it = state_vertex_map.find(state_hash);
        if (it != state_vertex_map.end())
        {
            // 获取已有顶点
            SCGVertex existing_vertex = it->second;

            // 验证状态是否真的相同（因为可能有哈希碰撞）
            if (*graph[existing_vertex].state == state)
            {
                return existing_vertex;
            }
        }

        // 创建新状态类并添加到图中
        SCGVertex new_vertex = boost::add_vertex(graph);

        // 设置顶点属性
        graph[new_vertex].id = "S" + std::to_string(new_vertex);
        graph[new_vertex].state = std::make_shared<PriorityStateClass>(state);
        graph[new_vertex].label = graph[new_vertex].id + "\n" + state.to_string();

        // 添加到映射
        state_vertex_map[state_hash] = new_vertex;

        state_logger->debug("添加新状态: {}", graph[new_vertex].id);

        return new_vertex;
    }

    // 添加状态间的边
    SCGEdge PriorityStateClassGraph::add_edge(
        SCGVertex from, SCGVertex to, ptpn_v_desc transition, const TimeInterval &interval)
    {
        // 检查边是否已存在
        boost::graph_traits<StateClassGraph>::out_edge_iterator ei, ei_end;
        for (boost::tie(ei, ei_end) = boost::out_edges(from, graph); ei != ei_end; ++ei)
        {
            if (boost::target(*ei, graph) == to && graph[*ei].transition == transition)
            {
                // 边已存在，更新时间区间（取交集）
                graph[*ei].time_interval = graph[*ei].time_interval.intersect(interval);
                return *ei;
            }
        }

        // 添加新边
        bool success;
        SCGEdge new_edge;
        boost::tie(new_edge, success) = boost::add_edge(from, to, graph);

        if (success)
        {
            // 设置边属性
            const auto &transition_vertex = petri_net[transition];
            std::string transition_name = transition_vertex.name;

            graph[new_edge].transition = transition;
            graph[new_edge].xlabel = transition_name;
            graph[new_edge].time_interval = interval;

            state_logger->debug("添加新边: {} --[{}, {}]--> {}",
                                graph[from].id, transition_name, interval.to_string(), graph[to].id);
        }

        return new_edge;
    }

    // 计算在当前标记下的使能变迁
    std::vector<ptpn_v_desc> PriorityStateClassGraph::compute_enabled_transitions(
        const PriorityStateClass &state)
    {
        // 获取当前标记
        const Marking &marking = state.get_marking();

        // 重置Petri网到当前标记状态
        reset_petri_net(marking);

        // 计算使能变迁
        std::vector<ptpn_v_desc> enabled_transitions;
        boost::graph_traits<PriorityTPNGraph>::vertex_iterator vi, vi_end;

        for (boost::tie(vi, vi_end) = boost::vertices(petri_net); vi != vi_end; ++vi)
        {
            const auto &vertex = petri_net[*vi];

            if (vertex.is_transition())
            {
                // 检查变迁是否使能
                bool is_enabled = true;
                boost::graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;

                for (boost::tie(ei, ei_end) = boost::in_edges(*vi, petri_net); ei != ei_end; ++ei)
                {
                    auto source = boost::source(*ei, petri_net);
                    const auto &edge = petri_net[*ei];
                    const auto &source_vertex = petri_net[source];

                    if (source_vertex.is_place())
                    {
                        const auto &place = source_vertex.as_place();
                        // 检查输入库所是否有足够的token
                        if (place.token < edge.weight)
                        {
                            is_enabled = false;
                            break;
                        }
                    }
                }

                if (is_enabled)
                {
                    enabled_transitions.push_back(*vi);
                }
            }
        }

        return enabled_transitions;
    }

    // 变迁触发后得到的后继状态
    PriorityStateClass PriorityStateClassGraph::fire_transition(
        const PriorityStateClass &state, ptpn_v_desc transition)
    {
        // 获取原始标记
        Marking new_marking = state.get_marking();

        // 重置Petri网到当前标记
        reset_petri_net(new_marking);

        // 移除输入库所的token
        boost::graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;
        for (boost::tie(ei, ei_end) = boost::in_edges(transition, petri_net); ei != ei_end; ++ei)
        {
            auto source = boost::source(*ei, petri_net);
            const auto &edge = petri_net[*ei];
            auto &source_vertex = petri_net[source];

            if (source_vertex.is_place())
            {
                auto &place = source_vertex.as_place();
                // 移除token
                place.token -= edge.weight;
                // 更新标记
                if (place.token > 0)
                {
                    new_marking[source] = place.token;
                }
                else
                {
                    new_marking.erase(source);
                }
            }
        }

        // 添加输出库所的token
        boost::graph_traits<PriorityTPNGraph>::out_edge_iterator eo, eo_end;
        for (boost::tie(eo, eo_end) = boost::out_edges(transition, petri_net); eo != eo_end; ++eo)
        {
            auto target = boost::target(*eo, petri_net);
            const auto &edge = petri_net[*eo];
            auto &target_vertex = petri_net[target];

            if (target_vertex.is_place())
            {
                auto &place = target_vertex.as_place();
                // 添加token
                place.token += edge.weight;
                // 更新标记
                new_marking[target] = place.token;
            }
        }

        // 计算新的使能变迁
        auto enabled_transitions = compute_enabled_transitions(PriorityStateClass(new_marking, std::map<ptpn_v_desc, TimeInterval>(), std::map<ptpn_v_desc, TimeInterval>()));
        auto filtered_transitions = filter_by_priority(enabled_transitions);

        // 获取所有使能变迁的时间区间
        std::map<ptpn_v_desc, TimeInterval> new_enabled_runtimes;
        std::map<ptpn_v_desc, TimeInterval> new_suspended_runtimes;

        // 处理新使能的变迁
        for (const auto &t : filtered_transitions)
        {
            // 获取变迁在Petri网中的时间约束
            const auto &vertex = petri_net[t];
            const auto &transition_node = vertex.as_transition();

            // 如果变迁在原状态中是使能的，保持其时间区间
            if (state.is_transition_enabled(t) && t != transition)
            {
                // 这个变迁在原状态中是使能的，保持其时间区间
                new_enabled_runtimes[t] = state.get_enabled_runtime(t);
            }
            // 如果变迁在原状态中是挂起的，现在变为使能
            else if (state.is_transition_suspended(t))
            {
                // 使用挂起变迁的时间区间
                new_enabled_runtimes[t] = state.get_suspended_runtime(t);
            }
            // 如果是新使能的变迁，设置初始时间区间
            else
            {
                TimeInterval interval(transition_node.const_time.first, transition_node.const_time.second);
                new_enabled_runtimes[t] = interval;
            }
        }

        // 处理被挂起的变迁
        for (const auto &[t, interval] : state.get_enabled_runtimes())
        {
            // 如果变迁在原状态中是使能的，但在新状态中不再使能且不是刚刚触发的变迁
            if (t != transition &&
                std::find(filtered_transitions.begin(), filtered_transitions.end(), t) == filtered_transitions.end())
            {
                // 变迁被挂起
                new_suspended_runtimes[t] = interval;
            }
        }

        // 创建新状态类
        return PriorityStateClass(new_marking, new_enabled_runtimes, new_suspended_runtimes);
    }

    // 找到状态在图中对应的顶点描述符
    SCGVertex PriorityStateClassGraph::find_state_vertex(const PriorityStateClass &state) const
    {
        // 计算状态的哈希值
        std::size_t state_hash = state.hash();

        // 在映射中查找
        auto it = state_vertex_map.find(state_hash);
        if (it != state_vertex_map.end())
        {
            // 验证状态是否真的相同
            if (*graph[it->second].state == state)
            {
                return it->second;
            }
        }

        // 找不到状态，返回无效顶点
        return boost::graph_traits<StateClassGraph>::null_vertex();
    }

    // 生成状态类图
    void PriorityStateClassGraph::generate_state_class_graph()
    {
        state_logger->info("开始生成状态类图...");

        // 清空现有图和映射
        graph.clear();
        state_vertex_map.clear();

        // 获取初始状态类
        auto initial_state = get_initial_state_class();
        if (!initial_state)
        {
            state_logger->error("无法获取初始状态类");
            return;
        }

        // 添加初始状态类到图中
        SCGVertex initial_vertex = add_state(*initial_state);

        // 使用BFS算法遍历状态空间
        std::queue<SCGVertex> vertex_queue;
        vertex_queue.push(initial_vertex);

        // 记录处理过的状态数
        int processed_states = 0;
        std::unordered_set<SCGVertex> processed_vertices;

        while (!vertex_queue.empty())
        {
            // 获取队列中的下一个状态
            SCGVertex current_vertex = vertex_queue.front();
            vertex_queue.pop();

            // 如果已经处理过这个状态，跳过
            if (processed_vertices.find(current_vertex) != processed_vertices.end())
            {
                continue;
            }

            // 标记为已处理
            processed_vertices.insert(current_vertex);

            // 获取当前状态类
            const PriorityStateClass &current_state = *graph[current_vertex].state;

            // 记录处理进度
            processed_states++;
            if (processed_states % 100 == 0)
            {
                state_logger->info("已处理 {} 个状态，当前队列大小: {}", processed_states, vertex_queue.size());
            }

            // 计算当前状态下的使能变迁
            auto enabled_transitions = compute_enabled_transitions(current_state);

            // 根据优先级过滤使能变迁
            auto filtered_transitions = filter_by_priority(enabled_transitions);

            // 对每个使能变迁计算时间区间并生成后继状态
            for (const auto &t : filtered_transitions)
            {
                // 计算变迁的时间区间
                TimeInterval interval = compute_time_interval(current_state, t);

                if (!interval.is_valid())
                {
                    state_logger->debug("变迁 {} 的时间区间无效，跳过", petri_net[t].name);
                    continue; // 跳过无效区间
                }

                // 触发变迁得到后继状态
                PriorityStateClass successor_state = fire_transition(current_state, t);

                // 检查后继状态是否有效
                if (!successor_state.is_valid())
                {
                    state_logger->debug("变迁 {} 触发后的状态无效，跳过", petri_net[t].name);
                    continue;
                }

                // 添加后继状态到图中
                SCGVertex successor_vertex = add_state(successor_state);

                // 添加边
                add_edge(current_vertex, successor_vertex, t, interval);

                // 检查状态是否需要加入队列
                if (processed_vertices.find(successor_vertex) == processed_vertices.end())
                {
                    // 将新状态加入队列
                    vertex_queue.push(successor_vertex);
                }
            }
        }

        state_logger->info("状态类图生成完成，共 {} 个状态，{} 条边",
                           boost::num_vertices(graph), boost::num_edges(graph));
    }

    // 获取状态类图的节点数量
    std::size_t PriorityStateClassGraph::get_vertex_count() const
    {
        return boost::num_vertices(graph);
    }

    // 获取状态类图的边数量
    std::size_t PriorityStateClassGraph::get_edge_count() const
    {
        return boost::num_edges(graph);
    }

    // 保存状态类图为DOT格式
    bool PriorityStateClassGraph::save_to_dot(const std::string &filename) const
    {
        try
        {
            std::ofstream dot_file(filename);
            if (!dot_file)
            {
                state_logger->error("无法打开文件: {}", filename);
                return false;
            }

            // 写入DOT格式
            boost::write_graphviz(dot_file, graph, [this](std::ostream &out, const SCGVertex &v)
                                  { out << "[label=\"" << graph[v].label << "\", shape=\"box\"]"; }, [this](std::ostream &out, const SCGEdge &e)
                                  { out << "[label=\"" << graph[e].xlabel << "\\n"
                                        << graph[e].time_interval.to_string() << "\"]"; }, [this](std::ostream &out)
                                  { out << "graph [rankdir=LR, fontname=\"SimSun\"];\n"
                                        << "node [fontname=\"SimSun\"];\n"
                                        << "edge [fontname=\"SimSun\"];\n"; });

            dot_file.close();
            state_logger->info("状态类图已保存到: {}", filename);
            return true;
        }
        catch (const std::exception &e)
        {
            state_logger->error("保存DOT文件时发生错误: {}", e.what());
            return false;
        }
    }

    // 打印状态类图信息
    void PriorityStateClassGraph::print_graph_info() const
    {
        state_logger->info("状态类图信息:");
        state_logger->info("  节点数量: {}", get_vertex_count());
        state_logger->info("  边数量: {}", get_edge_count());

        // 计算终止状态数量（出度为0的节点）
        int terminal_states = 0;
        boost::graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = boost::vertices(graph); vi != vi_end; ++vi)
        {
            if (boost::out_degree(*vi, graph) == 0)
            {
                terminal_states++;
            }
        }

        state_logger->info("  终止状态数量: {}", terminal_states);

        // 检查死锁状态
        bool has_deadlock_state = has_deadlock();
        state_logger->info("  是否存在死锁状态: {}", has_deadlock_state ? "是" : "否");

        // 获取最大深度
        int max_depth = get_max_depth();
        state_logger->info("  可达性树最大深度: {}", max_depth);
    }

    // 检查状态类图是否有死锁状态
    bool PriorityStateClassGraph::has_deadlock() const
    {
        // 检查是否有出度为0且不是终止状态的节点
        boost::graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = boost::vertices(graph); vi != vi_end; ++vi)
        {
            if (boost::out_degree(*vi, graph) == 0)
            {
                // 检查该状态是否有使能变迁
                const PriorityStateClass &state = *graph[*vi].state;
                if (!state.get_enabled_runtimes().empty())
                {
                    // 有使能变迁但没有出边，可能是死锁
                    state_logger->warn("发现死锁状态: {}", graph[*vi].id);
                    return true;
                }
            }
        }

        return false;
    }

    // 获取可达性树的最大深度
    int PriorityStateClassGraph::get_max_depth() const
    {
        // 从初始节点（ID为S0）开始BFS
        SCGVertex initial_vertex = 0; // 通常初始节点是第一个添加的

        std::unordered_map<SCGVertex, int> depths;
        depths[initial_vertex] = 0;

        std::queue<SCGVertex> vertex_queue;
        vertex_queue.push(initial_vertex);

        int max_depth = 0;

        while (!vertex_queue.empty())
        {
            SCGVertex current = vertex_queue.front();
            vertex_queue.pop();

            int current_depth = depths[current];
            max_depth = std::max(max_depth, current_depth);

            // 访问所有后继节点
            boost::graph_traits<StateClassGraph>::out_edge_iterator ei, ei_end;
            for (boost::tie(ei, ei_end) = boost::out_edges(current, graph); ei != ei_end; ++ei)
            {
                SCGVertex target = boost::target(*ei, graph);

                // 如果节点还未访问过，或者找到了更深的路径
                if (depths.find(target) == depths.end() || depths[target] < current_depth + 1)
                {
                    depths[target] = current_depth + 1;
                    vertex_queue.push(target);
                }
            }
        }

        return max_depth;
    }

} // namespace priority_scg