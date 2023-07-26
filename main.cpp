#include <iostream>
#include "timepetrinet.h"
#include <boost/chrono.hpp>
#include <boost/chrono/include.hpp>

int main() {
  Config config = {"../test1.dot", "../test/six_priority.json"};
  config.parse_json();
  config.parse_dag();
  TimePetriNet tpn;
  tpn.init_graph();
  tpn.construct_petri_net(config);

  tpn.initial_state_class = tpn.get_initial_state_class();
   double timeout = 10;
//  tpn.initial_state_class.print_current_mark();
  tpn.generate_state_class(timeout);
  return 0;
}
