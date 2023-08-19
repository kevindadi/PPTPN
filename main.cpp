// #include "timepetrinet.h"
#include <iostream>

// void signalHandler(int signal) {
//   if (signal == SIGINT) {
//     // 保存数据到文件
//     tpn.saveDataToFile("data.txt");
//
//     // 终止程序
//     exit(0);
//   }
// }
// DAG --------------------------------
// int main() {
//  // 注册信号处理函数
//  // signal(SIGINT, signalHandler);
//  Config config = {"../test/d.dot", "../test/six_priority.json"};
//  config.parse_json();
//  config.parse_dag();
//    TimePetriNet tpn;
//    tpn.init_graph();
//    tpn.construct_petri_net(config);
//
//    tpn.initial_state_class = tpn.get_initial_state_class();
//   double timeout = 10;
//    tpn.initial_state_class.print_current_mark();
//    tpn.generate_state_class();
//
//  return 0;
//}
// SubDAG --------------------------------
#include "probpetrinet.h"
#include <boost/program_options.hpp>

namespace po = boost::program_options;

int main(int argc, char *argv[]) {
  int deadline;
  std::string file_path;
  po::options_description desc("Allowed options");
  desc.add_options()("help", "produce help message")(
      "deadline", po::value<int>(&deadline)->default_value(0),
      "check deadline")(
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
  char *dag_file = const_cast<char *>(file_path.c_str());
  GConfig config(dag_file);

  ProbPetriNet probtpn;
  probtpn.init();
  probtpn.construct_sub_ptpn(config);
  probtpn.initial_state_class = probtpn.get_initial_state_class();
  probtpn.initial_state_class.print_current_mark();
  probtpn.generate_state_class();

  return 0;
}
