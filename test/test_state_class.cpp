#include "priority_time_petri_net.h"
#include "priority_state_graph.h"
#include <iostream>
#include <cassert>

int main()
{
    std::cout << "开始测试状态类图生成功能..." << std::endl;

    ptpn::PriorityTPN petri_net;
    std::string test_file = "../test/simple_petri.dot";
    petri_net.import_ptpn_from_dot(test_file);

    std::cout << "Petri网导入完成,开始生成状态类图..." << std::endl;

    priority_scg::PriorityStateClassGraph state_analyzer(petri_net.get_graph());

    state_analyzer.generate_state_class_graph_with_limit(50);
    state_analyzer.print_graph_info();

    // 保存到DOT和JSON文件
    if (!state_analyzer.save_to_dot("../test/state_class_output.dot"))
    {
        std::cerr << "保存DOT文件失败" << std::endl;
        return 1;
    }

    if (!state_analyzer.save_to_json("../test/state_class_output.json"))
    {
        std::cerr << "保存JSON文件失败" << std::endl;
        return 1;
    }

    std::cout << "状态类图已保存到:" << std::endl;
    std::cout << "  - ../test/state_class_output.dot" << std::endl;
    std::cout << "  - ../test/state_class_output.json" << std::endl;

    assert(state_analyzer.get_vertex_count() > 0);
    assert(state_analyzer.get_edge_count() >= 0);

    std::cout << "测试通过！" << std::endl;
    return 0;
}