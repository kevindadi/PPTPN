#include "differential_state_class.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace differential_scg
{

    // DifferentialBoundaryMatrix 实现
    DifferentialBoundaryMatrix::DifferentialBoundaryMatrix(const PriorityTPNGraph &graph)
    {
        initialize_matrix(graph);
    }

    void DifferentialBoundaryMatrix::initialize_matrix(const PriorityTPNGraph &graph)
    {
        // 计算矩阵维度
        size_t num_places = 0;
        size_t num_transitions = 0;

        boost::graph_traits<PriorityTPNGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
        {
            const Vertex &vertex = graph[*vi];
            if (vertex.is_place())
            {
                place_indices[*vi] = num_places++;
            }
            else if (vertex.is_transition())
            {
                transition_indices[*vi] = num_transitions++;
            }
        }

        // 创建反向索引映射
        reverse_place_indices.resize(num_places);
        reverse_transition_indices.resize(num_transitions);

        for (const auto &[vertex, index] : place_indices)
        {
            reverse_place_indices[index] = vertex;
        }

        for (const auto &[vertex, index] : transition_indices)
        {
            reverse_transition_indices[index] = vertex;
        }

        // 初始化矩阵
        dimension = num_places + num_transitions;
        matrix = MatrixXd::Zero(dimension, dimension);

        // 填充矩阵
        // 1. 填充库所到变迁的关系（前向和后向）
        for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
        {
            const Vertex &vertex = graph[*vi];

            if (vertex.is_place())
            {
                size_t p_index = place_indices[*vi];

                // 处理输入边
                boost::graph_traits<PriorityTPNGraph>::in_edge_iterator ei, ei_end;
                for (boost::tie(ei, ei_end) = in_edges(*vi, graph); ei != ei_end; ++ei)
                {
                    ptpn_v_desc source = boost::source(*ei, graph);
                    if (graph[source].is_transition())
                    {
                        size_t t_index = transition_indices[source];
                        matrix(p_index, num_places + t_index) = -graph[*ei].weight;
                    }
                }

                // 处理输出边
                boost::graph_traits<PriorityTPNGraph>::out_edge_iterator oe, oe_end;
                for (boost::tie(oe, oe_end) = out_edges(*vi, graph); oe != oe_end; ++oe)
                {
                    ptpn_v_desc target = boost::target(*oe, graph);
                    if (graph[target].is_transition())
                    {
                        size_t t_index = transition_indices[target];
                        matrix(p_index, num_places + t_index) = graph[*oe].weight;
                    }
                }
            }
        }

        // 2. 填充时间约束
        for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
        {
            const Vertex &vertex = graph[*vi];

            if (vertex.is_transition())
            {
                size_t t_index = transition_indices[*vi];
                const Transition &t = vertex.as_transition();

                // 设置时间约束
                matrix(num_places + t_index, num_places + t_index) = 1.0;

                // 设置优先级约束
                for (const auto &[other_t, other_index] : transition_indices)
                {
                    if (other_t != *vi)
                    {
                        const Transition &other = graph[other_t].as_transition();
                        if (t.priority > other.priority)
                        {
                            matrix(num_places + t_index, num_places + other_index) = -1.0;
                        }
                    }
                }
            }
        }
    }

    // DifferentialStateClass 实现
    bool DifferentialStateClass::operator==(const DifferentialStateClass &other) const
    {
        // 比较标记向量
        if (!marking.isApprox(other.marking, 1e-10))
        {
            return false;
        }

        // 比较时间约束
        if (time_constraints.size() != other.time_constraints.size())
        {
            return false;
        }

        for (size_t i = 0; i < time_constraints.size(); ++i)
        {
            if (!(time_constraints[i] == other.time_constraints[i]))
            {
                return false;
            }
        }

        return true;
    }

    std::string DifferentialStateClass::to_string(const DifferentialBoundaryMatrix &matrix) const
    {
        std::stringstream ss;

        // 输出标记
        ss << "Marking: {";
        bool first = true;
        for (size_t i = 0; i < matrix.get_reverse_place_indices().size(); ++i)
        {
            if (!first)
            {
                ss << ", ";
            }
            ss << "p" << matrix.get_reverse_place_indices()[i] << ":" << marking(i);
            first = false;
        }
        ss << "}\n";

        // 输出时间约束
        ss << "Time Constraints: {";
        first = true;
        for (size_t i = 0; i < time_constraints.size(); ++i)
        {
            if (!first)
            {
                ss << ", ";
            }
            ss << "t" << matrix.get_reverse_transition_indices()[i] << ":["
               << time_constraints[i].lower << ","
               << (std::isinf(time_constraints[i].upper) ? "∞" : std::to_string(time_constraints[i].upper))
               << "]";
            first = false;
        }
        ss << "}";

        return ss.str();
    }

    std::vector<ptpn_v_desc> DifferentialStateClass::get_enabled_transitions(
        const DifferentialBoundaryMatrix &matrix) const
    {
        std::vector<ptpn_v_desc> enabled_transitions;
        size_t num_places = matrix.get_reverse_place_indices().size();

        // 检查每个变迁是否可启用
        for (size_t i = 0; i < matrix.get_reverse_transition_indices().size(); ++i)
        {
            ptpn_v_desc transition = matrix.get_reverse_transition_indices()[i];

            // 检查输入库所的token是否足够
            bool is_enabled = true;
            for (size_t j = 0; j < num_places; ++j)
            {
                if (matrix.get_matrix()(j, num_places + i) < 0)
                {
                    if (marking(j) < -matrix.get_matrix()(j, num_places + i))
                    {
                        is_enabled = false;
                        break;
                    }
                }
            }

            if (is_enabled)
            {
                enabled_transitions.push_back(transition);
            }
        }

        return enabled_transitions;
    }

    std::vector<ptpn_v_desc> DifferentialStateClass::filter_by_priority(
        const std::vector<ptpn_v_desc> &enabled_transitions,
        const PriorityTPNGraph &graph) const
    {

        if (enabled_transitions.empty())
        {
            return {};
        }

        // 按优先级对变迁进行分组
        std::map<int, std::vector<ptpn_v_desc>> priority_groups;

        for (const auto &transition : enabled_transitions)
        {
            const Vertex &vertex = graph[transition];
            const Transition &t = vertex.as_transition();
            priority_groups[t.priority].push_back(transition);
        }

        // 返回最高优先级的变迁组
        return priority_groups.begin()->second;
    }

    std::shared_ptr<DifferentialStateClass> DifferentialStateClass::compute_successor(
        const DifferentialBoundaryMatrix &matrix,
        const PriorityTPNGraph &petri_net,
        ptpn_v_desc fired_transition,
        const TimeInterval &firing_interval) const
    {
        // 创建新的标记向量
        VectorXd new_marking = marking;

        // 获取变迁的索引
        size_t t_index = matrix.get_transition_indices().at(fired_transition);
        size_t num_places = matrix.get_reverse_place_indices().size();

        // 更新标记
        for (size_t i = 0; i < num_places; ++i)
        {
            if (matrix.get_matrix()(i, num_places + t_index) != 0)
            {
                new_marking(i) += matrix.get_matrix()(i, num_places + t_index);
            }
        }

        // 创建新的时间约束向量
        std::vector<TimeInterval> new_time_constraints;

        // 获取当前可启用的变迁
        std::vector<ptpn_v_desc> enabled_transitions = get_enabled_transitions(matrix);

        // 更新时间约束
        for (const auto &t : enabled_transitions)
        {
            const Vertex &vertex = petri_net[t];
            const Transition &transition = vertex.as_transition();
            TimeInterval t_interval(transition.const_time.first, transition.const_time.second);

            if (t == fired_transition)
            {
                new_time_constraints.push_back(firing_interval);
            }
            else
            {
                // 找到原状态中该变迁的时间约束
                bool found = false;
                for (size_t i = 0; i < matrix.get_reverse_transition_indices().size(); ++i)
                {
                    if (matrix.get_reverse_transition_indices()[i] == t)
                    {
                        new_time_constraints.push_back(time_constraints[i].intersect(t_interval));
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    new_time_constraints.push_back(t_interval);
                }
            }
        }

        // 创建并返回新的状态类
        return std::make_shared<DifferentialStateClass>(new_marking, new_time_constraints);
    }

    // DifferentialStateClassAnalyzer 实现
    DifferentialStateClassAnalyzer::DifferentialStateClassAnalyzer(const PriorityTPNGraph &petri_net)
        : petri_net(petri_net), matrix(petri_net)
    {
        // 初始化状态类图
        graph[boost::graph_bundle].name = "基于差分边界矩阵的状态类图";
    }

    void DifferentialStateClassAnalyzer::generate_state_class_graph()
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
        std::queue<std::pair<SCGVertex, std::shared_ptr<DifferentialStateClass>>> queue;
        std::unordered_map<std::string, SCGVertex> state_map; // 用于检测已访问的状态

        // 将初始状态添加到队列
        queue.push({initial_vertex, initial_state});
        state_map[initial_state->to_string(matrix)] = initial_vertex;

        while (!queue.empty())
        {
            auto [current_vertex, current_state] = queue.front();
            queue.pop();

            // 获取当前状态中启用的变迁
            auto enabled_transitions = current_state->get_enabled_transitions(matrix);

            // 应用优先级规则过滤变迁
            auto priority_filtered_transitions = current_state->filter_by_priority(enabled_transitions, petri_net);

            // 对每个可触发的变迁，计算后继状态
            for (const auto &transition : priority_filtered_transitions)
            {
                // 获取变迁的时间区间
                const Vertex &transition_vertex = petri_net[transition];
                const Transition &t = transition_vertex.as_transition();
                TimeInterval interval(t.const_time.first, t.const_time.second);

                // 计算触发该变迁后的后继状态
                auto successor_state = current_state->compute_successor(matrix, petri_net, transition, interval);

                // 检查该状态是否已经存在于状态类图中
                auto successor_key = successor_state->to_string(matrix);
                SCGVertex successor_vertex;

                auto it = state_map.find(successor_key);
                if (it == state_map.end())
                {
                    // 如果是新状态，添加到状态类图
                    successor_vertex = add_state(successor_state);
                    state_map[successor_key] = successor_vertex;
                    queue.push({successor_vertex, successor_state});
                }
                else
                {
                    // 如果状态已存在，使用现有顶点
                    successor_vertex = it->second;
                }

                // 添加从当前状态到后继状态的边
                add_edge(current_vertex, successor_vertex, transition, interval);
            }
        }

        std::cout << "状态类图生成完成，共有 " << num_vertices(graph) << " 个状态和 "
                  << num_edges(graph) << " 个转换。" << std::endl;
    }

    void DifferentialStateClassAnalyzer::export_to_dot(const std::string &filename)
    {
        // 暂时注释掉导出功能
        /*
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
        dp.property("shape", get(&SCGVertexProperties::shape, graph));

        // 添加边属性
        dp.property("label", get(&SCGEdgeProperties::label, graph));

        // 写入DOT文件
        boost::write_graphviz_dp(dot_file, graph, dp);
        */

        std::cout << "状态类图导出功能暂时禁用" << std::endl;
    }

    bool DifferentialStateClassAnalyzer::has_deadlock_states() const
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

    std::vector<SCGVertex> DifferentialStateClassAnalyzer::get_deadlock_states() const
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

    TimeInterval DifferentialStateClassAnalyzer::calculate_max_execution_time() const
    {
        // 找出所有终止状态（没有出边的状态）
        std::vector<SCGVertex> terminal_states = get_deadlock_states();

        if (terminal_states.empty())
        {
            // 如果没有终止状态，返回无界区间
            return TimeInterval(0, std::numeric_limits<double>::infinity());
        }

        double min_time = std::numeric_limits<double>::infinity();
        double max_time = 0;

        // 对每个终止状态，计算从初始状态到达它的最短和最长路径
        SCGVertex initial_vertex = *(vertices(graph).first); // 假设第一个顶点是初始状态

        for (const auto &terminal : terminal_states)
        {
            // 这里应该实现最短路径和最长路径算法
            // 为简化示例，我们假设每个边的权重就是其时间区间
            // 实际上需要使用图算法来计算路径

            // 这里只是占位符，实际实现应该计算真正的路径时间
            min_time = std::min(min_time, 10.0);
            max_time = std::max(max_time, 100.0);
        }

        return TimeInterval(min_time, max_time);
    }

    bool DifferentialStateClassAnalyzer::is_marking_reachable(const VectorXd &target_marking) const
    {
        // 检查状态类图中是否存在一个状态，其标记与目标标记匹配
        boost::graph_traits<StateClassGraph>::vertex_iterator vi, vi_end;
        for (boost::tie(vi, vi_end) = vertices(graph); vi != vi_end; ++vi)
        {
            const auto &state = graph[*vi].state;

            // 比较标记向量
            if (state->get_marking().isApprox(target_marking, 1e-10))
            {
                return true;
            }
        }

        return false;
    }

    std::shared_ptr<DifferentialStateClass> DifferentialStateClassAnalyzer::compute_initial_state()
    {
        // 初始标记向量
        VectorXd initial_marking = VectorXd::Zero(matrix.get_reverse_place_indices().size());

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
                    initial_marking(matrix.get_place_indices().at(*vi)) = place.token;
                }
            }
        }

        // 初始时间约束（针对在初始标记中启用的变迁）
        std::vector<TimeInterval> initial_constraints;

        // 遍历所有变迁，找出在初始标记中启用的变迁
        for (boost::tie(vi, vi_end) = vertices(petri_net); vi != vi_end; ++vi)
        {
            const Vertex &vertex = petri_net[*vi];

            if (vertex.is_transition() && is_transition_enabled(*vi, initial_marking))
            {
                const Transition &transition = vertex.as_transition();
                TimeInterval initial_interval(transition.const_time.first, transition.const_time.second);
                initial_constraints.push_back(initial_interval);
            }
        }

        return std::make_shared<DifferentialStateClass>(initial_marking, initial_constraints);
    }

    SCGVertex DifferentialStateClassAnalyzer::add_state(
        const std::shared_ptr<DifferentialStateClass> &state)
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

    SCGEdge DifferentialStateClassAnalyzer::add_edge(
        SCGVertex source, SCGVertex target,
        ptpn_v_desc transition, const TimeInterval &interval)
    {
        // 创建边属性
        SCGEdgeProperties ep;
        ep.transition = transition;
        ep.time_interval = interval;

        // 获取变迁的名称
        const Vertex &trans_vertex = petri_net[transition];
        ep.label = "t" + trans_vertex.name + " [" +
                   std::to_string(interval.lower) + "," +
                   (std::isinf(interval.upper) ? "∞" : std::to_string(interval.upper)) + "]";

        // 添加边
        auto [e, success] = boost::add_edge(source, target, ep, graph);
        return e;
    }

    std::string DifferentialStateClassAnalyzer::generate_state_label(
        const std::shared_ptr<DifferentialStateClass> &state)
    {
        // 返回状态的字符串表示，用于在DOT文件中显示
        return state->to_string(matrix);
    }

    bool DifferentialStateClassAnalyzer::is_transition_enabled(
        ptpn_v_desc transition, const VectorXd &marking)
    {
        // 检查变迁的所有输入库所是否都有足够的token
        size_t t_index = matrix.get_transition_indices().at(transition);
        size_t num_places = matrix.get_reverse_place_indices().size();

        for (size_t i = 0; i < num_places; ++i)
        {
            if (matrix.get_matrix()(i, num_places + t_index) < 0)
            {
                if (marking(i) < -matrix.get_matrix()(i, num_places + t_index))
                {
                    return false;
                }
            }
        }

        return true;
    }

    VectorXd DifferentialStateClassAnalyzer::update_marking(
        const VectorXd &current_marking, ptpn_v_desc transition)
    {
        VectorXd new_marking = current_marking;
        size_t t_index = matrix.get_transition_indices().at(transition);
        size_t num_places = matrix.get_reverse_place_indices().size();

        // 更新标记
        for (size_t i = 0; i < num_places; ++i)
        {
            new_marking(i) += matrix.get_matrix()(i, num_places + t_index);
        }

        return new_marking;
    }

    std::vector<TimeInterval> DifferentialStateClassAnalyzer::update_time_constraints(
        const std::vector<TimeInterval> &current_constraints,
        ptpn_v_desc fired_transition,
        const TimeInterval &firing_interval,
        const std::vector<ptpn_v_desc> &new_enabled_transitions)
    {
        std::vector<TimeInterval> new_constraints;
        std::unordered_map<ptpn_v_desc, size_t> transition_to_index;

        // 创建从变迁到索引的映射
        for (size_t i = 0; i < matrix.get_reverse_transition_indices().size(); ++i)
        {
            transition_to_index[matrix.get_reverse_transition_indices()[i]] = i;
        }

        // 对于每个在新状态中启用的变迁
        for (const auto &transition : new_enabled_transitions)
        {
            // 检查它是否在原状态中已经启用
            auto it = transition_to_index.find(transition);
            if (it != transition_to_index.end() && it->second < current_constraints.size())
            {
                // 如果不是刚触发的变迁，更新时间约束
                if (transition != fired_transition)
                {
                    // 更新时间区间（考虑时间流逝和变迁触发）
                    TimeInterval updated_interval = current_constraints[it->second];

                    // 调整区间下界（减去触发时间）
                    updated_interval.lower = std::max(0.0, updated_interval.lower - firing_interval.lower);

                    // 调整区间上界（减去触发时间）
                    if (!std::isinf(updated_interval.upper))
                    {
                        updated_interval.upper = updated_interval.upper - firing_interval.lower;
                    }

                    new_constraints.push_back(updated_interval);
                }
                else
                {
                    // 如果是刚触发的变迁，使用其触发区间
                    new_constraints.push_back(firing_interval);
                }
            }
            else
            {
                // 如果是新启用的变迁，添加初始时间约束
                const Vertex &vertex = petri_net[transition];
                const Transition &t = vertex.as_transition();
                TimeInterval initial_interval(t.const_time.first, t.const_time.second);
                new_constraints.push_back(initial_interval);
            }
        }

        return new_constraints;
    }

} // namespace differential_scg