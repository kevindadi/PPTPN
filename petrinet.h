//
// Created by 张凯文 on 2023/6/26.
//

#ifndef PPTPN__PETRINET_H_
#define PPTPN__PETRINET_H_
#include <vector>
#include <string>
#include "config.h"

struct Place {
  std::string name, label;
  std::string shape = "circle";
  int token = 0;
};

struct Transition {
  std::string name, label;
  std::string shape = "box";
  bool enable = false;
  bool handle = false;
  int runtime = 0;
  int priority = INT_MAX;
  std::pair<int, int> const_time = {0, 0};
};

enum PetriNetElement {
  Place,
  Transition,
};

struct PetriNetEdge {
  std::string label;
};

class PetriNet {
  typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS,
                                PetriNetElement, PetriNetEdge, graph_p> PN;
 public:
  virtual void construct_petri_net(const Config& config);
};

#endif //PPTPN__PETRINET_H_
