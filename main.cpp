#include "priority_time_petri_net.h"
#include <iostream>
#include "clap.h"
#include <boost/program_options.hpp>
#include "state_class_graph.h"  
#include "calcuate.h"

namespace po = boost::program_options;

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
  
  TDG tdg_rap = {"../test/label.dot"};
  tdg_rap.parse_tdg();
  PriorityTimePetriNet ptpn;
  ptpn.init();
  ptpn.transform_tdg_to_ptpn(tdg_rap);
  if (!ptpn.verify_petri_net_structure()) {
    BOOST_LOG_TRIVIAL(error) << "Petri net structure is incorrect";
    return 1;
  } 
  StateClassGraph Scg(ptpn.ptpn, std::vector<std::size_t>{1, 2});
  Scg.generate_state_class();
  // check_deadlock(Scg.scg); 
  return 0;
}

