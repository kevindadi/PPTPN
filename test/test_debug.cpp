#include <iostream>
#include "parser/ptpn_parser.h"

int main() {
  // Simple test - just parse attributes
  std::string content = R"(
transitions
T1 [3, 8] @priority=97, core=0, suspendable
)";

  parser::PTPNAST ast;
  std::string error;

  bool result = parser::PTPNParser::parse(content, ast, error);

  std::cout << "Result: " << (result ? "true" : "false") << std::endl;
  if (!result) std::cout << "Error: " << error << std::endl;

  std::cout << "Transitions: " << ast.transitions.size() << std::endl;
  for (const auto& t : ast.transitions) {
    std::cout << "  " << t.id << " [" << t.time_min << "," << t.time_max << "]"
              << " prio=" << t.priority << " core=" << t.core << " suspendable=" << t.suspendable << std::endl;
  }

  return 0;
}