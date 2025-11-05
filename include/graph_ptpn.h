#ifndef GRAPH_PTPN_H
#define GRAPH_PTPN_H

#include "matrix_ptpn.h"
#include "priority_time_petri_net.h"
#include <string>
#include <memory>

namespace graph_ptpn {

/**
 * GraphPTPN类：负责所有与图相关的操作（读取、导出等）
 * 将矩阵形式的PTPN转换为Boost Graph形式，用于导出和可视化
 */
class GraphPTPN {
public:
    explicit GraphPTPN(const matrix_ptpn::MatrixPTPN& matrix_ptpn);
    
    static matrix_ptpn::MatrixPTPN import_from_dot(const std::string& file_path);
    
    bool save_to_dot(const std::string& file_path) const;
    
    bool export_to_tina(const std::string& file_path) const;
    
    bool export_to_romeo(const std::string& file_path) const;
    
    const ptpn::PriorityTPNGraph& get_graph() const { return graph; }

private:
    void convert_matrix_to_graph(const matrix_ptpn::MatrixPTPN& matrix_ptpn);
    
    ptpn::PriorityTPNGraph graph;
};

} // namespace graph_ptpn

#endif // GRAPH_PTPN_H

