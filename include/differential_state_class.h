#ifndef PPTPN_INCLUDE_DIFFERENTIAL_STATE_CLASS_H
#define PPTPN_INCLUDE_DIFFERENTIAL_STATE_CLASS_H

#include "priority_time_petri_net.h"
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <eigen3/Eigen/Dense>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace boost;
using namespace boost::multiprecision;
using namespace ptpn;
using namespace Eigen;

namespace differential_scg
{

    // 时间区间表示
    struct TimeInterval
    {
        double lower; // 下界
        double upper; // 上界，如果是无穷大则使用std::numeric_limits<double>::infinity()

        TimeInterval(double lower = 0, double upper = std::numeric_limits<double>::infinity())
            : lower(lower), upper(upper) {}

        // 区间交集
        TimeInterval intersect(const TimeInterval &other) const
        {
            return TimeInterval(std::max(lower, other.lower), std::min(upper, other.upper));
        }

        // 区间是否有效
        bool is_valid() const { return lower <= upper; }

        // 区间相等
        bool operator==(const TimeInterval &other) const
        {
            return std::abs(lower - other.lower) < 1e-10 &&
                   std::abs(upper - other.upper) < 1e-10;
        }
    };

    // 差分边界矩阵
    class DifferentialBoundaryMatrix
    {
    public:
        DifferentialBoundaryMatrix(const PriorityTPNGraph &graph);

        // 获取矩阵维度
        size_t get_dimension() const { return dimension; }

        // 获取矩阵
        const MatrixXd &get_matrix() const { return matrix; }

        // 获取变迁索引映射
        const std::map<ptpn_v_desc, size_t> &get_transition_indices() const { return transition_indices; }

        // 获取库所索引映射
        const std::map<ptpn_v_desc, size_t> &get_place_indices() const { return place_indices; }

        // 获取反向索引映射
        const std::vector<ptpn_v_desc> &get_reverse_transition_indices() const { return reverse_transition_indices; }
        const std::vector<ptpn_v_desc> &get_reverse_place_indices() const { return reverse_place_indices; }

    private:
        MatrixXd matrix;                                     // 差分边界矩阵
        size_t dimension;                                    // 矩阵维度
        std::map<ptpn_v_desc, size_t> transition_indices;    // 变迁到矩阵索引的映射
        std::map<ptpn_v_desc, size_t> place_indices;         // 库所到矩阵索引的映射
        std::vector<ptpn_v_desc> reverse_transition_indices; // 矩阵索引到变迁的反向映射
        std::vector<ptpn_v_desc> reverse_place_indices;      // 矩阵索引到库所的反向映射

        // 初始化矩阵和索引映射
        void initialize_matrix(const PriorityTPNGraph &graph);
    };

    // 状态类表示
    class DifferentialStateClass
    {
    public:
        DifferentialStateClass() = default;
        DifferentialStateClass(const VectorXd &m, const std::vector<TimeInterval> &tc)
            : marking(m), time_constraints(tc) {}

        // 状态类的等价性判断
        bool operator==(const DifferentialStateClass &other) const;

        // 获取状态类的字符串表示
        std::string to_string(const DifferentialBoundaryMatrix &matrix) const;

        // 计算后继状态类
        std::shared_ptr<DifferentialStateClass> compute_successor(
            const DifferentialBoundaryMatrix &matrix,
            const PriorityTPNGraph &petri_net,
            ptpn_v_desc fired_transition,
            const TimeInterval &firing_interval) const;

        // 获取当前标记中的所有可启用变迁
        std::vector<ptpn_v_desc> get_enabled_transitions(const DifferentialBoundaryMatrix &matrix) const;

        // 考虑优先级规则过滤可启用的变迁
        std::vector<ptpn_v_desc> filter_by_priority(
            const std::vector<ptpn_v_desc> &enabled_transitions,
            const PriorityTPNGraph &graph) const;

        // 获取当前标记
        const VectorXd &get_marking() const { return marking; }

        // 获取时间约束
        const std::vector<TimeInterval> &get_time_constraints() const { return time_constraints; }

    private:
        VectorXd marking;                           // 标记向量
        std::vector<TimeInterval> time_constraints; // 时间约束
    };

    // 状态类图的顶点属性
    struct SCGVertexProperties
    {
        std::string id;
        std::string label;
        std::string shape = "ellipse"; // 默认形状为椭圆
        std::shared_ptr<DifferentialStateClass> state;
    };

    // 状态类图的边属性
    struct SCGEdgeProperties
    {
        ptpn_v_desc transition;
        std::string label;
        TimeInterval time_interval;
    };

    // 状态类图的属性
    struct SCGProperties
    {
        std::string name;
    };

    // 状态类图定义
    typedef boost::adjacency_list<
        boost::vecS, boost::vecS, boost::directedS,
        SCGVertexProperties, SCGEdgeProperties, SCGProperties>
        StateClassGraph;

    typedef boost::graph_traits<StateClassGraph>::vertex_descriptor SCGVertex;
    typedef boost::graph_traits<StateClassGraph>::edge_descriptor SCGEdge;

    // 基于差分边界矩阵的状态类图分析类
    class DifferentialStateClassAnalyzer
    {
    public:
        DifferentialStateClassAnalyzer(const PriorityTPNGraph &petri_net);

        // 生成状态类图
        void generate_state_class_graph();

        // 导出为DOT格式
        void export_to_dot(const std::string &filename);

        // 检查是否有死锁状态
        bool has_deadlock_states() const;

        // 获取死锁状态
        std::vector<SCGVertex> get_deadlock_states() const;

        // 计算最大执行时间
        TimeInterval calculate_max_execution_time() const;

        // 检查可达性
        bool is_marking_reachable(const VectorXd &target_marking) const;

    private:
        PriorityTPNGraph petri_net;        // 原始的优先级时间 Petri 网
        DifferentialBoundaryMatrix matrix; // 差分边界矩阵
        StateClassGraph graph;             // 状态类图

        // 计算初始状态类
        std::shared_ptr<DifferentialStateClass> compute_initial_state();

        // 添加状态类到图中
        SCGVertex add_state(const std::shared_ptr<DifferentialStateClass> &state);

        // 添加状态转换（边）到图中
        SCGEdge add_edge(SCGVertex source, SCGVertex target,
                         ptpn_v_desc transition, const TimeInterval &interval);

        // 生成状态标签
        std::string generate_state_label(const std::shared_ptr<DifferentialStateClass> &state);

        // 判断变迁是否可启用
        bool is_transition_enabled(ptpn_v_desc transition, const VectorXd &marking);

        // 更新标记（触发变迁后）
        VectorXd update_marking(const VectorXd &current_marking, ptpn_v_desc transition);

        // 更新时间约束（触发变迁后）
        std::vector<TimeInterval> update_time_constraints(
            const std::vector<TimeInterval> &current_constraints,
            ptpn_v_desc fired_transition,
            const TimeInterval &firing_interval,
            const std::vector<ptpn_v_desc> &new_enabled_transitions);
    };

} // namespace differential_scg

#endif // PPTPN_INCLUDE_DIFFERENTIAL_STATE_CLASS_H