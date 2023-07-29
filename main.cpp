#include <iostream>
#include "timepetrinet.h"


TimePetriNet tpn;
void signalHandler(int signal) {
  if (signal == SIGINT) {
    // 保存数据到文件
    tpn.saveDataToFile("data.txt");

    // 终止程序
    exit(0);
  }
}
int main() {
  // 注册信号处理函数
  signal(SIGINT, signalHandler);
  Config config = {"../test/d.dot", "../test/d.json"};
  config.parse_json();
  config.parse_dag();

  tpn.init_graph();
  tpn.construct_petri_net(config);

  tpn.initial_state_class = tpn.get_initial_state_class();
  //double timeout = 10;
//  tpn.initial_state_class.print_current_mark();
  tpn.generate_state_class();
  return 0;
}
