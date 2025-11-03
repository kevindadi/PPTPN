#include "priority_time_petri_net.h"
#include <iostream>
#include <cassert>

int main()
{
    std::cout << "开始测试Petri网导入功能..." << std::endl;

    // 创建PriorityTPN对象
    ptpn::PriorityTPN petri_net;

    // 导入DOT文件
    std::string test_file = "../test/simple_petri.dot";
    petri_net.import_ptpn_from_dot(test_file);

    // 验证导入结果
    const auto &graph = petri_net.get_graph();

    std::cout << "导入完成,图信息:" << std::endl;
    std::cout << "  节点数量: " << boost::num_vertices(graph) << std::endl;
    std::cout << "  边数量: " << boost::num_edges(graph) << std::endl;

    // 验证节点
    for (auto [vi, vi_end] = boost::vertices(graph); vi != vi_end; ++vi)
    {
        const auto &vertex = graph[*vi];
        std::cout << "节点: " << vertex.name << " (";

        if (vertex.is_place())
        {
            const auto &place = vertex.as_place();
            std::cout << "库所, token=" << place.token << ", capacity=" << place.capacity;
        }
        else if (vertex.is_transition())
        {
            const auto &transition = vertex.as_transition();
            std::cout << "变迁, priority=" << transition.priority
                      << ", core=" << transition.core
                      << ", time=[" << transition.const_time.first
                      << "," << transition.const_time.second << "]";
        }

        std::cout << ")" << std::endl;
    }

    // 验证边
    for (auto [ei, ei_end] = boost::edges(graph); ei != ei_end; ++ei)
    {
        auto source = boost::source(*ei, graph);
        auto target = boost::target(*ei, graph);
        const auto &edge = graph[*ei];

        std::cout << "边: " << graph[source].name << " -> " << graph[target].name
                  << " (weight=" << edge.weight << ")" << std::endl;
    }

    // 基本断言
    assert(boost::num_vertices(graph) == 5); // 3个库所 + 2个变迁
    assert(boost::num_edges(graph) == 4);    // 4条边

    std::cout << "测试通过！" << std::endl;
    return 0;
}