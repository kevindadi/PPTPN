#ifndef DEADLOCK_DETECTOR_H
#define DEADLOCK_DETECTOR_H

#include "task_analysis_types.h"
#include "priority_time_petri_net.h"
#include <unordered_set>
#include <vector>

namespace task_analysis {

/**
 * @brief 死锁检测器抽象基类
 * 
 * 定义了死锁检测的标准接口
 */
class IDeadlockDetector {
public:
    virtual ~IDeadlockDetector() = default;
    
    /**
     * @brief 检测死锁
     * @return 死锁分析结果
     */
    virtual DeadlockAnalysisResult detect_deadlocks() = 0;
    
    /**
     * @brief 检查特定任务是否包含死锁
     * @param task_name 任务名称
     * @return 是否包含死锁
     */
    virtual bool has_deadlock(const std::string& task_name) = 0;
};

/**
 * @brief 基于状态类图的死锁检测器
 * 
 * 使用深度优先搜索检测状态类图中的死锁
 */
template<typename StateGraphType, typename VertexType, typename StateType>
class StateGraphDeadlockDetector : public IDeadlockDetector {
private:
    const StateGraphType& state_graph_;
    const ptpn::PriorityTPNGraph& petri_net_;
    
public:
    /**
     * @brief 构造函数
     * @param state_graph 状态类图引用
     * @param petri_net Petri网图引用
     */
    StateGraphDeadlockDetector(const StateGraphType& state_graph,
                              const ptpn::PriorityTPNGraph& petri_net)
        : state_graph_(state_graph), petri_net_(petri_net) {}
    
    /**
     * @brief 检测死锁
     * @return 死锁分析结果
     */
    DeadlockAnalysisResult detect_deadlocks() override;
    
    /**
     * @brief 检查特定任务是否包含死锁
     * @param task_name 任务名称
     * @return 是否包含死锁
     */
    bool has_deadlock(const std::string& task_name) override;
    
private:
    /**
     * @brief 使用DFS检测循环依赖
     * @param start_vertex 起始顶点
     * @param visited 已访问顶点集合
     * @param recursion_stack 递归栈
     * @param deadlock_states 死锁状态列表
     * @return 是否发现死锁
     */
    bool dfs_detect_cycles(VertexType start_vertex,
                          std::unordered_set<VertexType>& visited,
                          std::unordered_set<VertexType>& recursion_stack,
                          std::vector<std::string>& deadlock_states) const;
    
    /**
     * @brief 检查状态是否为死锁状态
     * @param vertex 状态顶点
     * @return 是否为死锁状态
     */
    bool is_deadlock_state(VertexType vertex) const;
    
    /**
     * @brief 检查任务相关的死锁路径
     * @param task_name 任务名称
     * @return 是否包含死锁
     */
    bool check_task_deadlock_paths(const std::string& task_name) const;
    
    /**
     * @brief 构建死锁循环信息
     * @param deadlock_states 死锁状态列表
     * @return 死锁循环信息列表
     */
    std::vector<std::string> build_deadlock_cycles(const std::vector<std::string>& deadlock_states) const;
};

/**
 * @brief 基于Petri网的死锁检测器
 * 
 * 使用Petri网结构分析潜在的死锁情况
 */
class PetriNetDeadlockDetector : public IDeadlockDetector {
private:
    const ptpn::PriorityTPNGraph& petri_net_;
    
public:
    /**
     * @brief 构造函数
     * @param petri_net Petri网图引用
     */
    explicit PetriNetDeadlockDetector(const ptpn::PriorityTPNGraph& petri_net)
        : petri_net_(petri_net) {}
    
    /**
     * @brief 检测死锁
     * @return 死锁分析结果
     */
    DeadlockAnalysisResult detect_deadlocks() override;
    
    /**
     * @brief 检查特定任务是否包含死锁
     * @param task_name 任务名称
     * @return 是否包含死锁
     */
    bool has_deadlock(const std::string& task_name) override;
    
private:
    /**
     * @brief 检测资源死锁
     * @return 死锁分析结果
     */
    DeadlockAnalysisResult detect_resource_deadlocks() const;
    
    /**
     * @brief 检测循环等待死锁
     * @return 死锁分析结果
     */
    DeadlockAnalysisResult detect_circular_wait() const;
    
    /**
     * @brief 检查变迁是否可能死锁
     * @param transition 变迁顶点
     * @return 是否可能死锁
     */
    bool is_transition_deadlock_prone(ptpn::ptpn_v_desc transition) const;
};

/**
 * @brief 死锁检测器工厂类
 * 
 * 使用工厂模式创建不同类型的死锁检测器
 */
class DeadlockDetectorFactory {
public:
    /**
     * @brief 检测器类型枚举
     */
    enum class DetectorType {
        STATE_GRAPH,    ///< 状态类图检测器
        PETRI_NET       ///< Petri网检测器
    };
    
    /**
     * @brief 创建状态类图检测器
     * @param state_graph 状态类图引用
     * @param petri_net Petri网图引用
     * @return 状态类图检测器智能指针
     */
    template<typename StateGraphType, typename VertexType, typename StateType>
    static std::unique_ptr<IDeadlockDetector> create_state_graph_detector(
        const StateGraphType& state_graph,
        const ptpn::PriorityTPNGraph& petri_net) {
        return std::make_unique<StateGraphDeadlockDetector<StateGraphType, VertexType, StateType>>(
            state_graph, petri_net);
    }
    
    /**
     * @brief 创建Petri网检测器
     * @param petri_net Petri网图引用
     * @return Petri网检测器智能指针
     */
    static std::unique_ptr<IDeadlockDetector> create_petri_net_detector(
        const ptpn::PriorityTPNGraph& petri_net) {
        return std::make_unique<PetriNetDeadlockDetector>(petri_net);
    }
    
    /**
     * @brief 根据类型创建检测器
     * @param type 检测器类型
     * @param petri_net Petri网图引用
     * @param state_graph 状态类图引用(可选)
     * @return 检测器智能指针
     */
    template<typename StateGraphType, typename VertexType, typename StateType>
    static std::unique_ptr<IDeadlockDetector> create_detector(
        DetectorType type,
        const ptpn::PriorityTPNGraph& petri_net,
        const StateGraphType* state_graph = nullptr) {
        switch (type) {
            case DetectorType::STATE_GRAPH:
                if (state_graph) {
                    return create_state_graph_detector<StateGraphType, VertexType, StateType>(
                        *state_graph, petri_net);
                }
                break;
            case DetectorType::PETRI_NET:
                return create_petri_net_detector(petri_net);
        }
        return nullptr;
    }
};

/**
 * @brief 死锁分析工具类
 * 
 * 提供死锁分析的辅助功能
 */
class DeadlockAnalysisUtils {
public:
    /**
     * @brief 分析死锁原因
     * @param deadlock_states 死锁状态列表
     * @param petri_net Petri网图引用
     * @return 死锁原因描述
     */
    static std::string analyze_deadlock_reason(const std::vector<std::string>& deadlock_states,
                                              const ptpn::PriorityTPNGraph& petri_net);
    
    /**
     * @brief 生成死锁报告
     * @param result 死锁分析结果
     * @return 格式化的死锁报告
     */
    static std::string generate_deadlock_report(const DeadlockAnalysisResult& result);
    
    /**
     * @brief 检查死锁严重程度
     * @param result 死锁分析结果
     * @return 严重程度等级(1-5)
     */
    static int assess_deadlock_severity(const DeadlockAnalysisResult& result);
};

} // namespace task_analysis

#endif // DEADLOCK_DETECTOR_H
