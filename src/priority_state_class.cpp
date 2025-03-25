#include "priority_state_class.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

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

    bool PriorityStateClass::operator==(const PriorityStateClass &other) const
    {
        // 首先比较标记是否相同
        if (marking.size() != other.marking.size())
        {
            return false;
        }

        for (const auto &p_m : marking)
        {
            // 只比较token数量大于0的库所
            if (p_m.second <= 0)
            {
                continue;
            }

            auto it = other.marking.find(p_m.first);
            if (it == other.marking.end() || it->second != p_m.second)
            {
                return false;
            }
        }

        for (const auto &p_m : other.marking)
        {
            // 只比较token数量大于0的库所
            if (p_m.second <= 0)
            {
                continue;
            }

            auto it = marking.find(p_m.first);
            if (it == marking.end())
            {
                return false;
            }
        }

        // 然后比较时间约束是否相同
        if (time_constraints.size() != other.time_constraints.size())
        {
            return false;
        }

        for (const auto &tc : time_constraints)
        {
            bool found = false;
            for (const auto &other_tc : other.time_constraints)
            {
                if (tc.transition == other_tc.transition &&
                    tc.time_interval == other_tc.time_interval &&
                    tc.priority == other_tc.priority &&
                    tc.cpu == other_tc.cpu)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                return false;
            }
        }

        // 最后比较挂起变迁是否相同
        return suspended_transitions == other.suspended_transitions;
    }

    std::string PriorityStateClass::to_string() const
    {
        std::stringstream ss;

        // 输出标记
        ss << "Marking: {";
        bool first = true;
        for (const auto &[place, tokens] : marking)
        {
            if (!first)
            {
                ss << ", ";
            }
            ss << "p" << place << ":" << tokens;
            first = false;
        }
        ss << "}\n";

        // 输出时间约束
        ss << "Time Constraints: {";
        first = true;
        for (const auto &tc : time_constraints)
        {
            if (!first)
            {
                ss << ", ";
            }
            ss << "t" << tc.transition << ":["
               << tc.time_interval.lower << ","
               << (tc.time_interval.upper == INT_MAX ? "∞" : std::to_string(tc.time_interval.upper))
               << "] (priority:" << tc.priority << ", cpu:" << tc.cpu << ")";
            first = false;
        }
        ss << "}\n";

        // 输出挂起变迁及其时钟
        if (!suspended_transitions.empty())
        {
            ss << "Suspended Transitions: {";
            bool first = true;
            for (const auto &t : suspended_transitions)
            {
                if (!first)
                {
                    ss << ", ";
                }
                auto clock = suspended_transitions_clocks.find(t);
                ss << "t" << t;
                if (clock != suspended_transitions_clocks.end())
                {
                    ss << ":[" << clock->second.lower << ","
                       << (clock->second.upper == INT_MAX ? "∞" : std::to_string(clock->second.upper))
                       << "]";
                }
                first = false;
            }
            ss << "}";
        }

        return ss.str();
    }

    std::vector<ptpn_v_desc> PriorityStateClass::get_enabled_transitions(const PriorityTPNGraph &graph) const
    {
        std::vector<ptpn_v_desc> enabled_transitions;
        boost::graph_traits<PriorityTPNGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
        {
            ptpn_v_desc v = *vi;

            const Vertex &vertex = graph[v];
            if (!vertex.is_transition())
            {
                continue;
            }

            bool is_enabled = true;
            boost::graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;
            for (boost::tie(ei, ei_end) = in_edges(v, graph); ei != ei_end; ++ei)
            {
                ptpn_v_desc source = boost::source(*ei, graph);
                const Vertex &source_vertex = graph[source];

                if (!source_vertex.is_place())
                {
                    continue;
                }

                int weight = graph[*ei].weight;

                auto it = marking.find(source);
                if (it == marking.end() || it->second < weight)
                {
                    is_enabled = false;
                    break;
                }
            }

            if (is_enabled)
            {
                enabled_transitions.push_back(v);
            }
        }

        return enabled_transitions;
    }

    PriorityStateClass::PriorityFilterResult PriorityStateClass::filter_by_priority(
        const std::vector<ptpn_v_desc> &enabled_transitions,
        const PriorityTPNGraph &graph) const
    {
        PriorityFilterResult result;

        if (enabled_transitions.empty())
        {
            return result;
        }

        // 按CPU对变迁进行分组
        std::map<int, std::vector<ptpn_v_desc>> cpu_groups;
        for (const auto &transition : enabled_transitions)
        {
            const Vertex &vertex = graph[transition];
            const Transition &t = vertex.as_transition();
            cpu_groups[t.core].push_back(transition);
        }

        // 对每个CPU组进行优先级过滤
        for (auto &[cpu_id, transitions] : cpu_groups)
        {
            // 如果只有一个变迁，直接添加到启用列表
            if (transitions.size() == 1)
            {
                result.enabled_transitions.push_back(transitions[0]);
                continue;
            }

            int highest_priority = 0;
            for (const auto &t : transitions)
            {
                const Transition &trans = graph[t].as_transition();
                highest_priority = std::max(highest_priority, trans.priority);
            }
            for (const auto &t : transitions)
            {
                const Vertex &vertex = graph[t];
                const Transition &trans = vertex.as_transition();

                if (trans.priority >= highest_priority)
                {
                    result.enabled_transitions.push_back(t);
                }
                // 可挂起的低优先级变迁放入挂起列表
                else if (trans.handle)
                {
                    result.suspended_transitions.push_back(t);
                }
                // 不可挂起的低优先级变迁忽略
            }
        }

        return result;
    }

    std::shared_ptr<PriorityStateClass> PriorityStateClass::compute_successor(
        const PriorityTPNGraph &graph,
        ptpn_v_desc fired_transition) const
    {
        // 获取当前变迁的信息
        const Vertex &transition_vertex = graph[fired_transition];
        const Transition &transition = transition_vertex.as_transition();

        // 计算新的标记
        Marking new_marking = marking;

        // 移除输入库所的token
        boost::graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;
        for (boost::tie(ei, ei_end) = in_edges(fired_transition, graph); ei != ei_end; ++ei)
        {
            ptpn_v_desc source = boost::source(*ei, graph);
            const Vertex &source_vertex = graph[source];

            if (source_vertex.is_place())
            {
                int weight = graph[*ei].weight;
                new_marking[source] -= weight;

                // 如果token数量小于等于0，从标记中移除该库所
                // 正常情况下不应该出现负数标记，如果出现则记录警告
                if (new_marking[source] < 0)
                {
                    state_logger->warn("计算后继状态时出现负数标记: p{}:{}", source, new_marking[source]);
                }

                if (new_marking[source] <= 0)
                {
                    new_marking.erase(source);
                }
            }
        }

        // 添加输出库所的token
        boost::graph_traits<PriorityTPNGraph>::out_edge_iterator oe, oe_end;
        for (boost::tie(oe, oe_end) = out_edges(fired_transition, graph); oe != oe_end; ++oe)
        {
            ptpn_v_desc target = boost::target(*oe, graph);
            const Vertex &target_vertex = graph[target];

            if (target_vertex.is_place())
            {
                int weight = graph[*oe].weight;
                new_marking[target] += weight;

                // 确保添加的token为正数
                assert(new_marking[target] > 0 && "添加token后标记数应为正数");
            }
        }

        // 计算新的时间约束
        std::vector<TransitionTimeConstraint> new_constraints;

        // 创建新的挂起变迁集合（从当前状态复制）
        std::set<ptpn_v_desc> new_suspended = suspended_transitions;

        // 如果触发的变迁是挂起状态，从挂起集合中移除
        if (is_suspended(fired_transition))
        {
            new_suspended.erase(fired_transition);
        }

        // 获取新状态中启用的变迁
        std::vector<ptpn_v_desc> new_enabled_transitions;
        boost::graph_traits<PriorityTPNGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
        {
            ptpn_v_desc v = *vi;

            // 只检查类型为变迁的顶点
            const Vertex &vertex = graph[v];
            if (!vertex.is_transition())
            {
                continue;
            }

            // 检查该变迁的所有输入库所是否都有足够的token
            bool is_enabled = true;
            boost::graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;
            for (boost::tie(ei, ei_end) = in_edges(v, graph); ei != ei_end; ++ei)
            {
                ptpn_v_desc source = boost::source(*ei, graph);

                // 检查该库所是否在新标记中，且有足够的token
                int weight = graph[*ei].weight;
                auto it = new_marking.find(source);
                if (it == new_marking.end() || it->second < weight)
                {
                    is_enabled = false;
                    break;
                }
            }

            if (is_enabled)
            {
                new_enabled_transitions.push_back(v);

                // 计算该变迁的时间约束
                const Transition &t = vertex.as_transition();
                TimeInterval initial_interval(t.const_time.first, t.const_time.second);

                // 对于新启用的变迁或从挂起中恢复的变迁，使用初始时间约束
                bool newly_enabled = true;
                for (const auto &tc : time_constraints)
                {
                    if (tc.transition == v)
                    {
                        newly_enabled = false;

                        // 如果不是刚触发的变迁，并且不是挂起状态或不再挂起，则需要根据触发变迁的时间调整时间约束
                        if (v != fired_transition && !(is_suspended(v) && new_suspended.find(v) != new_suspended.end()))
                        {
                            // 获取触发变迁的时间（lower值）
                            TimeInterval firing_time(0, 0);
                            for (const auto &ftc : time_constraints)
                            {
                                if (ftc.transition == fired_transition)
                                {
                                    firing_time.lower = ftc.time_interval.lower;
                                    break;
                                }
                            }

                            // 更新时间约束，减去触发时间
                            TimeInterval updated_interval = tc.time_interval;
                            updated_interval.lower = std::max(0, updated_interval.lower - firing_time.lower);
                            if (updated_interval.upper != INT_MAX)
                            {
                                updated_interval.upper = updated_interval.upper - firing_time.lower;
                            }

                            // 确保更新后的区间合法
                            if (updated_interval.is_valid())
                            {
                                new_constraints.push_back(TransitionTimeConstraint(v, updated_interval, tc.priority, tc.cpu));
                            }
                            else
                            {
                                state_logger->warn("变迁 t{} 更新后的时间区间 [{}, {}] 无效",
                                                   v, updated_interval.lower, updated_interval.upper);
                            }
                        }
                        // 如果是挂起状态且仍然挂起，保持原时间约束
                        else if (is_suspended(v) && new_suspended.find(v) != new_suspended.end())
                        {
                            new_constraints.push_back(tc);
                        }

                        break;
                    }
                }

                // 如果是新启用的变迁或者从挂起状态恢复的变迁，使用初始时间约束
                if (newly_enabled || (v == fired_transition && is_suspended(v)))
                {
                    new_constraints.push_back(TransitionTimeConstraint(v, initial_interval, t.priority, t.core));
                }
            }
        }

        // 创建新的状态类
        auto successor = std::make_shared<PriorityStateClass>(new_marking, new_constraints);
        successor->suspended_transitions = new_suspended;
        return successor;
    }

    void PriorityStateClass::mark_suspended(ptpn_v_desc transition, const TimeInterval &clock_time)
    {
        suspended_transitions.insert(transition);
        suspended_transitions_clocks[transition] = clock_time;
    }

    bool PriorityStateClass::is_suspended(ptpn_v_desc transition) const
    {
        return suspended_transitions.find(transition) != suspended_transitions.end();
    }

    // PriorityStateClassAnalyzer 方法实现
    PriorityStateClassAnalyzer::PriorityStateClassAnalyzer(const PriorityTPNGraph &petri_net)
        : petri_net(petri_net)
    {
        // 初始化状态类图
        graph[boost::graph_bundle].name = "优先级时间Petri网状态类图";
    }

    void PriorityStateClassAnalyzer::generate_state_class_graph()
    {
        // 计算初始状态类
        auto initial_state = compute_initial_state();
        if (!initial_state)
        {
            state_logger->error("无法计算初始状态类");
            return;
        }

        state_logger->info("初始状态类: \n{}", initial_state->to_string());

        // 添加初始状态到状态类图
        SCGVertex initial_vertex = add_state(initial_state);
        std::queue<std::pair<SCGVertex, std::shared_ptr<PriorityStateClass>>> queue;
        std::unordered_map<std::string, SCGVertex> state_map;

        queue.push({initial_vertex, initial_state});
        state_map[initial_state->to_string()] = initial_vertex;

        int state_counter = 0;
        while (!queue.empty())
        {
            auto [current_vertex, current_state] = queue.front();
            queue.pop();
            state_counter++;

            state_logger->debug("\n处理状态 #{}: {}", state_counter, current_state->to_string());

            // 检查标记中是否有非法值（负数或零值）
            for (const auto &[place, tokens] : current_state->marking)
            {
                if (tokens <= 0)
                {
                    state_logger->error("状态 #{} 中发现非法标记: p{}:{} <= 0",
                                        state_counter, place, tokens);
                    assert(tokens > 0 && "标记中出现负数或零值，这是不合法的");
                    continue; // 跳过当前状态
                }
            }

            auto enabled_transitions = current_state->get_enabled_transitions(petri_net);
            state_logger->debug("启用的变迁数量: {}", enabled_transitions.size());
            for (const auto &t : enabled_transitions)
            {
                const Vertex &v = petri_net[t];
                state_logger->debug("  变迁 t{} ({}): 优先级={}, CPU={}",
                                    t, v.name, v.as_transition().priority, v.as_transition().core);
            }

            auto filter_result = current_state->filter_by_priority(enabled_transitions, petri_net);
            state_logger->debug("优先级过滤后的变迁数量: {}", filter_result.enabled_transitions.size());
            state_logger->debug("挂起的变迁数量: {}", filter_result.suspended_transitions.size());

            if (filter_result.enabled_transitions.empty())
            {
                state_logger->warn("没有可用变迁，跳过当前状态");
                continue;
            }

            // 计算时间区间和可调度变迁
            TimeInterval common_interval(0, 0);
            auto schedulable_transitions = current_state->get_schedulable_transitions(
                filter_result.enabled_transitions, petri_net, common_interval);

            // 如果没有可调度变迁或时间区间无效，跳过当前状态
            if (schedulable_transitions.empty() || !common_interval.is_valid())
            {
                state_logger->warn("没有可调度变迁或时间区间无效，跳过当前状态");
                continue;
            }

            state_logger->debug("可调度变迁数量: {}", schedulable_transitions.size());
            state_logger->debug("状态间链接边权重 (common_interval): [{}, {}]",
                                common_interval.lower,
                                common_interval.upper == INT_MAX ? "∞" : std::to_string(common_interval.upper));

            // 处理每个可调度的变迁
            for (const auto &transition : schedulable_transitions)
            {
                const Vertex &vertex = petri_net[transition];
                const Transition &t = vertex.as_transition();

                state_logger->debug("处理变迁 t{} ({}): 优先级={}, CPU={}, 可挂起={}",
                                    transition, vertex.name, t.priority, t.core, t.handle ? "是" : "否");

                // 计算后继状态
                auto successor_state = current_state->compute_successor(petri_net, transition);

                // 检查后继状态的标记是否合法
                bool has_invalid_marking = false;
                for (const auto &[place, tokens] : successor_state->marking)
                {
                    if (tokens <= 0)
                    {
                        state_logger->error("后继状态中发现非法标记: p{}:{} <= 0", place, tokens);
                        has_invalid_marking = true;
                        break;
                    }
                }

                if (has_invalid_marking)
                {
                    state_logger->error("跳过触发变迁 t{} 生成的后继状态，因为它产生了非法标记", transition);
                    continue;
                }

                // 更新触发变迁的runtimes（已在计算后继状态时完成）
                auto successor_key = successor_state->to_string();
                SCGVertex successor_vertex;

                auto it = state_map.find(successor_key);
                if (it == state_map.end())
                {
                    successor_vertex = add_state(successor_state);
                    state_map[successor_key] = successor_vertex;
                    queue.push({successor_vertex, successor_state});
                }
                else
                {
                    successor_vertex = it->second;
                }

                // 添加边，使用 common_interval 作为边的权重
                add_edge(current_vertex, successor_vertex, transition, common_interval);
            }
        }

        state_logger->info("状态类图生成完成，共有 {} 个状态和 {} 个转换",
                           num_vertices(graph), num_edges(graph));
    }

    void PriorityStateClassAnalyzer::export_to_dot(const std::string &filename)
    {
        std::ofstream dot_file(filename);
        if (!dot_file.is_open())
        {
            state_logger->error("无法打开文件: {}", filename);
            return;
        }

        // 创建动态属性映射
        boost::dynamic_properties dp;

        // 添加顶点属性
        dp.property("label", get(&SCGVertexProperties::label, graph));
        dp.property("node_id", get(&SCGVertexProperties::id, graph));

        // 添加边属性
        dp.property("label", get(&SCGEdgeProperties::xlabel, graph));

        // 写入DOT文件
        boost::write_graphviz_dp(dot_file, graph, dp);

        state_logger->info("状态类图已导出到: {}", filename);
    }

    bool PriorityStateClassAnalyzer::has_deadlock_states() const
    {
        // 检查是否有没有出边的状态（死锁状态）
        boost::graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
        {
            if (out_degree(*vi, graph) == 0)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<SCGVertex> PriorityStateClassAnalyzer::get_deadlock_states() const
    {
        std::vector<SCGVertex> deadlock_states;

        // 查找所有没有出边的状态（死锁状态）
        boost::graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
        {
            if (out_degree(*vi, graph) == 0)
            {
                deadlock_states.push_back(*vi);
            }
        }

        return deadlock_states;
    }

    TimeInterval PriorityStateClassAnalyzer::calculate_max_execution_time() const
    {
        // 找出所有终止状态（没有出边的状态）
        std::vector<SCGVertex> terminal_states = get_deadlock_states();

        if (terminal_states.empty())
        {
            // 如果没有终止状态，返回无界区间
            return TimeInterval(0, INT_MAX);
        }

        int min_time = INT_MAX;
        int max_time = 0;

        // 对每个终止状态，计算从初始状态到达它的最短和最长路径
        SCGVertex initial_vertex = *(vertices(graph).first); // 假设第一个顶点是初始状态

        for (const auto &terminal : terminal_states)
        {
            // 这里应该实现最短路径和最长路径算法
            // 为简化示例，我们假设每个边的权重就是其时间区间
            // 实际上需要使用图算法来计算路径

            // 这里只是占位符，实际实现应该计算真正的路径时间
            min_time = std::min(min_time, 10);
            max_time = std::max(max_time, 100);
        }

        return TimeInterval(min_time, max_time);
    }

    bool PriorityStateClassAnalyzer::is_marking_reachable(const Marking &target_marking) const
    {
        // 检查状态类图中是否存在一个状态，其标记与目标标记匹配
        boost::graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
        {
            const auto &state = graph[*vi].state;

            // 比较标记
            bool matching = true;
            for (const auto &[place, tokens] : target_marking)
            {
                // 检查状态的标记中是否有相同数量的token
                auto it = state->marking.find(place);
                if (it == state->marking.end() || it->second != tokens)
                {
                    matching = false;
                    break;
                }
            }

            if (matching)
            {
                return true;
            }
        }

        return false;
    }

    std::shared_ptr<PriorityStateClass> PriorityStateClassAnalyzer::compute_initial_state()
    {
        // 初始标记
        Marking initial_marking;

        // 遍历所有顶点，找出所有带有token的库所
        boost::graph_traits<PriorityTPNGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = vertices(petri_net); vi != vi_end; ++vi)
        {
            const Vertex &vertex = petri_net[*vi];

            if (vertex.is_place())
            {
                const Place &place = vertex.as_place();
                if (place.token > 0)
                {
                    initial_marking[*vi] = place.token;
                }
            }
        }

        // 初始时间约束（针对在初始标记中启用的变迁）
        std::vector<TransitionTimeConstraint> initial_constraints;

        // 遍历所有变迁，找出在初始标记中启用的变迁，并确保其运行时间为0
        for (boost::tie(vi, vi_end) = vertices(petri_net); vi != vi_end; ++vi)
        {
            const Vertex &vertex = petri_net[*vi];

            if (vertex.is_transition())
            {
                // 确保所有变迁的 runtimes 初始化为 0
                Transition &transition = const_cast<Transition &>(vertex.as_transition());
                transition.runtimes = std::make_pair(0, 0);

                // 检查变迁是否在初始状态下启用
                if (is_transition_enabled(*vi, initial_marking))
                {
                    // 添加初始时间约束
                    TimeInterval initial_interval(transition.const_time.first, transition.const_time.second);
                    initial_constraints.push_back(
                        TransitionTimeConstraint(*vi, initial_interval, transition.priority, transition.core));

                    state_logger->debug("初始状态下启用变迁 t{} ({})，初始时间区间: [{}, {}]",
                                        *vi, vertex.name,
                                        initial_interval.lower,
                                        initial_interval.upper == INT_MAX ? "∞" : std::to_string(initial_interval.upper));
                }
            }
        }

        auto initial_state = std::make_shared<PriorityStateClass>(initial_marking, initial_constraints);
        state_logger->info("初始状态标记: {}", initial_state->marking_to_string());
        state_logger->info("初始状态启用变迁数量: {}", initial_constraints.size());

        return initial_state;
    }

    SCGVertex PriorityStateClassAnalyzer::add_state(const std::shared_ptr<PriorityStateClass> &state)
    {
        // 创建顶点属性
        SCGVertexProperties vp;
        vp.id = "s" + std::to_string(num_vertices(graph));
        vp.state = state;
        vp.label = generate_state_label(state);

        // 添加顶点
        SCGVertex v = boost::add_vertex(vp, graph);
        return v;
    }

    SCGEdge PriorityStateClassAnalyzer::add_edge(SCGVertex source, SCGVertex target,
                                                 ptpn_v_desc transition, const TimeInterval &interval)
    {
        // 创建边属性
        SCGEdgeProperties ep;
        ep.transition = transition;
        ep.time_interval = interval;

        // 获取变迁的名称
        const Vertex &trans_vertex = petri_net[transition];
        ep.xlabel = "t" + trans_vertex.name + " [" +
                    std::to_string(interval.lower) + "," +
                    (interval.upper == INT_MAX ? "∞" : std::to_string(interval.upper)) + "]";

        // 添加边
        auto [e, success] = boost::add_edge(source, target, ep, graph);
        return e;
    }

    std::string PriorityStateClassAnalyzer::generate_state_label(const std::shared_ptr<PriorityStateClass> &state)
    {
        // 返回状态的字符串表示，用于在DOT文件中显示
        return state->to_string();
    }

    bool PriorityStateClassAnalyzer::is_transition_enabled(ptpn_v_desc transition, const Marking &marking)
    {
        // 检查变迁的所有输入库所是否都有足够的token
        boost::graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;
        for (boost::tie(ei, ei_end) = in_edges(transition, petri_net); ei != ei_end; ++ei)
        {
            ptpn_v_desc source = boost::source(*ei, petri_net);

            // 获取边的权重
            int weight = petri_net[*ei].weight;

            // 检查该库所是否在标记中，且有足够的token
            auto it = marking.find(source);
            if (it == marking.end() || it->second < weight)
            {
                return false;
            }
        }

        return true;
    }

    Marking PriorityStateClassAnalyzer::update_marking(const Marking &current_marking, ptpn_v_desc transition)
    {
        Marking new_marking = current_marking;

        // 移除输入库所的token
        boost::graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;
        for (boost::tie(ei, ei_end) = in_edges(transition, petri_net); ei != ei_end; ++ei)
        {
            ptpn_v_desc source = boost::source(*ei, petri_net);
            int weight = petri_net[*ei].weight;

            new_marking[source] -= weight;
            if (new_marking[source] == 0)
            {
                new_marking.erase(source);
            }
        }

        // 添加输出库所的token
        boost::graph_traits<PriorityTPNGraph>::out_edge_iterator eo, eo_end;
        for (boost::tie(eo, eo_end) = out_edges(transition, petri_net); eo != eo_end; ++eo)
        {
            ptpn_v_desc target = boost::target(*eo, petri_net);
            int weight = petri_net[*eo].weight;

            new_marking[target] += weight;
        }

        return new_marking;
    }

    std::vector<TransitionTimeConstraint> PriorityStateClassAnalyzer::update_time_constraints(
        const std::vector<TransitionTimeConstraint> &current_constraints,
        ptpn_v_desc fired_transition,
        const TimeInterval &firing_interval,
        const std::vector<ptpn_v_desc> &new_enabled_transitions)
    {

        std::vector<TransitionTimeConstraint> new_constraints;

        // 对于每个在新状态中启用的变迁
        for (const auto &transition : new_enabled_transitions)
        {
            // 检查它是否在原状态中已经启用
            bool was_enabled = false;
            for (const auto &tc : current_constraints)
            {
                if (tc.transition == transition)
                {
                    was_enabled = true;

                    // 如果不是刚触发的变迁，更新时间约束
                    if (transition != fired_transition)
                    {
                        // 更新时间区间（考虑时间流逝和变迁触发）
                        TimeInterval updated_interval = tc.time_interval;

                        // 调整区间下界（减去触发时间）
                        updated_interval.lower = std::max(0, updated_interval.lower - firing_interval.lower);

                        // 调整区间上界（减去触发时间）
                        if (updated_interval.upper != INT_MAX)
                        {
                            updated_interval.upper = updated_interval.upper - firing_interval.lower;
                        }

                        new_constraints.push_back(TransitionTimeConstraint(
                            transition, updated_interval, tc.priority, tc.cpu));
                    }
                    break;
                }
            }

            // 如果是新启用的变迁，添加初始时间约束
            if (!was_enabled)
            {
                const Vertex &vertex = petri_net[transition];
                const Transition &t = vertex.as_transition();
                TimeInterval initial_interval(t.const_time.first, t.const_time.second);
                new_constraints.push_back(TransitionTimeConstraint(
                    transition, initial_interval, t.priority, t.core));
            }
        }

        return new_constraints;
    }

    int PriorityStateClassAnalyzer::calculate_wcet(ptpn_v_desc start_place, ptpn_v_desc end_place) const
    {
        // 获取所有从开始库所到结束库所的路径
        auto paths = get_paths_between_places(start_place, end_place);

        if (paths.empty())
        {
            std::cerr << "未找到从开始库所到结束库所的路径" << std::endl;
            return -1;
        }

        // 计算每条路径的执行时间，找出最大值
        int max_time = 0;
        for (const auto &path : paths)
        {
            int path_time = calculate_path_execution_time(path);
            max_time = std::max(max_time, path_time);
        }

        return max_time;
    }

    std::vector<std::vector<SCGVertex>> PriorityStateClassAnalyzer::get_paths_between_places(
        ptpn_v_desc start_place, ptpn_v_desc end_place) const
    {
        std::vector<std::vector<SCGVertex>> all_paths;
        std::vector<SCGVertex> current_path;
        std::unordered_set<SCGVertex> visited;

        // 找到包含开始库所的初始状态
        boost::graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
        {
            if (vertex_contains_place(*vi, start_place))
            {
                current_path.push_back(*vi);
                visited.insert(*vi);
                break;
            }
        }

        if (current_path.empty())
        {
            return all_paths;
        }

        // 使用深度优先搜索找到所有路径
        std::function<void(SCGVertex)> dfs = [&](SCGVertex current)
        {
            // 如果当前状态包含结束库所，找到一条路径
            if (vertex_contains_place(current, end_place))
            {
                all_paths.push_back(current_path);
                return;
            }

            // 遍历所有后继状态
            boost::graph_traits<StateClassGraph>::out_edge_iterator ei, ei_end;
            for (boost::tie(ei, ei_end) = out_edges(current, graph); ei != ei_end; ++ei)
            {
                SCGVertex next = boost::target(*ei, graph);
                if (visited.find(next) == visited.end())
                {
                    current_path.push_back(next);
                    visited.insert(next);
                    dfs(next);
                    current_path.pop_back();
                    visited.erase(next);
                }
            }
        };

        dfs(current_path[0]);
        return all_paths;
    }

    int PriorityStateClassAnalyzer::calculate_path_execution_time(const std::vector<SCGVertex> &path) const
    {
        int total_time = 0;

        // 计算路径上所有边的执行时间之和
        for (size_t i = 0; i < path.size() - 1; ++i)
        {
            SCGVertex current = path[i];
            SCGVertex next = path[i + 1];

            // 找到连接这两个顶点的边
            boost::graph_traits<StateClassGraph>::out_edge_iterator ei, ei_end;
            for (boost::tie(ei, ei_end) = out_edges(current, graph); ei != ei_end; ++ei)
            {
                if (boost::target(*ei, graph) == next)
                {
                    // 使用边的上界作为执行时间
                    total_time += graph[*ei].time_interval.upper;
                    break;
                }
            }
        }

        return total_time;
    }

    bool PriorityStateClassAnalyzer::vertex_contains_place(SCGVertex v, ptpn_v_desc place) const
    {
        const auto &state = graph[v].state;
        return state->marking.find(place) != state->marking.end();
    }

    const std::vector<TransitionTimeConstraint> &PriorityStateClass::get_time_constraints() const
    {
        return time_constraints;
    }

    // 获取可调度变迁（基于时间约束能够触发的变迁）
    std::vector<ptpn_v_desc> PriorityStateClass::get_schedulable_transitions(
        const std::vector<ptpn_v_desc> &enabled_transitions,
        const PriorityTPNGraph &graph,
        TimeInterval &common_interval) const
    {
        std::vector<ptpn_v_desc> schedulable_transitions;

        // 使用 map 存储每个变迁的静态时间区间
        std::map<ptpn_v_desc, TimeInterval> transition_intervals;

        // 第一步：计算每个使能变迁的静态时间区间并存储到 map 中
        for (const auto &transition : enabled_transitions)
        {
            // 获取变迁的静态时间约束
            const Vertex &vertex = graph[transition];
            const Transition &t = vertex.as_transition();

            // 计算时间区间：const_time - runtimes
            // 例如：const_time=[3,5], runtimes=[1,2]
            // 则时间区间为：[max(0, 3-2), max(0, 5-1)] = [1,4]
            TimeInterval static_interval(
                std::max(0, t.const_time.first - t.runtimes.second), // 最小时间 = max(0, 最小约束时间 - 最大运行时间)
                std::max(0, t.const_time.second - t.runtimes.first)  // 最大时间 = max(0, 最大约束时间 - 最小运行时间)
            );

            // 存储到 map 中
            transition_intervals[transition] = static_interval;

            state_logger->debug("变迁 t{} 的时间区间: static_interval=[{}, {}], const_time=[{},{}], runtimes=[{},{}]",
                                transition,
                                static_interval.lower, static_interval.upper == INT_MAX ? "∞" : std::to_string(static_interval.upper),
                                t.const_time.first, t.const_time.second,
                                t.runtimes.first, t.runtimes.second);
        }

        // 如果没有使能变迁，返回空列表
        if (transition_intervals.empty())
        {
            state_logger->warn("没有使能变迁，返回空列表");
            return schedulable_transitions;
        }

        // 第二步：计算共同时间区间（使用最小上界和最小下界）
        int min_lower = INT_MAX;
        int min_upper = INT_MAX;

        // 找出所有区间中的最小下界和最小上界
        for (const auto &[transition, interval] : transition_intervals)
        {
            min_lower = std::min(min_lower, interval.lower);
            min_upper = std::min(min_upper, interval.upper);
        }

        // 设置共同时间区间
        common_interval = TimeInterval(min_lower, min_upper);

        if (!common_interval.is_valid())
        {
            state_logger->warn("无效的时间区间：[{}, {}]，跳过当前状态",
                               common_interval.lower, common_interval.upper);
            return {}; // 返回空列表，表示没有可调度的变迁
        }
        state_logger->info("共同时间区间: [{}, {}]", common_interval.lower,
                           common_interval.upper == INT_MAX ? "∞" : std::to_string(common_interval.upper));

        // 第三步：筛选可调度的变迁（与共同时间区间有交集的变迁）
        for (const auto &transition : enabled_transitions)
        {
            const TimeInterval &static_interval = transition_intervals[transition];

            // 计算变迁静态区间与公共区间的交集
            TimeInterval intersection = static_interval.intersect(common_interval);

            // 判断变迁是否可调度：变迁的静态时间区间与共同区间有交集
            if (intersection.is_valid())
            {
                schedulable_transitions.push_back(transition);
                state_logger->debug("变迁 t{} 的时间区间 [{}, {}] 与共同区间 [{}, {}] 有交集 [{}, {}]，可调度",
                                    transition,
                                    static_interval.lower, static_interval.upper == INT_MAX ? "∞" : std::to_string(static_interval.upper),
                                    common_interval.lower, common_interval.upper == INT_MAX ? "∞" : std::to_string(common_interval.upper),
                                    intersection.lower, intersection.upper == INT_MAX ? "∞" : std::to_string(intersection.upper));
            }
            else
            {
                state_logger->debug("变迁 t{} 的时间区间 [{}, {}] 与共同区间 [{}, {}] 无交集，不可调度",
                                    transition,
                                    static_interval.lower, static_interval.upper == INT_MAX ? "∞" : std::to_string(static_interval.upper),
                                    common_interval.lower, common_interval.upper == INT_MAX ? "∞" : std::to_string(common_interval.upper));
            }
        }

        return schedulable_transitions;
    }

    TimeInterval PriorityStateClass::get_suspended_clock(ptpn_v_desc transition) const
    {
        auto it = suspended_transitions_clocks.find(transition);
        if (it != suspended_transitions_clocks.end())
        {
            return it->second;
        }
        return TimeInterval(0, 0); // 默认返回零时间
    }

    std::string PriorityStateClass::marking_to_string() const
    {
        std::stringstream ss;
        ss << "[";

        bool first = true;
        for (const auto &p_m : marking)
        {
            // 忽略token数量小于等于0的库所
            if (p_m.second <= 0)
            {
                continue;
            }

            if (!first)
            {
                ss << ", ";
            }
            ss << "p" << p_m.first << ":" << p_m.second;
            first = false;
        }

        ss << "]";
        return ss.str();
    }
} // namespace priority_scg