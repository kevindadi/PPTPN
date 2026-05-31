#include <iostream>
#include "parser/ptpn_parser.h"

int main() {
  std::string ptpn_content = R"(
// === Places ===
places
    P0: Start:1
    P1: Running:1

// === Transitions ===
transitions
    T0 [0, 0] @priority=0, core=-1
    T1 [3, 8] @priority=97, core=0

// === Arcs ===
P0 -> T0
T0 -> P1
P1 -> T1

// === Initial Marking ===
@init P0:1
)";

  parser::Parser parser(ptpn_content);
  parser::PTPNAST ast = parser.parse();

  if (parser.has_error()) {
    std::cerr << "Parse error: " << parser.error_message() << "\n";
    return 1;
  }

  std::cout << "Parse successful!\n";
  std::cout << "Places: " << ast.places.size() << "\n";
  std::cout << "Transitions: " << ast.transitions.size() << "\n";
  std::cout << "Arcs: " << ast.arcs.size() << "\n";
  std::cout << "Initial marking: " << ast.initial_marking.size() << "\n";

  return 0;
}
