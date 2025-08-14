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
        graph[graph_bundle].name = "优先级时间Petri网状态类图";
    }

    // 获取初始标记下的状态类
    std::shared_ptr<PriorityStateClass> PriorityStateClassGraph::get_initial_state_class()
    {
        state_logger->debug("正在获取初始状态类...");
        Marking initial_marking;

        graph_traits<PriorityTPNGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = vertices(petri_net); vi != vi_end; ++vi)
        {
            if (const auto &vertex = petri_net[*vi]; vertex.is_place())
            {
                if (const auto &[token, capacity] = vertex.as_place(); token > 0)
                {
                    initial_marking[*vi] = token;
                    state_logger->debug("初始标记: 库所 {} 的 token 数量为 {}", vertex.name, token);
                }
            }
        }

        // 首先创建一个只有标记的初始状态类
        PriorityStateClass initial_state(initial_marking, std::map<ptpn_v_desc, TimeInterval>(), std::map<ptpn_v_desc, TimeInterval>());

        std::map<ptpn_v_desc, TimeInterval> enabled_runtimes;
        std::vector<ptpn_v_desc> enabled_transitions;

        for (boost::tie(vi, vi_end) = vertices(petri_net); vi != vi_end; ++vi)
        {
            const auto &vertex = petri_net[*vi];

            if (vertex.is_transition())
            {
                bool all_places_have_tokens = true;
                graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;
                for (boost::tie(ei, ei_end) = in_edges(*vi, petri_net); ei != ei_end; ++ei)
                {
                    const auto &[label, weight] = petri_net[*ei];
                    const auto &source = boost::source(*ei, petri_net);

                    if (const auto &source_vertex = petri_net[source]; source_vertex.is_place())
                    {
                        if (const auto &[token, capacity] = source_vertex.as_place(); token < weight)
                        {
                            all_places_have_tokens = false;
                            break;
                        }
                    }
                }
                if (all_places_have_tokens)
                {
                    enabled_transitions.push_back(*vi);

                    // 获取变迁的时间区间
                    TimeInterval interval{0, 0};
                    enabled_runtimes[*vi] = interval;

                    state_logger->debug("初始使能变迁: {} 时间区间 {}", vertex.name, interval.to_string());
                }
            }
        }

        const auto filtered_transitions = filter_by_priority(enabled_transitions);
        std::map<ptpn_v_desc, TimeInterval> filtered_runtimes;
        for (const auto &t : filtered_transitions)
        {
            filtered_runtimes[t] = enabled_runtimes[t];
            const auto &vertex = petri_net[t];
            state_logger->debug("优先级过滤后的使能变迁: {}", vertex.name);
        }

        return std::make_shared<PriorityStateClass>(initial_marking, filtered_runtimes, std::map<ptpn_v_desc, TimeInterval>());
    }

    // 重置 Petri 网到指定的状态类
    void PriorityStateClassGraph::reset_petri_net(const PriorityStateClass &state)
    {
        // 获取标记和运行时间
        const Marking &marking = state.get_marking();
        const auto &enabled_runtimes = state.get_enabled_runtimes();
        const auto &suspended_runtimes = state.get_suspended_runtimes();

        // 首先,将所有库所的token清零,并重置所有变迁的状态
        graph_traits<PriorityTPNGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = vertices(petri_net); vi != vi_end; ++vi)
        {
            if (auto &vertex = petri_net[*vi]; vertex.is_place())
            {
                auto &[token, capacity] = vertex.as_place();
                token = 0;
            }
            else if (vertex.is_transition())
            {
                auto &transition = vertex.as_transition();
                // 注意: runtime 和 handle 是初始化后不应修改的参数
                transition.runtimes = {0, 0}; // 重置运行时间区间
                transition.enable = false;    // 重置使能状态
            }
        }

        // 设置库所的token
        for (const auto &[place, tokens] : marking)
        {
            if (tokens > 0)
            {
                if (auto &vertex = petri_net[place]; vertex.is_place())
                {
                    auto &[token, capacity] = vertex.as_place();
                    token = tokens;
                }
            }
        }

        // 不需要计算是否使能,直接重置时间
        for (const auto &[transition, interval] : enabled_runtimes)
        {
            auto &vertex = petri_net[transition];
            // 注意这里使用单独赋值,而不是整体赋值
            vertex.as_transition().runtimes.first = interval.lower;
            vertex.as_transition().runtimes.second = interval.upper;
        }

        for (const auto &[transition, interval] : suspended_runtimes)
        {
            auto &vertex = petri_net[transition];
            // 注意这里使用单独赋值,而不是整体赋值
            vertex.as_transition().runtimes.first = interval.lower;
            vertex.as_transition().runtimes.second = interval.upper;
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

        // 按照core属性分组
        std::map<int, std::vector<std::pair<ptpn_v_desc, int>>> transitions_by_core;

        for (const auto &t : enabled_transitions)
        {
            if (const auto &vertex = petri_net[t]; vertex.is_transition())
            {
                const auto &transition = vertex.as_transition();
                // 按core分组,每组中保存(变迁, 优先级)对
                transitions_by_core[transition.core].emplace_back(t, transition.priority);
            }
        }

        // 最终过滤后的变迁
        std::vector<ptpn_v_desc> filtered_transitions;

        // 对每个core组,选择最高优先级的变迁
        for (auto &[core, transitions] : transitions_by_core)
        {
            // 按优先级降序排序
            std::sort(transitions.begin(), transitions.end(),
                      [](const auto &a, const auto &b)
                      {
                          return a.second > b.second; // 优先级高排前面
                      });

            // 找出当前core组的最高优先级
            int highest_priority = transitions.front().second;

            // 添加所有具有最高优先级的变迁
            for (const auto &[t, priority] : transitions)
            {
                if (priority == highest_priority)
                {
                    filtered_transitions.push_back(t);
                }
                else
                {
                    break;
                }
            }
        }

        return filtered_transitions;
    }

    // 计算变迁的时间区间
    TimeInterval PriorityStateClassGraph::compute_time_interval(
        const PriorityStateClass &state, const ptpn_v_desc transition)
    {
        // 如果变迁不是使能的,返回无效区间
        if (!state.is_transition_enabled(transition))
        {
            return TimeIntervalT<int>{1, 0}; // 下界大于上界,表示无效区间
        }

        // 获取变迁的静态时间约束和当前已运行时间
        const auto &vertex = petri_net[transition];
        const auto &transition_node = vertex.as_transition();
        const auto &[fst, snd] = transition_node.const_time;
        const auto &runtime = state.get_enabled_runtime(transition);

        // 计算可发生时间区间:const_time - runtime
        // 下界 = max(0, const_time.first - runtime.second)
        // 上界 = const_time.second - runtime.first
        int lower = std::max(0, fst - runtime.upper);
        int upper = snd - runtime.lower;

        // 确保区间有效
        if (upper < lower)
        {
            state_logger->warn("变迁 {} 的可发生时间区间无效: [{},{}]", vertex.name, lower, upper);
            return TimeIntervalT<int>{1, 0}; // 无效区间
        }

        const TimeInterval interval(lower, upper);
        state_logger->debug("变迁 {} 的可发生时间区间: {}", vertex.name, interval.to_string());
        return interval;
    }

    // 添加状态类到图中
    SCGVertex PriorityStateClassGraph::add_state(const PriorityStateClass &state)
    {
        // 计算状态的哈希值,用于快速查找
        const std::size_t state_hash = state.hash();

        // 查找状态是否已在图中
        if (const auto it = state_vertex_map.find(state_hash); it != state_vertex_map.end())
        {
            // 验证状态是否真的相同(因为可能有哈希碰撞)
            if (const SCGVertex existing_vertex = it->second; *graph[existing_vertex].state == state)
            {
                return existing_vertex;
            }
        }

        // 创建新状态类并添加到图中
        const SCGVertex new_vertex = boost::add_vertex(graph);

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
        const SCGVertex from, const SCGVertex to, const ptpn_v_desc transition, const TimeInterval &interval)
    {
        // 检查边是否已存在
        graph_traits<StateClassGraph>::out_edge_iterator ei, ei_end;
        for (boost::tie(ei, ei_end) = out_edges(from, graph); ei != ei_end; ++ei)
        {
            if (boost::target(*ei, graph) == to && graph[*ei].transition == transition)
            {
                // 边已存在,更新时间区间(取交集)
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
        reset_petri_net(state);

        std::vector<ptpn_v_desc> enabled_transitions;
        graph_traits<PriorityTPNGraph>::vertex_iterator vi, vi_end;

        for (boost::tie(vi, vi_end) = vertices(petri_net); vi != vi_end; ++vi)
        {
            if (auto &vertex = petri_net[*vi]; vertex.is_transition())
            {
                // 检查变迁的所有前置库所是否都有足够的token
                bool is_enabled = true;
                graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;

                for (boost::tie(ei, ei_end) = in_edges(*vi, petri_net); ei != ei_end; ++ei)
                {
                    const auto source = boost::source(*ei, petri_net);
                    const auto &[label, weight] = petri_net[*ei];

                    if (const auto &source_vertex = petri_net[source]; source_vertex.is_place())
                    {
                        // 如果前置库所的token小于边的权重,则变迁不使能
                        if (const auto &[token, capacity] = source_vertex.as_place(); token < weight)
                        {
                            is_enabled = false;
                            break;
                        }
                    }
                }

                if (is_enabled)
                {
                    vertex.as_transition().enable = true;
                    enabled_transitions.push_back(*vi);
                }
            }
        }

        return enabled_transitions;
    }

    // 变迁触发后得到的后继状态
    PriorityStateClass PriorityStateClassGraph::fire_transition(
        const PriorityStateClass &state, ptpn_v_desc transition, const TimeInterval &common_interval)
    {
        // 重置Petri网到当前状态
        reset_petri_net(state);

        Marking new_marking = state.get_marking();

        // 移除输入库所的token
        graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;
        for (boost::tie(ei, ei_end) = in_edges(transition, petri_net); ei != ei_end; ++ei)
        {
            auto source = boost::source(*ei, petri_net);
            const auto &[label, weight] = petri_net[*ei];

            if (auto &source_vertex = petri_net[source]; source_vertex.is_place())
            {
                auto &[token, capacity] = source_vertex.as_place();
                token -= weight;
                if (token > 0)
                {
                    new_marking[source] = token;
                }
                else
                {
                    new_marking.erase(source);
                }
            }
        }

        // 添加输出库所的token
        graph_traits<PriorityTPNGraph>::out_edge_iterator eo, eo_end;
        for (boost::tie(eo, eo_end) = out_edges(transition, petri_net); eo != eo_end; ++eo)
        {
            auto target = boost::target(*eo, petri_net);
            const auto &[label, weight] = petri_net[*eo];

            if (auto &target_vertex = petri_net[target]; target_vertex.is_place())
            {
                auto &[token, capacity] = target_vertex.as_place();
                token += weight;
                if (token > capacity)
                {
                    token = capacity;
                }
                new_marking[target] = token;
                BOOST_ASSERT(token <= capacity);
            }
        }

        PriorityStateClass temp_state(new_marking, std::map<ptpn_v_desc, TimeInterval>(), std::map<ptpn_v_desc, TimeInterval>());

        // 重置Petri网到新状态
        reset_petri_net(temp_state);

        // 计算新使能变迁
        auto enabled_transitions = compute_enabled_transitions(temp_state);
        state_logger->debug("新状态下有 {} 个使能变迁", enabled_transitions.size());
        for (const auto &t : enabled_transitions)
        {
            state_logger->debug("新状态下的使能变迁: {}, 优先级: {}, cpu: {}", petri_net[t].name, petri_net[t].as_transition().priority, petri_net[t].as_transition().core);
        }

        auto filtered_transitions = filter_by_priority(enabled_transitions);
        state_logger->debug("优先级过滤后有 {} 个使能变迁", filtered_transitions.size());
        for (const auto &t : filtered_transitions)
        {
            state_logger->debug("优先级过滤后的使能变迁: {}, 优先级: {}, cpu: {}", petri_net[t].name, petri_net[t].as_transition().priority, petri_net[t].as_transition().core);
        }

        // 获取所有使能变迁的时间区间
        std::map<ptpn_v_desc, TimeInterval> new_enabled_runtimes;
        std::map<ptpn_v_desc, TimeInterval> new_suspended_runtimes;

        // 处理新使能的变迁
        for (const auto &t : filtered_transitions)
        {
            // 获取变迁在Petri网中的时间约束
            const auto &vertex = petri_net[t];
            const auto &transition_node = vertex.as_transition();

            // 如果变迁在原状态中是使能的且不是刚刚触发的变迁
            if (state.is_transition_enabled(t) && t != transition)
            {
                // 获取原始时间区间
                TimeInterval original_interval = state.get_enabled_runtime(t);

                // 计算更新后的时间区间,考虑公共调度区间的影响
                // 因为时间过去了[common_interval.lower, common_interval.upper]的时间
                int new_lower = original_interval.lower + common_interval.lower;
                int new_upper = original_interval.upper + common_interval.upper;

                // 确保上界不超过变迁的静态时间约束
                if (new_upper > transition_node.const_time.second)
                {
                    new_upper = transition_node.const_time.second;
                }

                TimeInterval updated_interval(new_lower, new_upper);
                new_enabled_runtimes[t] = updated_interval;

                state_logger->debug("变迁 {} 在经过时间区间 {} 后更新为时间区间: {}",
                                    vertex.name, common_interval.to_string(), updated_interval.to_string());
            }
            // 如果变迁在原状态中是挂起的,现在变为使能
            else if (state.is_transition_suspended(t))
            {
                // 使用挂起变迁的时间区间,不增加时间
                new_enabled_runtimes[t] = state.get_suspended_runtime(t);
                state_logger->debug("挂起的变迁 {} 恢复使能,使用原挂起时间区间: {}",
                                    vertex.name, state.get_suspended_runtime(t).to_string());
            }
            // 如果是新使能的变迁或刚刚触发的变迁,重置运行时间为0
            else
            {
                // 新使能的变迁,设置初始时间区间
                TimeInterval interval(0, 0);
                new_enabled_runtimes[t] = interval;
                state_logger->debug("新使能的变迁 {} 重置运行时间为0", vertex.name);
            }
        }

        // 处理需要挂起的变迁
        for (const auto &[t, _] : state.get_enabled_runtimes())
        {
            // 如果变迁在原状态中是使能的,但在新状态中不再使能且不是刚刚触发的变迁
            if (t != transition &&
                std::find(filtered_transitions.begin(), filtered_transitions.end(), t) == filtered_transitions.end())
            {
                const auto &vertex = petri_net[t];
                const auto &transition_node = vertex.as_transition();

                // 只有可挂起的变迁才会被加入挂起集合
                if (transition_node.handle && transition_node.runtimes.first != 0 && transition_node.runtimes.second != 0)
                {
                    // 变迁被挂起,保持原有时间区间
                    new_suspended_runtimes[t] = state.get_enabled_runtime(t);
                    state_logger->debug("变迁 {} 被挂起,保持原有时间区间: {}",
                                        vertex.name, state.get_enabled_runtime(t).to_string());
                }
                else
                {
                    state_logger->debug("变迁 {} 不是可挂起变迁,不加入挂起集合", vertex.name);
                }
            }
        }

        // 对于已挂起的变迁,如果在新状态中仍未使能,则保持挂起状态
        for (const auto &[t, interval] : state.get_suspended_runtimes())
        {
            // 如果这个挂起的变迁在新状态中仍未使能
            if (std::find(filtered_transitions.begin(), filtered_transitions.end(), t) == filtered_transitions.end())
            {
                // 继续保持挂起状态及其时间区间
                new_suspended_runtimes[t] = interval;
                state_logger->debug("挂起的变迁 {} 保持挂起状态,时间区间: {}",
                                    petri_net[t].name, interval.to_string());
            }
            // 注意:如果挂起的变迁变为使能,已在前面的代码中处理
        }

        // 创建新状态类
        return {new_marking, new_enabled_runtimes, new_suspended_runtimes};
    }

    SCGVertex PriorityStateClassGraph::find_state_vertex(const PriorityStateClass &state) const
    {
        const std::size_t state_hash = state.hash();

        if (const auto it = state_vertex_map.find(state_hash); it != state_vertex_map.end())
        {
            if (*graph[it->second].state == state)
            {
                return it->second;
            }
        }

        // 找不到状态,返回无效顶点
        return graph_traits<StateClassGraph>::null_vertex();
    }

    // 计算公共调度区间
    TimeInterval PriorityStateClassGraph::compute_common_firing_interval(
        const std::map<ptpn_v_desc, TimeInterval> &transition_intervals)
    {
        if (transition_intervals.empty())
        {
            state_logger->warn("没有变迁区间可计算,返回默认公共调度区间 [0, 0]");
            return TimeIntervalT<int>{0, 0};
        }

        state_logger->debug("计算公共调度区间,共 {} 个变迁区间", transition_intervals.size());

        // 存储所有可发生区间下界和上界
        std::vector<std::pair<int, int>> valid_intervals;

        // 收集所有有效的变迁区间
        for (const auto &[t, interval] : transition_intervals)
        {
            state_logger->debug("变迁 {} 的时间区间: {}", petri_net[t].name, interval.to_string());

            // 确保区间有效
            if (interval.is_valid())
            {
                valid_intervals.emplace_back(interval.lower, interval.upper);
            }
            else
            {
                state_logger->warn("变迁 {} 的区间 {} 无效,跳过", petri_net[t].name, interval.to_string());
            }
        }

        if (valid_intervals.empty())
        {
            state_logger->warn("没有有效的变迁区间,返回默认公共调度区间 [0, 0]");
            return TimeIntervalT<int>{0, 0};
        }

        // 对所有可发生区间按下界排序
        std::sort(valid_intervals.begin(), valid_intervals.end());

        // 最小下界和上界
        const int min_lower = valid_intervals[0].first;
        int min_upper = valid_intervals[0].second;

        // 查找可发生区间的重叠部分
        for (const auto &[lower, upper] : valid_intervals)
        {
            if (lower > min_upper)
            {
                // 如果当前区间下界大于已有区间上界,则没有重叠
                // 只考虑第一个变迁的区间
                break;
            }

            // 更新最小上界
            min_upper = std::min(min_upper, upper);
        }

        // 创建公共调度区间
        const TimeInterval common_interval(min_lower, min_upper);
        state_logger->debug("计算得到的公共调度区间: {}", common_interval.to_string());

        return common_interval;
    }

    // 生成状态类图
    void PriorityStateClassGraph::generate_state_class_graph()
    {
        state_logger->info("开始生成状态类图...");

        graph.clear();
        state_vertex_map.clear();

        // 获取初始状态类
        auto initial_state = get_initial_state_class();
        if (!initial_state)
        {
            state_logger->error("无法获取初始状态类");
            return;
        }

        state_logger->debug(initial_state->to_string());

        SCGVertex initial_vertex = add_state(*initial_state);

        std::queue<SCGVertex> vertex_queue;
        vertex_queue.push(initial_vertex);

        int processed_states = 0;
        std::unordered_set<SCGVertex> processed_vertices;

        while (!vertex_queue.empty())
        {
            SCGVertex current_vertex = vertex_queue.front();
            vertex_queue.pop();

            if (processed_vertices.find(current_vertex) != processed_vertices.end())
            {
                continue;
            }

            processed_vertices.insert(current_vertex);
            const PriorityStateClass &current_state = *graph[current_vertex].state;
            state_logger->debug("当前状态类:{}", current_state.to_string());
            // 记录处理进度
            processed_states++;
            if (processed_states % 100 == 0)
            {
                state_logger->info("已处理 {} 个状态,当前队列大小: {}", processed_states, vertex_queue.size());
            }

            // 计算当前状态下的使能变迁
            auto enabled_transitions = compute_enabled_transitions(current_state);
            // 根据优先级过滤使能变迁
            auto filtered_transitions = filter_by_priority(enabled_transitions);

            // 如果没有使能变迁,则处理下一个状态
            if (filtered_transitions.empty())
            {
                state_logger->debug("状态 {} 没有使能变迁", graph[current_vertex].id);
                continue;
            }

            // 计算所有使能变迁的时间区间
            std::map<ptpn_v_desc, TimeInterval> transition_intervals;
            for (const auto &t : filtered_transitions)
            {
                TimeInterval interval = compute_time_interval(current_state, t);
                if (interval.is_valid())
                {
                    transition_intervals[t] = interval;
                    state_logger->debug("变迁 {} 的时间区间: {}", petri_net[t].name, interval.to_string());
                }
                else
                {
                    state_logger->warn("变迁 {} 的时间区间是:{},无效,跳过", petri_net[t].name, interval.to_string());
                }
            }

            // 计算公共调度区间
            TimeInterval common_interval = compute_common_firing_interval(transition_intervals);
            state_logger->debug("公共调度区间: {}", common_interval.to_string());

            // 筛选在公共区间内可调度的变迁
            std::vector<ptpn_v_desc> schedulable_transitions;
            for (const auto &[t, interval] : transition_intervals)
            {
                if (interval.lower <= common_interval.upper)
                {
                    schedulable_transitions.push_back(t);
                    state_logger->debug("可调度变迁: {}", petri_net[t].name);
                }
            }

            // 对每个可调度变迁生成后继状态
            for (const auto &t : schedulable_transitions)
            {
                // 获取变迁的时间区间
                TimeInterval interval = transition_intervals[t];

                // 计算实际发生区间(与公共区间的交集)
                TimeInterval firing_interval = interval.intersect(common_interval);
                state_logger->debug("变迁 {} 的实际发生区间: {}", petri_net[t].name, firing_interval.to_string());

                // 触发变迁得到后继状态
                PriorityStateClass successor_state = fire_transition(current_state, t, common_interval);

                // 检查后继状态是否有效
                if (!successor_state.is_valid())
                {
                    state_logger->debug("变迁 {} 触发后的状态无效,跳过", petri_net[t].name);
                    continue;
                }

                // 添加后继状态到图中
                SCGVertex successor_vertex = add_state(successor_state);

                state_logger->debug("后继状态类: {}", successor_state.to_string());
                // 添加边
                add_edge(current_vertex, successor_vertex, t, firing_interval);

                // 检查状态是否需要加入队列
                if (processed_vertices.find(successor_vertex) == processed_vertices.end())
                {
                    // 将新状态加入队列
                    vertex_queue.push(successor_vertex);
                    state_logger->debug("将后继状态 S{} 加入队列", successor_vertex);
                }
                else
                {
                    state_logger->debug("后继状态 S{} 已处理,不再加入队列", successor_vertex);
                }
            }
        }

        state_logger->info("状态类图生成完成,共 {} 个状态,{} 条边",
                           num_vertices(graph), num_edges(graph));
    }

    // 生成状态类图(带状态数限制)
    void PriorityStateClassGraph::generate_state_class_graph_with_limit(size_t max_states)
    {
        state_logger->info("开始生成状态类图(最大状态数限制: {})...", max_states);

        graph.clear();
        state_vertex_map.clear();

        // 获取初始状态类
        auto initial_state = get_initial_state_class();
        if (!initial_state)
        {
            state_logger->error("无法获取初始状态类");
            return;
        }

        state_logger->debug(initial_state->to_string());

        SCGVertex initial_vertex = add_state(*initial_state);

        std::queue<SCGVertex> vertex_queue;
        vertex_queue.push(initial_vertex);

        int processed_states = 0;
        std::unordered_set<SCGVertex> processed_vertices;

        while (!vertex_queue.empty() && processed_states < max_states)
        {
            SCGVertex current_vertex = vertex_queue.front();
            vertex_queue.pop();

            if (processed_vertices.find(current_vertex) != processed_vertices.end())
            {
                continue;
            }

            processed_vertices.insert(current_vertex);
            const PriorityStateClass &current_state = *graph[current_vertex].state;
            state_logger->debug("当前状态类:{}", current_state.to_string());
            // 记录处理进度
            processed_states++;
            if (processed_states % 50 == 0)
            {
                state_logger->info("已处理 {} 个状态,当前队列大小: {}", processed_states, vertex_queue.size());
            }

            // 检查是否达到状态数限制
            if (processed_states >= max_states)
            {
                state_logger->warn("已达到最大状态数限制 {},停止生成", max_states);
                break;
            }

            // 计算当前状态下的使能变迁
            auto enabled_transitions = compute_enabled_transitions(current_state);
            // 根据优先级过滤使能变迁
            auto filtered_transitions = filter_by_priority(enabled_transitions);

            // 如果没有使能变迁,则处理下一个状态
            if (filtered_transitions.empty())
            {
                state_logger->debug("状态 {} 没有使能变迁", graph[current_vertex].id);
                continue;
            }

            // 计算所有使能变迁的时间区间
            std::map<ptpn_v_desc, TimeInterval> transition_intervals;
            for (const auto &t : filtered_transitions)
            {
                TimeInterval interval = compute_time_interval(current_state, t);
                if (interval.is_valid())
                {
                    transition_intervals[t] = interval;
                    state_logger->debug("变迁 {} 的时间区间: {}", petri_net[t].name, interval.to_string());
                }
                else
                {
                    state_logger->warn("变迁 {} 的时间区间是:{},无效,跳过", petri_net[t].name, interval.to_string());
                }
            }

            // 计算公共调度区间
            TimeInterval common_interval = compute_common_firing_interval(transition_intervals);
            state_logger->debug("公共调度区间: {}", common_interval.to_string());

            // 筛选在公共区间内可调度的变迁
            std::vector<ptpn_v_desc> schedulable_transitions;
            for (const auto &[t, interval] : transition_intervals)
            {
                if (interval.lower <= common_interval.upper)
                {
                    schedulable_transitions.push_back(t);
                    state_logger->debug("可调度变迁: {}", petri_net[t].name);
                }
            }

            // 对每个可调度变迁生成后继状态
            for (const auto &t : schedulable_transitions)
            {
                // 检查是否达到状态数限制
                if (processed_states >= max_states)
                {
                    state_logger->warn("已达到最大状态数限制 {},停止生成", max_states);
                    break;
                }

                // 获取变迁的时间区间
                TimeInterval interval = transition_intervals[t];

                // 计算实际发生区间(与公共区间的交集)
                TimeInterval firing_interval = interval.intersect(common_interval);
                state_logger->debug("变迁 {} 的实际发生区间: {}", petri_net[t].name, firing_interval.to_string());

                // 触发变迁得到后继状态
                PriorityStateClass successor_state = fire_transition(current_state, t, common_interval);

                // 检查后继状态是否有效
                if (!successor_state.is_valid())
                {
                    state_logger->debug("变迁 {} 触发后的状态无效,跳过", petri_net[t].name);
                    continue;
                }

                // 添加后继状态到图中
                SCGVertex successor_vertex = add_state(successor_state);

                state_logger->debug("后继状态类: {}", successor_state.to_string());
                // 添加边
                add_edge(current_vertex, successor_vertex, t, firing_interval);

                // 检查状态是否需要加入队列
                if (processed_vertices.find(successor_vertex) == processed_vertices.end())
                {
                    // 将新状态加入队列
                    vertex_queue.push(successor_vertex);
                    state_logger->debug("将后继状态 S{} 加入队列", successor_vertex);
                }
                else
                {
                    state_logger->debug("后继状态 S{} 已处理,不再加入队列", successor_vertex);
                }
            }
        }

        if (processed_states >= max_states)
        {
            state_logger->warn("状态类图生成因达到最大状态数限制 {} 而停止", max_states);
        }

        state_logger->info("状态类图生成完成,共 {} 个状态,{} 条边",
                           num_vertices(graph), num_edges(graph));
    }

    std::size_t PriorityStateClassGraph::get_vertex_count() const
    {
        return num_vertices(graph);
    }

    std::size_t PriorityStateClassGraph::get_edge_count() const
    {
        return num_edges(graph);
    }

    // 按照TODO要求输出状态类图
    // Node:{标识 markings:[name->token]使能变迁name[time:time];挂起变迁name[time:time]}
    // Edge:{firing transitions: [name->[time,time]]}
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

            // 写入DOT格式头部
            dot_file << "digraph StateClassGraph {\n";
            dot_file << "  rankdir=LR;\n";
            dot_file << "  fontname=\"SimSun\";\n";
            dot_file << "  node [fontname=\"SimSun\", shape=box];\n";
            dot_file << "  edge [fontname=\"SimSun\"];\n\n";

            // 写入节点
            graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
            for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
            {
                const SCGVertex &v = *vi;
                const PriorityStateClass &state = *graph[v].state;

                // 构建节点标签:标识 + markings + 使能变迁 + 挂起变迁
                std::string node_label = graph[v].id + "\\n";

                // 添加markings信息
                node_label += "markings: {";
                bool first = true;
                for (const auto &[place, tokens] : state.get_marking())
                {
                    if (tokens <= 0)
                        continue;
                    if (!first)
                        node_label += ", ";
                    // 获取库所名称
                    std::string place_name = "P" + std::to_string(place);
                    if (place < num_vertices(petri_net))
                    {
                        const auto &vertex = petri_net[place];
                        if (vertex.is_place())
                        {
                            place_name = vertex.name;
                        }
                    }
                    node_label += place_name + "->" + std::to_string(tokens);
                    first = false;
                }
                node_label += "}\\n";

                // 添加使能变迁信息
                const auto &enabled_runtimes = state.get_enabled_runtimes();
                if (!enabled_runtimes.empty())
                {
                    node_label += "使能变迁: ";
                    first = true;
                    for (const auto &[trans, interval] : enabled_runtimes)
                    {
                        if (!first)
                            node_label += ", ";
                        // 获取变迁名称
                        std::string trans_name = "T" + std::to_string(trans);
                        if (trans < num_vertices(petri_net))
                        {
                            const auto &vertex = petri_net[trans];
                            if (vertex.is_transition())
                            {
                                trans_name = vertex.name;
                            }
                        }
                        node_label += trans_name + "[" + interval.to_string() + "]";
                        first = false;
                    }
                    node_label += "\\n";
                }

                // 添加挂起变迁信息
                const auto &suspended_runtimes = state.get_suspended_runtimes();
                if (!suspended_runtimes.empty())
                {
                    node_label += "挂起变迁: ";
                    first = true;
                    for (const auto &[trans, interval] : suspended_runtimes)
                    {
                        if (!first)
                            node_label += ", ";
                        // 获取变迁名称
                        std::string trans_name = "T" + std::to_string(trans);
                        if (trans < num_vertices(petri_net))
                        {
                            const auto &vertex = petri_net[trans];
                            if (vertex.is_transition())
                            {
                                trans_name = vertex.name;
                            }
                        }
                        node_label += trans_name + "[" + interval.to_string() + "]";
                        first = false;
                    }
                    node_label += "\\n";
                }

                dot_file << "  " << graph[v].id << " [label=\"" << node_label << "\"];\n";
            }

            // 写入边
            graph_traits<StateClassGraph>::edge_iterator ei, ei_end;
            for (boost::tie(ei, ei_end) = edges(graph); ei != ei_end; ++ei)
            {
                const SCGEdge &e = *ei;
                SCGVertex source = boost::source(*ei, graph);
                SCGVertex target = boost::target(*ei, graph);

                // 构建边标签:变迁名称 + 时间区间
                std::string edge_label = graph[e].xlabel + "\\n" + graph[e].time_interval.to_string();

                dot_file << "  " << graph[source].id << " -> " << graph[target].id
                         << " [label=\"" << edge_label << "\"];\n";
            }

            dot_file << "}\n";
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

    void PriorityStateClassGraph::print_graph_info() const
    {
        state_logger->info("状态类图信息:");
        state_logger->info("  节点数量: {}", get_vertex_count());
        state_logger->info("  边数量: {}", get_edge_count());

        // 计算终止状态数量(出度为0的节点)
        int terminal_states = 0;
        graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
        {
            if (out_degree(*vi, graph) == 0)
            {
                terminal_states++;
            }
        }

        state_logger->info("  终止状态数量: {}", terminal_states);

        const bool has_deadlock_state = has_deadlock();
        state_logger->info("  是否存在死锁状态: {}", has_deadlock_state ? "是" : "否");

        int max_depth = get_max_depth();
        state_logger->info("  可达性树最大深度: {}", max_depth);
    }

    bool PriorityStateClassGraph::save_to_json(const std::string &filename) const
    {
        try
        {
            std::ofstream json_file(filename);
            if (!json_file)
            {
                state_logger->error("无法打开文件: {}", filename);
                return false;
            }

            json_file << "{\n";
            json_file << "  \"graph\": {\n";
            json_file << "    \"name\": \"StateClassGraph\",\n";
            json_file << "    \"nodes\": [\n";

            // 写入节点信息
            graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
            bool first_node = true;
            for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
            {
                const SCGVertex &v = *vi;
                const PriorityStateClass &state = *graph[v].state;

                if (!first_node)
                    json_file << ",\n";
                first_node = false;

                json_file << "      {\n";
                json_file << R"(        "id": ")" << graph[v].id << "\",\n";

                // 写入markings信息
                json_file << "        \"markings\": {\n";
                bool first_marking = true;
                for (const auto &[place, tokens] : state.get_marking())
                {
                    if (tokens <= 0)
                        continue;
                    if (!first_marking)
                        json_file << ",\n";
                    first_marking = false;

                    // 获取库所名称
                    std::string place_name = "P" + std::to_string(place);
                    if (place < num_vertices(petri_net))
                    {
                        const auto &vertex = petri_net[place];
                        if (vertex.is_place())
                        {
                            place_name = vertex.name;
                        }
                    }
                    json_file << "          \"" << place_name << "\": " << tokens;
                }
                json_file << "\n        },\n";

                // 写入使能变迁信息
                const auto &enabled_runtimes = state.get_enabled_runtimes();
                json_file << "        \"enabled_transitions\": {\n";
                bool first_enabled = true;
                for (const auto &[trans, interval] : enabled_runtimes)
                {
                    if (!first_enabled)
                        json_file << ",\n";
                    first_enabled = false;

                    // 获取变迁名称
                    std::string trans_name = "T" + std::to_string(trans);
                    if (trans < num_vertices(petri_net))
                    {
                        const auto &vertex = petri_net[trans];
                        if (vertex.is_transition())
                        {
                            trans_name = vertex.name;
                        }
                    }
                    json_file << "          \"" << trans_name << "\": \"" << interval.to_string() << "\"";
                }
                json_file << "\n        },\n";

                // 写入挂起变迁信息
                const auto &suspended_runtimes = state.get_suspended_runtimes();
                json_file << "        \"suspended_transitions\": {\n";
                bool first_suspended = true;
                for (const auto &[trans, interval] : suspended_runtimes)
                {
                    if (!first_suspended)
                        json_file << ",\n";
                    first_suspended = false;

                    // 获取变迁名称
                    std::string trans_name = "T" + std::to_string(trans);
                    if (trans < num_vertices(petri_net))
                    {
                        const auto &vertex = petri_net[trans];
                        if (vertex.is_transition())
                        {
                            trans_name = vertex.name;
                        }
                    }
                    json_file << "          \"" << trans_name << "\": \"" << interval.to_string() << "\"";
                }
                json_file << "\n        }\n";
                json_file << "      }";
            }

            json_file << "\n    ],\n";
            json_file << "    \"edges\": [\n";

            // 写入边信息
            graph_traits<StateClassGraph>::edge_iterator ei, ei_end;
            bool first_edge = true;
            for (boost::tie(ei, ei_end) = edges(graph); ei != ei_end; ++ei)
            {
                const SCGEdge &e = *ei;
                SCGVertex source = boost::source(*ei, graph);
                SCGVertex target = boost::target(*ei, graph);

                if (!first_edge)
                    json_file << ",\n";
                first_edge = false;

                json_file << "      {\n";
                json_file << R"(        "source": ")" << graph[source].id << "\",\n";
                json_file << R"(        "target": ")" << graph[target].id << "\",\n";
                json_file << R"(        "transition": ")" << graph[e].xlabel << "\",\n";
                json_file << R"(        "time_interval": ")" << graph[e].time_interval.to_string() << "\"\n";
                json_file << "      }";
            }

            json_file << "\n    ]\n";
            json_file << "  }\n";
            json_file << "}\n";

            json_file.close();
            state_logger->info("状态类图JSON已保存到: {}", filename);
            return true;
        }
        catch (const std::exception &e)
        {
            state_logger->error("保存JSON文件时发生错误: {}", e.what());
            return false;
        }
    }

    bool PriorityStateClassGraph::has_deadlock() const
    {
        // 检查是否有出度为0且不是终止状态的节点
        graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
        {
            if (out_degree(*vi, graph) == 0)
            {
                const PriorityStateClass &state = *graph[*vi].state;
                if (!state.get_enabled_runtimes().empty())
                {
                    // 有使能变迁但没有出边,可能是死锁
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
        // 从初始节点(ID为S0)开始BFS
        constexpr SCGVertex initial_vertex = 0; // 通常初始节点是第一个添加的

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
            graph_traits<StateClassGraph>::out_edge_iterator ei, ei_end;
            for (boost::tie(ei, ei_end) = out_edges(current, graph); ei != ei_end; ++ei)
            {
                SCGVertex target = boost::target(*ei, graph);

                // 如果节点还未访问过,或者找到了更深的路径
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