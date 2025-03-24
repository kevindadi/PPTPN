#include "priority_state_class.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace priority_scg
{
    bool PriorityStateClass::operator==(const PriorityStateClass &other) const
    {
        if (marking != other.marking)
        {
            return false;
        }

        // 如果时间约束数量不同，则状态类不同
        if (time_constraints.size() != other.time_constraints.size())
        {
            return false;
        }

        // 比较每个时间约束
        for (const auto &tc1 : time_constraints)
        {
            bool found_match = false;
            for (const auto &tc2 : other.time_constraints)
            {
                if (tc1 == tc2)
                {
                    found_match = true;
                    break;
                }
            }
            if (!found_match)
            {
                return false;
            }
        }

        return true;
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
               << "] (priority:" << tc.priority << ")";
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

            // 检查该变迁的所有输入库所是否都有足够的token
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

                // 检查该库所是否在当前标记中
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

                // 如果token数量为0，从标记中移除该库所
                if (new_marking[source] == 0)
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

                // 如果是新启用的变迁，添加初始时间约束
                bool was_enabled = false;
                for (const auto &tc : time_constraints)
                {
                    if (tc.transition == v)
                    {
                        was_enabled = true;

                        // 如果不是刚触发的变迁，保持时间约束
                        if (v != fired_transition)
                        {
                            // 如果变迁之前是挂起状态并且还在挂起集合中，保持其当前时间约束
                            if (is_suspended(v) && new_suspended.find(v) != new_suspended.end())
                            {
                                new_constraints.push_back(tc);
                            }
                            // 否则更新时间约束（减去经过的时间）
                            else
                            {
                                // 找到被触发变迁的时间约束，获取触发时间
                                TimeInterval firing_time(0, 0);
                                for (const auto &ftc : time_constraints)
                                {
                                    if (ftc.transition == fired_transition)
                                    {
                                        firing_time = TimeInterval(ftc.time_interval.lower, ftc.time_interval.lower);
                                        break;
                                    }
                                }

                                // 更新时间约束
                                TimeInterval updated_interval = tc.time_interval;
                                // 减去触发时间
                                updated_interval.lower = std::max(0, updated_interval.lower - firing_time.lower);
                                if (updated_interval.upper != INT_MAX)
                                {
                                    updated_interval.upper = updated_interval.upper - firing_time.lower;
                                }

                                new_constraints.push_back(TransitionTimeConstraint(v, updated_interval, tc.priority));
                            }
                        }
                        break;
                    }
                }

                // 如果是新启用的变迁或者是刚从挂起状态恢复的变迁，添加初始时间约束
                if (!was_enabled || (was_enabled && v == fired_transition && is_suspended(v)))
                {
                    const Transition &t = vertex.as_transition();
                    TimeInterval initial_interval(t.const_time.first, t.const_time.second);
                    new_constraints.push_back(TransitionTimeConstraint(v, initial_interval, t.priority));
                }
            }
        }

        // 创建新的状态类
        auto new_state = std::make_shared<PriorityStateClass>(new_marking, new_constraints);

        // 复制挂起状态
        for (const auto &suspended : new_suspended)
        {
            new_state->mark_suspended(suspended, get_suspended_clock(suspended));
        }

        return new_state;
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
            std::cerr << "无法计算初始状态类" << std::endl;
            return;
        }

        // 添加初始状态到状态类图
        SCGVertex initial_vertex = add_state(initial_state);

        // 使用广度优先搜索生成状态类图
        std::queue<std::pair<SCGVertex, std::shared_ptr<PriorityStateClass>>> queue;
        std::unordered_map<std::string, SCGVertex> state_map; // 用于检测已访问的状态

        // 将初始状态添加到队列
        queue.push({initial_vertex, initial_state});
        state_map[initial_state->to_string()] = initial_vertex;

        while (!queue.empty())
        {
            auto [current_vertex, current_state] = queue.front();
            queue.pop();

            // 获取当前状态中启用的变迁
            auto enabled_transitions = current_state->get_enabled_transitions(petri_net);
            auto filter_result = current_state->filter_by_priority(enabled_transitions, petri_net);

            if (filter_result.enabled_transitions.empty())
            {
                continue; // 没有可用变迁，继续处理下一个状态
            }

            // 计算所有使能变迁的时间区间交集（全局时间）
            TimeInterval common_interval(0, INT_MAX);
            for (const auto &transition : filter_result.enabled_transitions)
            {
                for (const auto &constraint : current_state->get_time_constraints())
                {
                    if (constraint.transition == transition)
                    {
                        common_interval = common_interval.intersect(constraint.time_interval);
                        break;
                    }
                }
            }

            if (!common_interval.is_valid())
            {
                continue; // 如果没有有效的时间区间交集，跳过这个状态
            }

            // 处理保留的高优先级变迁
            auto schedulable_transitions = current_state->get_schedulable_transitions(
                filter_result.enabled_transitions, petri_net, common_interval);

            // 处理挂起变迁
            for (const auto &suspended_transition : filter_result.suspended_transitions)
            {
                // 找到对应的时间约束
                for (const auto &constraint : current_state->get_time_constraints())
                {
                    if (constraint.transition == suspended_transition)
                    {
                        // 创建一个新的状态，将变迁标记为挂起
                        auto suspended_state = std::make_shared<PriorityStateClass>(*current_state);
                        suspended_state->mark_suspended(suspended_transition, constraint.time_interval);

                        // 将新状态添加到队列...
                        break;
                    }
                }
            }

            // 处理每个可调度的变迁
            for (const auto &transition : schedulable_transitions)
            {
                const Vertex &vertex = petri_net[transition];
                const Transition &t = vertex.as_transition();
                bool is_highest_priority = true;

                // 检查是否是最高优先级
                for (const auto &other_t : schedulable_transitions)
                {
                    if (other_t == transition)
                        continue;

                    const Transition &other_trans = petri_net[other_t].as_transition();
                    if (t.core == other_trans.core && other_trans.priority < t.priority)
                    {
                        is_highest_priority = false;
                        break;
                    }
                }

                if (is_highest_priority)
                {
                    // 对于最高优先级变迁，直接执行
                    auto successor_state = current_state->compute_successor(petri_net, transition);
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

                    add_edge(current_vertex, successor_vertex, transition, common_interval);
                }
                else if (t.handle)
                {
                    // 对于可挂起变迁，创建两个后继状态

                    // 1. 执行该变迁
                    auto successor_state = current_state->compute_successor(petri_net, transition);
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

                    add_edge(current_vertex, successor_vertex, transition, common_interval);

                    // 2. 挂起该变迁
                    auto suspended_state = std::make_shared<PriorityStateClass>(*current_state);
                    suspended_state->mark_suspended(transition, current_state->get_suspended_clock(transition));
                    successor_key = suspended_state->to_string();

                    it = state_map.find(successor_key);
                    if (it == state_map.end())
                    {
                        successor_vertex = add_state(suspended_state);
                        state_map[successor_key] = successor_vertex;
                        queue.push({successor_vertex, suspended_state});
                    }
                    else
                    {
                        successor_vertex = it->second;
                    }

                    add_edge(current_vertex, successor_vertex, transition, common_interval);
                }
                // 对于不可挂起的低优先级变迁，不处理
            }
        }

        std::cout << "状态类图生成完成，共有 " << num_vertices(graph) << " 个状态和 "
                  << num_edges(graph) << " 个转换。" << std::endl;
    }

    void PriorityStateClassAnalyzer::export_to_dot(const std::string &filename)
    {
        std::ofstream dot_file(filename);
        if (!dot_file.is_open())
        {
            std::cerr << "无法打开文件: " << filename << std::endl;
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

        std::cout << "状态类图导出功能暂时禁用" << std::endl;
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

        // 遍历所有变迁，找出在初始标记中启用的变迁
        for (boost::tie(vi, vi_end) = vertices(petri_net); vi != vi_end; ++vi)
        {
            const Vertex &vertex = petri_net[*vi];

            if (vertex.is_transition() && is_transition_enabled(*vi, initial_marking))
            {
                const Transition &transition = vertex.as_transition();
                TimeInterval initial_interval(transition.const_time.first, transition.const_time.second);
                initial_constraints.push_back(
                    TransitionTimeConstraint(*vi, initial_interval, transition.priority));
            }
        }

        return std::make_shared<PriorityStateClass>(initial_marking, initial_constraints);
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
                            transition, updated_interval, tc.priority));
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
                    transition, initial_interval, t.priority));
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

        // 初始化时间区间为最大范围
        common_interval = TimeInterval(0, INT_MAX);

        // 计算所有使能变迁的时间区间交集
        for (const auto &transition : enabled_transitions)
        {
            bool found = false;
            for (const auto &constraint : time_constraints)
            {
                if (constraint.transition == transition)
                {
                    common_interval = common_interval.intersect(constraint.time_interval);
                    found = true;
                    break;
                }
            }

            // 如果没有找到变迁的时间约束，说明该变迁不是当前可用的
            if (!found)
            {
                // 使用变迁的静态时间约束
                const Vertex &vertex = graph[transition];
                const Transition &t = vertex.as_transition();
                TimeInterval static_interval(t.const_time.first, t.const_time.second);
                common_interval = common_interval.intersect(static_interval);
            }
        }

        // 检查时间区间是否有效
        if (!common_interval.is_valid())
        {
            return {}; // 返回空列表，表示没有可调度的变迁
        }

        // 找出在共同时间区间内可以调度的变迁
        for (const auto &transition : enabled_transitions)
        {
            for (const auto &constraint : time_constraints)
            {
                if (constraint.transition == transition)
                {
                    // 检查变迁的时间区间是否与共同区间相交
                    TimeInterval intersection = constraint.time_interval.intersect(common_interval);
                    if (intersection.is_valid())
                    {
                        schedulable_transitions.push_back(transition);
                    }
                    break;
                }
            }
        }

        return schedulable_transitions;
    }

    // 使用修正后的算法生成状态类图
    void PriorityStateClassAnalyzer::generate_state_class_graph_corrected()
    {
        // 计算初始状态类
        auto initial_state = compute_initial_state();
        if (!initial_state)
        {
            std::cerr << "无法计算初始状态类" << std::endl;
            return;
        }

        // 添加初始状态到状态类图
        SCGVertex initial_vertex = add_state(initial_state);

        // 使用广度优先搜索生成状态类图
        std::queue<std::pair<SCGVertex, std::shared_ptr<PriorityStateClass>>> queue;
        std::unordered_map<std::string, SCGVertex> state_map; // 用于检测已访问的状态

        // 将初始状态添加到队列
        queue.push({initial_vertex, initial_state});
        state_map[initial_state->to_string()] = initial_vertex;

        while (!queue.empty())
        {
            auto [current_vertex, current_state] = queue.front();
            queue.pop();

            // 获取当前状态中启用的变迁
            auto enabled_transitions = current_state->get_enabled_transitions(petri_net);

            if (enabled_transitions.empty())
            {
                continue; // 没有使能的变迁，继续处理下一个状态
            }

            // 应用优先级过滤，同一CPU上只保留最高优先级的变迁和可挂起变迁
            auto priority_filtered_transitions = current_state->filter_by_priority(enabled_transitions, petri_net);

            if (priority_filtered_transitions.enabled_transitions.empty())
            {
                continue; // 没有通过优先级过滤的变迁
            }

            // 计算可调度变迁（在时间约束下可以触发的变迁）
            TimeInterval common_interval;
            auto schedulable_transitions = current_state->get_schedulable_transitions(
                priority_filtered_transitions.enabled_transitions, petri_net, common_interval);

            if (schedulable_transitions.empty())
            {
                continue; // 没有可调度的变迁
            }

            // 处理每个可调度的变迁
            for (const auto &transition : schedulable_transitions)
            {
                const Vertex &vertex = petri_net[transition];
                const Transition &t = vertex.as_transition();

                // 检查是否是最高优先级变迁
                bool is_highest_priority = true;
                for (const auto &other_t : schedulable_transitions)
                {
                    if (other_t == transition)
                        continue;

                    const Transition &other_trans = petri_net[other_t].as_transition();
                    if (t.core == other_trans.core && other_trans.priority < t.priority)
                    {
                        is_highest_priority = false;
                        break;
                    }
                }

                if (is_highest_priority)
                {
                    // 对于最高优先级变迁，直接执行
                    auto successor_state = current_state->compute_successor(petri_net, transition);
                    auto successor_key = successor_state->to_string();
                    SCGVertex successor_vertex;

                    auto it = state_map.find(successor_key);
                    if (it == state_map.end())
                    {
                        // 新状态，添加到状态类图和队列
                        successor_vertex = add_state(successor_state);
                        state_map[successor_key] = successor_vertex;
                        queue.push({successor_vertex, successor_state});
                    }
                    else
                    {
                        // 已存在的状态，直接使用
                        successor_vertex = it->second;
                    }

                    // 添加从当前状态到后继状态的边
                    add_edge(current_vertex, successor_vertex, transition, common_interval);
                }
                else if (t.handle)
                {
                    // 对于非最高优先级的可挂起变迁，创建两个后继状态

                    // 1. 执行该变迁的后继状态
                    auto executed_state = current_state->compute_successor(petri_net, transition);
                    auto executed_key = executed_state->to_string();
                    SCGVertex executed_vertex;

                    auto it = state_map.find(executed_key);
                    if (it == state_map.end())
                    {
                        executed_vertex = add_state(executed_state);
                        state_map[executed_key] = executed_vertex;
                        queue.push({executed_vertex, executed_state});
                    }
                    else
                    {
                        executed_vertex = it->second;
                    }

                    add_edge(current_vertex, executed_vertex, transition, common_interval);

                    // 2. 挂起该变迁的后继状态
                    auto suspended_state = std::make_shared<PriorityStateClass>(*current_state);
                    suspended_state->mark_suspended(transition, current_state->get_suspended_clock(transition));
                    auto suspended_key = suspended_state->to_string();

                    SCGVertex suspended_vertex;
                    it = state_map.find(suspended_key);
                    if (it == state_map.end())
                    {
                        suspended_vertex = add_state(suspended_state);
                        state_map[suspended_key] = suspended_vertex;
                        queue.push({suspended_vertex, suspended_state});
                    }
                    else
                    {
                        suspended_vertex = it->second;
                    }

                    add_edge(current_vertex, suspended_vertex, transition, common_interval);
                }
                // 对于不可挂起的低优先级变迁，不处理
            }
        }

        std::cout << "修正后的状态类图生成完成，共有 " << num_vertices(graph) << " 个状态和 "
                  << num_edges(graph) << " 个转换。" << std::endl;
    }

    // 测试状态类生成算法
    void PriorityStateClassAnalyzer::test_state_class_generation()
    {
        std::cout << "开始测试状态类生成算法..." << std::endl;

        // 1. 计算初始状态
        auto initial_state = compute_initial_state();
        if (!initial_state)
        {
            std::cerr << "无法计算初始状态类，测试失败" << std::endl;
            return;
        }

        std::cout << "初始状态类：" << std::endl;
        std::cout << initial_state->to_string() << std::endl;

        // 2. 获取初始状态的使能变迁
        auto enabled_transitions = initial_state->get_enabled_transitions(petri_net);
        std::cout << "初始状态使能变迁数量: " << enabled_transitions.size() << std::endl;

        for (const auto &transition : enabled_transitions)
        {
            const Vertex &vertex = petri_net[transition];
            const Transition &t = vertex.as_transition();
            std::cout << "  变迁 t" << transition << " (" << vertex.name << "): 优先级=" << t.priority
                      << ", CPU=" << t.core << ", 时间=["
                      << t.const_time.first << ","
                      << (t.const_time.second == INT_MAX ? "∞" : std::to_string(t.const_time.second))
                      << "], 可挂起=" << (t.handle ? "是" : "否") << std::endl;
        }

        // 3. 按优先级过滤变迁
        auto filtered_transitions = initial_state->filter_by_priority(enabled_transitions, petri_net);
        std::cout << "优先级过滤后的变迁数量: " << filtered_transitions.enabled_transitions.size() << std::endl;

        for (const auto &transition : filtered_transitions.enabled_transitions)
        {
            const Vertex &vertex = petri_net[transition];
            const Transition &t = vertex.as_transition();
            std::cout << "  变迁 t" << transition << " (" << vertex.name << "): 优先级=" << t.priority
                      << ", CPU=" << t.core << std::endl;
        }

        // 4. 获取可调度变迁
        TimeInterval common_interval;
        auto schedulable_transitions = initial_state->get_schedulable_transitions(
            filtered_transitions.enabled_transitions, petri_net, common_interval);

        std::cout << "可调度变迁数量: " << schedulable_transitions.size() << std::endl;
        std::cout << "共同时间区间: [" << common_interval.lower << ", "
                  << (common_interval.upper == INT_MAX ? "∞" : std::to_string(common_interval.upper))
                  << "]" << std::endl;

        for (const auto &transition : schedulable_transitions)
        {
            const Vertex &vertex = petri_net[transition];
            const Transition &t = vertex.as_transition();
            std::cout << "  变迁 t" << transition << " (" << vertex.name << ")" << std::endl;
        }

        // 5. 计算一些后继状态
        if (!schedulable_transitions.empty())
        {
            std::cout << "计算部分后继状态:" << std::endl;

            for (size_t i = 0; i < std::min(size_t(3), schedulable_transitions.size()); ++i)
            {
                auto transition = schedulable_transitions[i];
                const Vertex &vertex = petri_net[transition];

                std::cout << "触发变迁 t" << transition << " (" << vertex.name << ") 后的状态类:" << std::endl;
                auto successor = initial_state->compute_successor(petri_net, transition);
                std::cout << successor->to_string() << std::endl;
            }
        }

        std::cout << "状态类生成算法测试完成" << std::endl;
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
} // namespace priority_scg