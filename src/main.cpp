// #include "timepetrinet.h"
#include <iostream>

#include "priority_time_petri_net.h"
// TimePetriNet tpn;
//  DAG --------------------------------
//  int main() {
//   // 注册信号处理函数
//   // signal(SIGINT, signalHandler);
//   Config config = {"../test/d.dot", "../test/six_priority.json"};
//   config.parse_json();
//   config.parse_dag();
//     TimePetriNet tpn;
//     tpn.init_graph();
//     tpn.construct_petri_net(config);
//
//     tpn.initial_state_class = tpn.get_initial_state_class();
//    double timeout = 10;
//     tpn.initial_state_class.print_current_mark();
//     tpn.generate_state_class();
//
//   return 0;
// }
//  SubDAG --------------------------------
// #include "probpetrinet.h"
#include <boost/program_options.hpp>

#include "clap.h"

namespace po = boost::program_options;
// void signalHandler(int signal)
//{
//   if (signal == SIGINT)
//   {
//     // 保存数据到文件
//     tpn.state_class_graph.write_to_dot("scg.dot");
//
//     // 终止程序
//     exit(0);
//   }
// };

int main(int argc, char *argv[]) {
  int deadline;
  std::string file_path;
  std::string dot_style;
  po::options_description desc("Allowed options");
  desc.add_options()("help", "produce help message")(
      "deadline", po::value<int>(&deadline)->default_value(0),
      "check deadline")(
      "style", po::value<std::string>(&dot_style)->default_value("NEWPN"),
      "dot style only support PSTPN or PTPN")(
      "file", po::value<std::string>(&file_path)->default_value("dag.dot"),
      "petri net with dot file");
  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);
  if (vm.count("help")) {
    std::cout << desc << std::endl;
    return 1;
  }
  // 注册信号处理函数
  // signal(SIGINT, signalHandler);
  if (dot_style.find("PSTPN") != std::string::npos) {
    char *dag_file = const_cast<char *>(file_path.c_str());
    //    GConfig config(dag_file);
    //
    //    ProbPetriNet probtpn;
    //    probtpn.init();
    //    probtpn.construct_sub_ptpn(config);
    //    probtpn.initial_state_class = probtpn.get_initial_state_class();
    //    probtpn.initial_state_class.print_current_mark();
    //    probtpn.generate_state_class();
    //    probtpn.state_class_graph.task_wcet();
  } else if (dot_style.find("PTPN") != std::string::npos) {
    //    signal(SIGINT, signalHandler);
    //    Config config = {"./test/d.dot", "./test/six_priority.json"};
    //    config.parse_json();
    //    config.parse_dag();
    //
    //    tpn.init_graph();
    //    tpn.construct_petri_net(config);
    //
    //    tpn.initial_state_class = tpn.get_initial_state_class();
    //    double timeout = 10;
    //    tpn.initial_state_class.print_current_state();
    //    tpn.generate_state_class();
    //
  } else {
    TDG tdg_rap = {"../test/label.dot"};
    tdg_rap.parse_tdg();
    tdg_rap.classify_priority();
    PriorityTimePetriNet ptpn;
    ptpn.transform_tdg_to_ptpn(tdg_rap);
  }

  return 0;
}
