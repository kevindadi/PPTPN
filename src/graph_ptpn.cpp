#include "graph_ptpn.h"
#include <boost/log/trivial.hpp>
#include <boost/graph/graphviz.hpp>
#include <limits>
#include <map>

namespace graph_ptpn {

using namespace ptpn;
using namespace boost;

GraphPTPN::GraphPTPN(const matrix_ptpn::MatrixPTPN& matrix_ptpn) {
    convert_matrix_to_graph(matrix_ptpn);
}

void GraphPTPN::convert_matrix_to_graph(const matrix_ptpn::MatrixPTPN& matrix_ptpn) {
    try {
        BOOST_LOG_TRIVIAL(info) << "[GRAPH_PTPN] 开始将矩阵形式转换为Boost Graph形式...";
        
        graph.clear();
        std::map<size_t, ptpn_v_desc> place_to_vertex;
        std::map<size_t, ptpn_v_desc> transition_to_vertex;
        
        const auto& marking = matrix_ptpn.get_marking();
        
        for (size_t p = 0; p < matrix_ptpn.num_places(); ++p) {
            const auto& place = matrix_ptpn.get_place(p);
            ptpn_v_desc v = ptpn::add_place(graph, place.name, marking[p], place.capacity);
            place_to_vertex[p] = v;
        }
        
        for (size_t t = 0; t < matrix_ptpn.num_transitions(); ++t) {
            const auto& trans = matrix_ptpn.get_transition(t);
            std::pair<int, int> const_time(
                trans.time_interval.earliest,
                trans.time_interval.latest == matrix_ptpn::INF ?
                    std::numeric_limits<int>::max() :
                    trans.time_interval.latest
            );
            ptpn_v_desc v = ptpn::add_transition(
                graph, 
                trans.name,
                trans.priority,
                trans.core,
                const_time,
                trans.suspendable,
                {0, 0}
            );
            transition_to_vertex[t] = v;
        }
        
        const auto& Pre = matrix_ptpn.get_pre_matrix();
        for (size_t p = 0; p < Pre.size(); ++p) {
            for (size_t t = 0; t < Pre[p].size(); ++t) {
                if (Pre[p][t] > 0) {
                    Edge e;
                    e.weight = Pre[p][t];
                    add_edge(place_to_vertex[p], transition_to_vertex[t], e, graph);
                }
            }
        }
        
        const auto& Post = matrix_ptpn.get_post_matrix();
        for (size_t t = 0; t < Post.size(); ++t) {
            for (size_t p = 0; p < Post[t].size(); ++p) {
                if (Post[t][p] > 0) {
                    Edge e;
                    e.weight = Post[t][p];
                    add_edge(transition_to_vertex[t], place_to_vertex[p], e, graph);
                }
            }
        }
        
        BOOST_LOG_TRIVIAL(info) << "[GRAPH_PTPN] 转换完成: " << num_vertices(graph) 
                                << " 个节点, " << num_edges(graph) << " 条边";
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[GRAPH_PTPN] 转换失败: " << e.what();
        throw;
    }
}

bool GraphPTPN::save_to_dot(const std::string& file_path) const {
    try {
        PriorityTPN ptpn(graph);
        ptpn.save_ptpn_and_dot(file_path);
        BOOST_LOG_TRIVIAL(info) << "[GRAPH_PTPN] DOT文件已保存到: " << file_path;
        return true;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[GRAPH_PTPN] 保存DOT文件时发生错误: " << e.what();
        return false;
    }
}

bool GraphPTPN::export_to_tina(const std::string& file_path) const {
    try {
        PriorityTPN ptpn(graph);
        bool result = ptpn.export_to_tina(file_path);
        if (result) {
            BOOST_LOG_TRIVIAL(info) << "[GRAPH_PTPN] Tina文件已导出到: " << file_path;
        }
        return result;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[GRAPH_PTPN] 导出Tina文件时发生错误: " << e.what();
        return false;
    }
}

bool GraphPTPN::export_to_romeo(const std::string& file_path) const {
    try {
        PriorityTPN ptpn(graph);
        bool result = ptpn.export_to_romeo(file_path);
        if (result) {
            BOOST_LOG_TRIVIAL(info) << "[GRAPH_PTPN] Romeo文件已导出到: " << file_path;
        }
        return result;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[GRAPH_PTPN] 导出Romeo文件时发生错误: " << e.what();
        return false;
    }
}

matrix_ptpn::MatrixPTPN GraphPTPN::import_from_dot(const std::string& file_path) {
    BOOST_LOG_TRIVIAL(info) << "[GRAPH_PTPN] 从DOT文件导入: " << file_path;
    PriorityTPN ptpn;
    ptpn.import_ptpn_from_dot(file_path);
    
    // TODO: 实现Boost Graph到矩阵形式的转换
    matrix_ptpn::MatrixPTPN matrix_net;
    BOOST_LOG_TRIVIAL(warning) << "[GRAPH_PTPN] import_from_dot 尚未完全实现";
    return matrix_net;
}

} // namespace graph_ptpn

