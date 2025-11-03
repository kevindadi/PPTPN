#ifndef SEMANTIC_WCRT_CALCULATOR_H
#define SEMANTIC_WCRT_CALCULATOR_H

#include "task_analysis_types.h"
#include "wcrt_calculator.h"
#include "priority_time_petri_net.h"
#include "priority_state_class.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>

namespace task_analysis {

/**
 * @brief 基于Petri网语义的WCRT/WCET计算器
 * 
 * 这个计算器考虑了Petri网的发生语义,确保找到的路径在实际执行中是可达的
 */
class SemanticWCRTCalculator : public IWCRTCalculator {
private:
    const ptpn::PriorityTPNGraph& petri_net_;
    
public:
    /**
     * @brief 构造函数
     * @param petri_net Petri网图引用
     */
    explicit SemanticWCRTCalculator(const ptpn::PriorityTPNGraph& petri_net)
        : petri_net_(petri_net) {}
    
    /**
     * @brief 计算任务的WCRT
     * @param task_name 任务名称
     * @return WCRT值,-1表示计算失败
     */
    int calculate_wcrt(const std::string& task_name) override;
    
    /**
     * @brief 计算任务的WCET
     * @param task_name 任务名称
     * @return WCET值,-1表示计算失败
     */
    int calculate_wcet(const std::string& task_name) override;
    
    /**
     * @brief 获取任务路径信息
     * @param task_name 任务名称
     * @return 任务路径信息
     */
    TaskPathInfo<ptpn::ptpn_v_desc> get_task_path_info(const std::string& task_name) override;
    
private:
    /**
     * @brief 基于语义的最长路径计算
     * @param source 源顶点
     * @param target 目标顶点
     * @return 最长路径时间,-1表示无法到达
     */
    int compute_semantic_longest_path_time(ptpn::ptpn_v_desc source, ptpn::ptpn_v_desc target) const;
    
    /**
     * @brief 检查变迁是否在当前状态下使能
     * @param transition 变迁顶点
     * @param marking 当前标记
     * @return 是否使能
     */
    bool is_transition_enabled(ptpn::ptpn_v_desc transition, const priority_scg::Marking& marking) const;
    
    /**
     * @brief 触发变迁,返回新的标记
     * @param transition 变迁顶点
     * @param marking 当前标记
     * @return 新的标记
     */
    priority_scg::Marking fire_transition(ptpn::ptpn_v_desc transition, const priority_scg::Marking& marking) const;
    
    /**
     * @brief 使用BFS找到从source到target的所有可达路径
     * @param source 源顶点
     * @param target 目标顶点
     * @return 所有可达路径及其时间
     */
    std::vector<std::pair<std::vector<ptpn::ptpn_v_desc>, int>> find_reachable_paths(
        ptpn::ptpn_v_desc source, ptpn::ptpn_v_desc target) const;
    
    /**
     * @brief 计算路径的时间
     * @param path 路径（顶点序列）
     * @return 路径时间
     */
    int calculate_path_time(const std::vector<ptpn::ptpn_v_desc>& path) const;
    
    /**
     * @brief 在Petri网中查找任务的关键节点
     * @param task_name 任务名称
     * @param node_type 节点类型后缀
     * @return 节点描述符,如果未找到返回null_vertex
     */
    ptpn::ptpn_v_desc find_task_node(const std::string& task_name, const std::string& node_type) const;
    
    /**
     * @brief 获取初始标记
     * @return 初始标记
     */
    priority_scg::Marking get_initial_marking() const;
    
    /**
     * @brief 检查标记是否包含目标顶点
     * @param marking 标记
     * @param target 目标顶点
     * @return 是否包含
     */
    bool marking_contains_vertex(const priority_scg::Marking& marking, ptpn::ptpn_v_desc target) const;
};

/**
 * @brief 增强的WCRT计算器工厂
 * 
 * 支持创建基于语义的计算器
 */
class EnhancedWCRTCalculatorFactory {
public:
    /**
     * @brief 计算器类型枚举
     */
    enum class CalculatorType {
        STATIC,     ///< 静态计算器（基于图论）
        SEMANTIC,   ///< 语义计算器（考虑Petri网语义）
        DYNAMIC     ///< 动态计算器（基于状态类图）
    };
    
    /**
     * @brief 创建语义计算器
     * @param petri_net Petri网图引用
     * @return 语义计算器智能指针
     */
    static std::unique_ptr<IWCRTCalculator> create_semantic_calculator(
        const ptpn::PriorityTPNGraph& petri_net);
    
    /**
     * @brief 根据类型创建计算器
     * @param type 计算器类型
     * @param petri_net Petri网图引用
     * @param state_graph 状态类图引用(可选)
     * @return 计算器智能指针
     */
    template<typename StateGraphType>
    static std::unique_ptr<IWCRTCalculator> create_calculator(
        CalculatorType type,
        const ptpn::PriorityTPNGraph& petri_net,
        const StateGraphType* state_graph = nullptr);
    
    /**
     * @brief 比较不同计算器的结果
     * @param petri_net Petri网图引用
     * @param task_name 任务名称
     * @return 比较结果
     */
    static std::string compare_calculators(const ptpn::PriorityTPNGraph& petri_net, 
                                          const std::string& task_name);
};

} // namespace task_analysis

#endif // SEMANTIC_WCRT_CALCULATOR_H
