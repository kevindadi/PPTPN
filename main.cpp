#include <iostream>
#include "timepetrinet.h"

int main() {
  Config config = {"../demo.dot", "../data.json"};
  config.parse_json();
  config.parse_dag();
  TimePetriNet tpn;
  tpn.init_graph();
  tpn.construct_petri_net(config);
  return 0;
}
