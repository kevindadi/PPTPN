#include "petri/graph.h"

#include <boost/filesystem.hpp>
#include <boost/graph/graphviz.hpp>
#include <fstream>
#include <limits>
#include <spdlog/spdlog.h>

namespace graph {

using namespace boost;

namespace {

constexpr int kSyntheticMetaValue = 411;

bool is_helper_transition(const Transition& transition) {
  return transition.core < 0;
}

bool is_immediate_transition(const Transition& transition) {
  return transition.const_time.first == 0 && transition.const_time.second == 0;
}

bool should_show_scheduling_meta(const Transition& transition) {
  if (transition.priority == 0 &&
      transition.core == -1) {
    return false;
  }
  if (is_helper_transition(transition)) {
    return false;
  }
  return true;
}

std::string escape_dot_string(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

std::string quote_dot_string(const std::string& value) {
  return "\"" + escape_dot_string(value) + "\"";
}

std::string format_int_or_infinity(int value) {
  return value == std::numeric_limits<int>::max() ? "∞" : std::to_string(value);
}

std::string format_place_label(const Vertex& vertex) {
  const auto& place = vertex.as_place();
  std::string label = vertex.name;
  if (place.kind != PlaceKind::NORMAL || place.token > 0 || place.capacity != 1) {
    label += "\nM=" + std::to_string(place.token) + ", C=" +
             format_int_or_infinity(place.capacity);
  }
  return label;
}

std::string format_transition_label(const Vertex& vertex) {
  const auto& transition = vertex.as_transition();
  std::string label = vertex.name;

  if (should_show_scheduling_meta(transition)) {
    label += "\nπ=" + std::to_string(transition.priority) +
             "  core=" + std::to_string(transition.core);
  }

  label += "\nI=[" + format_int_or_infinity(transition.const_time.first) + ", " +
           format_int_or_infinity(transition.const_time.second) + "]";
  return label;
}

std::string vertex_label(const Vertex& vertex) {
  return vertex.is_place() ? format_place_label(vertex) : format_transition_label(vertex);
}

std::string vertex_shape(const Vertex& vertex) {
  return vertex.shape;
}

std::string vertex_style(const Vertex&) {
  return "filled,rounded";
}

std::string vertex_fillcolor(const Vertex& vertex) {
  if (vertex.is_place()) {
    switch (vertex.as_place().kind) {
      case PlaceKind::CPU_RESOURCE:
        return "#dbeafe";
      case PlaceKind::LOCK_RESOURCE:
        return "#fef3c7";
      case PlaceKind::NORMAL:
      default:
        return "#ffffff";
    }
  }

  const auto& transition = vertex.as_transition();
  if (transition.suspendable) {
    return "#fce7f3";
  }
  if (is_immediate_transition(transition) && should_show_scheduling_meta(transition)) {
    return "#fde68a";
  }
  return "#e5e7eb";
}

std::string vertex_color(const Vertex& vertex) {
  if (vertex.is_place()) {
    switch (vertex.as_place().kind) {
      case PlaceKind::CPU_RESOURCE:
        return "#2563eb";
      case PlaceKind::LOCK_RESOURCE:
        return "#d97706";
      case PlaceKind::NORMAL:
      default:
        return "#374151";
    }
  }

  const auto& transition = vertex.as_transition();
  if (transition.suspendable) {
    return "#be185d";
  }
  if (is_immediate_transition(transition) && should_show_scheduling_meta(transition)) {
    return "#d97706";
  }
  return "#6b7280";
}

std::string vertex_fontcolor(const Vertex&) {
  return "#111827";
}

std::string vertex_penwidth(const Vertex& vertex) {
  return vertex.is_transition() && vertex.as_transition().suspendable ? "2.2" : "1.4";
}

std::string edge_color(const Edge&) {
  return "#9ca3af";
}

std::string edge_penwidth(const Edge& edge) {
  return edge.weight > 1 ? "1.6" : "1.0";
}

PlaceKind detect_place_kind(const std::string& name) {
  if (name.rfind("core", 0) == 0) {
    return PlaceKind::CPU_RESOURCE;
  }
  if (name.rfind("mutex", 0) == 0 || name.rfind("spin", 0) == 0) {
    return PlaceKind::LOCK_RESOURCE;
  }
  return PlaceKind::NORMAL;
}

}  // namespace

static VertexDesc add_place(Graph& graph, const std::string& name,
                            int token = 0, int capacity = 1) {
  Vertex v;
  v.name = name;
  v.label = name;
  v.shape = "circle";
  Place p;
  p.token = token;
  p.capacity = capacity;
  p.kind = detect_place_kind(name);
  v.node = p;
  return boost::add_vertex(v, graph);
}

static VertexDesc add_transition(Graph& graph, const std::string& name,
                                 int priority = INT_MAX, int core = 0,
                                 const std::pair<int, int>& const_time = {0, 0},
                                 bool suspendable = false) {
  Vertex v;
  v.name = name;
  v.label = name;
  v.shape = "box";
  Transition t;
  t.priority = priority;
  t.core = core;
  t.const_time = const_time;
  t.suspendable = suspendable;
  v.node = t;
  return boost::add_vertex(v, graph);
}

GraphPTPN::GraphPTPN(const petri::PTPN& ptpn) {
  convert_matrix_to_graph(ptpn);
}

void GraphPTPN::convert_matrix_to_graph(const petri::PTPN& ptpn) {
  try {
    spdlog::info("[GRAPH] Converting PTPN to Boost Graph...");

    graph.clear();
    std::map<size_t, VertexDesc> place_to_vertex;
    std::map<size_t, VertexDesc> transition_to_vertex;

    const auto& marking = ptpn.get_marking();

    for (size_t p = 0; p < ptpn.num_places(); ++p) {
      const auto& place = ptpn.get_place(p);
      VertexDesc v = add_place(graph, place.name, marking[p], place.capacity);
      place_to_vertex[p] = v;
    }

    for (size_t t = 0; t < ptpn.num_transitions(); ++t) {
      const auto& trans = ptpn.get_transition(t);
      std::pair<int, int> const_time(
          trans.time_interval.earliest,
          trans.time_interval.latest == petri::INF
              ? std::numeric_limits<int>::max()
              : trans.time_interval.latest);
      VertexDesc v = add_transition(graph, trans.name, trans.priority,
                                    trans.core, const_time, trans.suspendable);
      transition_to_vertex[t] = v;
    }

    const auto& Pre = ptpn.get_pre_matrix();
    for (size_t p = 0; p < Pre.size(); ++p) {
      for (size_t t = 0; t < Pre[p].size(); ++t) {
        if (Pre[p][t] > 0) {
          Edge e;
          e.weight = Pre[p][t];
          if (Pre[p][t] > 1) {
            e.label = std::to_string(Pre[p][t]);
          }
          boost::add_edge(place_to_vertex[p], transition_to_vertex[t], e, graph);
        }
      }
    }

    const auto& Post = ptpn.get_post_matrix();
    for (size_t t = 0; t < Post.size(); ++t) {
      for (size_t p = 0; p < Post[t].size(); ++p) {
        if (Post[t][p] > 0) {
          Edge e;
          e.weight = Post[t][p];
          if (Post[t][p] > 1) {
            e.label = std::to_string(Post[t][p]);
          }
          boost::add_edge(transition_to_vertex[t], place_to_vertex[p], e, graph);
        }
      }
    }

    spdlog::info("[GRAPH] Conversion complete: {} vertices, {} edges", num_vertices(graph), num_edges(graph));
  } catch (const std::exception& e) {
    spdlog::error("[GRAPH] Conversion failed: {}", e.what());
    throw;
  }
}

bool GraphPTPN::save_to_dot(const std::string& file_path) const {
  try {
    boost::filesystem::path dot_filename(file_path);

    if (!dot_filename.parent_path().empty() &&
        !boost::filesystem::exists(dot_filename.parent_path())) {
      boost::filesystem::create_directories(dot_filename.parent_path());
    }

    std::ofstream ofs(dot_filename.string());
    if (!ofs) {
      spdlog::error("[GRAPH] Cannot open DOT file: {}", dot_filename.string());
      return false;
    }

    Graph& g = const_cast<Graph&>(graph);

    ofs << "digraph G {\n";
    ofs << "graph [rankdir=LR, fontname=\"Helvetica\", nodesep=0.35, ranksep=0.55, bgcolor=\"white\"];\n";
    ofs << "node [fontname=\"Helvetica\", margin=0.08, style=\"filled,rounded\", fontcolor=\"#111827\"];\n";
    ofs << "edge [fontname=\"Helvetica\", color=\"#9ca3af\", arrowsize=0.7];\n";

    for (const auto vertex : boost::make_iterator_range(vertices(g))) {
      const auto& data = g[vertex];
      ofs << quote_dot_string(data.name) << " ["
          << "label=" << quote_dot_string(vertex_label(data))
          << ", shape=" << quote_dot_string(vertex_shape(data))
          << ", style=" << quote_dot_string(vertex_style(data))
          << ", fillcolor=" << quote_dot_string(vertex_fillcolor(data))
          << ", color=" << quote_dot_string(vertex_color(data))
          << ", fontcolor=" << quote_dot_string(vertex_fontcolor(data))
          << ", penwidth=" << quote_dot_string(vertex_penwidth(data))
          << "];\n";
    }

    for (const auto edge : boost::make_iterator_range(edges(g))) {
      const auto source_vertex = source(edge, g);
      const auto target_vertex = target(edge, g);
      const auto& data = g[edge];
      ofs << quote_dot_string(g[source_vertex].name) << " -> "
          << quote_dot_string(g[target_vertex].name) << " ["
          << "label=" << quote_dot_string(data.label)
          << ", color=" << quote_dot_string(edge_color(data))
          << ", penwidth=" << quote_dot_string(edge_penwidth(data))
          << "];\n";
    }

    ofs << "}\n";
    ofs.close();

    std::string saved_path = boost::filesystem::absolute(dot_filename).string();
    spdlog::info("[GRAPH] DOT file saved to: {}", saved_path);
    return true;
  } catch (const std::exception& e) {
    spdlog::error("[GRAPH] Error saving DOT file: {}", e.what());
    return false;
  }
}

}  // namespace graph
