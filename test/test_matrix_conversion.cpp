#include "matrix_ptpn.h"
#include "graph_ptpn.h"
#include "clap.h"
#include <iostream>
#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <iomanip>

using namespace std;
// using namespace ptpn;
using namespace boost;
using namespace graph_ptpn;

int main(int argc, char* argv[]) {  
    boost::log::add_console_log(std::clog);
    boost::log::add_common_attributes();
    boost::log::core::get()->set_filter(
        boost::log::trivial::severity >= boost::log::trivial::info);
    
    try {
        std::string file_path = "../example/common.dot";
        int num_cpus = 6;
        int cores_per_cpu = 1;
        
        if (argc > 1) {
            file_path = argv[1];
        }
        if (argc > 2) {
            num_cpus = std::stoi(argv[2]);
        }
        if (argc > 3) {
            cores_per_cpu = std::stoi(argv[3]);
        }
        
        std::cout << "=== TDG 到矩阵形式 PTPN 转换测试 ===" << std::endl;
        std::cout << "输入文件: " << file_path << std::endl;
        std::cout << "CPU数量: " << num_cpus << std::endl;
        std::cout << "每CPU核心数: " << cores_per_cpu << std::endl;
        std::cout << std::endl; 
        
        std::cout << "[步骤 1] 解析 TDG 文件..." << std::endl;
        TDG tdg(file_path, num_cpus, cores_per_cpu);
        tdg.parse_tdg();
        
        std::cout << "TDG 解析完成:" << std::endl;
        std::cout << "  任务数量: " << tdg.all_task.size() << std::endl;
        std::cout << "  节点类型数量: " << tdg.nodes_type.size() << std::endl;
        std::cout << std::endl;
        
        std::cout << "[步骤 2] 转换到矩阵形式 PTPN..." << std::endl;
        matrix_ptpn::MatrixPTPN matrix_net;
        matrix_net.transform_tdg_to_matrix_ptpn(tdg);
        
        std::cout << "转换完成!" << std::endl;
        std::cout << std::endl;
        
        std::cout << "[步骤 3] 转换结果统计:" << std::endl;
        std::cout << "  库所数量: " << matrix_net.num_places() << std::endl;
        std::cout << "  变迁数量: " << matrix_net.num_transitions() << std::endl;
        std::cout << "  初始标识: ";
        const auto& marking = matrix_net.get_marking();
        for (size_t i = 0; i < marking.size(); ++i) {
            if (marking[i] > 0) {
                std::cout << "P" << i << "=" << marking[i] << " ";
            }
        }
        std::cout << std::endl;
        std::cout << std::endl;
        
        std::cout << "[步骤 4] 全部变迁信息:" << std::endl;
        std::cout << std::left << std::setw(10) << "索引" 
                  << std::setw(50) << "名称"
                  << std::setw(12) << "优先级"
                  << std::setw(8) << "核心"
                  << std::setw(25) << "时间区间"
                  << std::setw(12) << "可挂起" << std::endl;
        std::cout << std::string(103, '-') << std::endl;
        for (size_t t = 0; t < matrix_net.num_transitions(); ++t) {
            const auto& trans = matrix_net.get_transition(t);
            std::cout << std::left << std::setw(10) << ("T" + std::to_string(t))
                      << std::setw(50) << trans.name
                      << std::setw(12) << trans.priority
                      << std::setw(8) << trans.core
                      << std::setw(25) << trans.time_interval.to_string()
                      << std::setw(12) << (trans.suspendable ? "是" : "否") << std::endl;
        }
        std::cout << std::endl;
        
        std::cout << "[步骤 5] 全部库所信息:" << std::endl;
        std::cout << std::left << std::setw(10) << "索引" 
                  << std::setw(50) << "名称"
                  << std::setw(12) << "初始token"
                  << std::setw(12) << "容量" << std::endl;
        std::cout << std::string(103, '-') << std::endl;
        for (size_t p = 0; p < matrix_net.num_places(); ++p) {
            const auto& place = matrix_net.get_place(p);
            std::cout << std::left << std::setw(10) << ("P" + std::to_string(p))
                      << std::setw(50) << place.name
                      << std::setw(12) << marking[p]
                      << std::setw(12) << (place.capacity == matrix_ptpn::INF ? "∞" : std::to_string(place.capacity)) << std::endl;
        }
        std::cout << std::endl;
        
        std::cout << "[步骤 6] 测试使能变迁..." << std::endl;
        auto enabled = matrix_net.get_enabled_transitions();
        std::cout << "  使能变迁数量: " << enabled.size() << std::endl;
        if (!enabled.empty()) {
            std::cout << "  使能变迁: ";
            for (size_t t : enabled) {
                const auto& trans = matrix_net.get_transition(t);
                std::cout << "T" << t << "(" << trans.name << ") ";
            }
            std::cout << std::endl;
            
            auto schedulable = matrix_net.filter_by_core_and_priority(enabled);
            std::cout << "  可调度变迁数量: " << schedulable.size() << std::endl;
            if (!schedulable.empty()) {
                std::cout << "  可调度变迁: ";
                for (size_t t : schedulable) {
                    const auto& trans = matrix_net.get_transition(t);
                    std::cout << "T" << t << "(" << trans.name 
                              << ", core=" << trans.core << ", priority=" << trans.priority << ") ";
                }
                std::cout << std::endl;
            }
        }
        std::cout << std::endl;
        
        std::cout << "[步骤 7] 矩阵维度信息:" << std::endl;
        const auto& Pre = matrix_net.get_pre_matrix();
        const auto& Post = matrix_net.get_post_matrix();
        std::cout << "  Pre矩阵: " << Pre.size() << "x" << (Pre.empty() ? 0 : Pre[0].size()) << std::endl;
        std::cout << "  Post矩阵: " << Post.size() << "x" << (Post.empty() ? 0 : Post[0].size()) << std::endl;
        
        int pre_nonzero = 0;
        for (const auto& row : Pre) {
            for (int val : row) {
                if (val > 0) pre_nonzero++;
            }
        }
        int post_nonzero = 0;
        for (const auto& row : Post) {
            for (int val : row) {
                if (val > 0) post_nonzero++;
            }
        }
        std::cout << "  Pre矩阵非零元素: " << pre_nonzero << std::endl;
        std::cout << "  Post矩阵非零元素: " << post_nonzero << std::endl;
        std::cout << std::endl;
        
        std::cout << "[步骤 8] 验证网络结构..." << std::endl;
        if (matrix_net.verify_structure()) {
            std::cout << "  结构验证通过!" << std::endl;
        } else {
            std::cout << "  结构验证失败!" << std::endl;
        }
        std::cout << std::endl;
        
        // std::cout << "[步骤 9] 测试导出功能(使用graph_ptpn模块)..." << std::endl;
        // GraphPTPN graph_ptpn(matrix_net);
        // std::string output_dot = "test_matrix_output.dot";
        // if (graph_ptpn.save_to_dot(output_dot)) {
        //     std::cout << "  DOT文件已保存: " << output_dot << std::endl;
        // } else {
        //     std::cout << "  DOT文件保存失败!" << std::endl;
        // }
        // std::cout << std::endl;
        
        // std::cout << "[步骤 10] 对比测试:转换为 Boost Graph 形式..." << std::endl;
        // PriorityTPN boost_net;
        // boost_net.transform_tdg_to_ptpn(tdg);
        // std::cout << "  Boost Graph形式转换完成" << std::endl;
        // std::cout << "  库所+变迁数量: " << num_vertices(boost_net.get_graph()) << std::endl;
        // std::cout << "  边数量: " << num_edges(boost_net.get_graph()) << std::endl;
        
        // std::cout << std::endl;
        // std::cout << "=== 测试完成 ===" << std::endl;
        // std::cout << "矩阵形式 PTPN: " << matrix_net.num_places() << " 个库所, " 
        //           << matrix_net.num_transitions() << " 个变迁" << std::endl;
        // std::cout << "Boost Graph形式: " << num_vertices(boost_net.get_graph()) 
        //           << " 个节点" << std::endl;
        // std::cout << "GraphPTPN形式: " << num_vertices(graph_ptpn.get_graph()) 
        //           << " 个节点" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

