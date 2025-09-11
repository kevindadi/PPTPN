#ifndef WCRT_CALCULATOR_H
#define WCRT_CALCULATOR_H

#include "task_analysis_types.h"
#include "priority_time_petri_net.h"


namespace task_analysis {

/**
 * @brief WCRT/WCET计算器抽象基类
 * 
 * 定义了WCRT和WCET计算的标准接口
 */
class IWCRTCalculator {
public:
    virtual ~IWCRTCalculator() = default;
    
    /**
     * @brief 计算任务的WCRT
     * @param task_name 任务名称
     * @return WCRT值，-1表示计算失败
     */
    virtual int calculate_wcrt(const std::string& task_name) = 0;
    
    /**
     * @brief 计算任务的WCET
     * @param task_name 任务名称
     * @return WCET值，-1表示计算失败
     */
    virtual int calculate_wcet(const std::string& task_name) = 0;
    
    /**
     * @brief 获取任务路径信息
     * @param task_name 任务名称
     * @return 任务路径信息
     */
    virtual TaskPathInfo<ptpn::ptpn_v_desc> get_task_path_info(const std::string& task_name) = 0;
};

/**
 * @brief 基于Petri网的静态WCRT/WCET计算器
 * 
 * 使用静态路径分析计算WCRT和WCET
 */
class StaticWCRTCalculator : public IWCRTCalculator {
private:
    const ptpn::PriorityTPNGraph& petri_net_;
    
public:
    /**
     * @brief 构造函数
     * @param petri_net Petri网图引用
     */
    explicit StaticWCRTCalculator(const ptpn::PriorityTPNGraph& petri_net)
        : petri_net_(petri_net) {}
    
    /**
     * @brief 计算任务的WCRT
     * @param task_name 任务名称
     * @return WCRT值，-1表示计算失败
     */
    int calculate_wcrt(const std::string& task_name) override;
    
    /**
     * @brief 计算任务的WCET
     * @param task_name 任务名称
     * @return WCET值，-1表示计算失败
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
     * @brief 计算从源顶点到目标顶点的最长路径时间
     * @param source 源顶点
     * @param target 目标顶点
     * @return 最长路径时间，-1表示无法到达
     */
    int compute_longest_path_time(ptpn::ptpn_v_desc source, ptpn::ptpn_v_desc target) const;
    
    /**
     * @brief 在Petri网中查找任务的关键节点
     * @param task_name 任务名称
     * @param node_type 节点类型后缀
     * @return 节点描述符，如果未找到返回null_vertex
     */
    ptpn::ptpn_v_desc find_task_node(const std::string& task_name, const std::string& node_type) const;
};

/**
 * @brief 基于状态类图的动态WCRT/WCET计算器
 * 
 * 使用状态类图中的实际执行路径计算WCRT和WCET
 */
template<typename StateGraphType, typename VertexType>
class DynamicWCRTCalculator : public IWCRTCalculator {
private:
    const ptpn::PriorityTPNGraph& petri_net_;
    const StateGraphType& state_graph_;
    
public:
    /**
     * @brief 构造函数
     * @param petri_net Petri网图引用
     * @param state_graph 状态类图引用
     */
    DynamicWCRTCalculator(const ptpn::PriorityTPNGraph& petri_net, 
                         const StateGraphType& state_graph)
        : petri_net_(petri_net), state_graph_(state_graph) {}
    
    /**
     * @brief 计算任务的WCRT
     * @param task_name 任务名称
     * @return WCRT值，-1表示计算失败
     */
    int calculate_wcrt(const std::string& task_name) override;
    
    /**
     * @brief 计算任务的WCET
     * @param task_name 任务名称
     * @return WCET值，-1表示计算失败
     */
    int calculate_wcet(const std::string& task_name) override;
    
    /**
     * @brief 获取任务路径信息
     * @param task_name 任务名称
     * @return 任务路径信息
     */
    TaskPathInfo<VertexType> get_task_path_info(const std::string& task_name) override;
    
private:
    /**
     * @brief 在状态类图中计算任务的实际执行时间
     * @param task_name 任务名称
     * @param start_node_type 起始节点类型
     * @param end_node_type 结束节点类型
     * @return 最大执行时间
     */
    int compute_execution_time_in_state_graph(const std::string& task_name,
                                             const std::string& start_node_type,
                                             const std::string& end_node_type) const;
};

/**
 * @brief WCRT/WCET计算器工厂类
 * 
 * 使用工厂模式创建不同类型的计算器
 */
class WCRTCalculatorFactory {
public:
    /**
     * @brief 计算器类型枚举
     */
    enum class CalculatorType {
        STATIC,     ///< 静态计算器
        DYNAMIC     ///< 动态计算器
    };
    
    /**
     * @brief 创建静态计算器
     * @param petri_net Petri网图引用
     * @return 静态计算器智能指针
     */
    static std::unique_ptr<IWCRTCalculator> create_static_calculator(
        const ptpn::PriorityTPNGraph& petri_net);
    
    /**
     * @brief 创建动态计算器
     * @param petri_net Petri网图引用
     * @param state_graph 状态类图引用
     * @return 动态计算器智能指针
     */
    template<typename StateGraphType>
    static std::unique_ptr<IWCRTCalculator> create_dynamic_calculator(
        const ptpn::PriorityTPNGraph& petri_net,
        const StateGraphType& state_graph) {
        return std::make_unique<DynamicWCRTCalculator<StateGraphType, typename boost::graph_traits<StateGraphType>::vertex_descriptor>>(
            petri_net, state_graph);
    }
    
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
        const StateGraphType* state_graph = nullptr) {
        switch (type) {
            case CalculatorType::STATIC:
                return create_static_calculator(petri_net);
            case CalculatorType::DYNAMIC:
                if (state_graph) {
                    return create_dynamic_calculator(petri_net, *state_graph);
                }
                break;
        }
        return nullptr;
    }
    
    /**
     * @brief 比较不同计算器的结果
     * @param petri_net Petri网图引用
     * @param task_name 任务名称
     * @return 比较结果字符串
     */
    static std::string compare_calculators(const ptpn::PriorityTPNGraph& petri_net, 
                                          const std::string& task_name);
};

} // namespace task_analysis

#endif // WCRT_CALCULATOR_H
