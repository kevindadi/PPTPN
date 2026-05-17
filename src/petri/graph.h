#ifndef GRAPH_H
#define GRAPH_H

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <climits>
#include <string>
#include <variant>

#include "petri/petri.h"

namespace graph {

enum class PlaceKind {
  NORMAL,
  CPU_RESOURCE,
  LOCK_RESOURCE,
};

struct Place {
  int token = 0;
  int capacity = 1;
  PlaceKind kind = PlaceKind::NORMAL;
};

struct Transition {
  int priority = INT_MAX;
  int core = 0;
  std::pair<int, int> const_time = {0, 0};
  bool suspendable = false;
};

struct Edge {
  std::string label;
  int weight = 1;
};

struct Vertex {
  std::string name;
  std::string label;
  std::string shape;
  std::variant<Place, Transition> node;

  [[nodiscard]] bool is_place() const noexcept {
    return std::holds_alternative<Place>(node);
  }

  [[nodiscard]] bool is_transition() const noexcept {
    return std::holds_alternative<Transition>(node);
  }

  Place& as_place() { return std::get<Place>(node); }
  Transition& as_transition() { return std::get<Transition>(node); }
  [[nodiscard]] const Place& as_place() const { return std::get<Place>(node); }
  [[nodiscard]] const Transition& as_transition() const {
    return std::get<Transition>(node);
  }
};

typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS,
                              Vertex, Edge, boost::no_property>
    Graph;
typedef boost::graph_traits<Graph>::vertex_descriptor VertexDesc;

class GraphPTPN {
 public:
  explicit GraphPTPN(const petri::PTPN& ptpn);

  bool save_to_dot(const std::string& file_path) const;

  const Graph& get_graph() const { return graph; }

 private:
  void convert_matrix_to_graph(const petri::PTPN& ptpn);

  Graph graph;
};

}  // namespace graph

#endif
