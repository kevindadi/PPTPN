#include "graph_ptpn.h"

#include <boost/filesystem.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/property_map/property_map.hpp>
#include <climits>
#include <fstream>
#include <limits>
#include <map>
#include <spdlog/spdlog.h>

namespace graph_ptpn {

using namespace boost;

static VertexDesc add_place(Graph& graph, const std::string& name,
                            int token = 0, int capacity = 1) {
  Vertex v;
  v.name = name;
  v.label = name;
  v.shape = "circle";
  Place p;
  p.token = token;
  p.capacity = capacity;
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

GraphPTPN::GraphPTPN(const matrix_ptpn::MatrixPTPN& matrix_ptpn) {
  convert_matrix_to_graph(matrix_ptpn);
}

void GraphPTPN::convert_matrix_to_graph(
    const matrix_ptpn::MatrixPTPN& matrix_ptpn) {
  try {
    spdlog::info("[GRAPH_PTPN] 开始将矩阵形式转换为Boost Graph形式...");

    graph.clear();
    std::map<size_t, VertexDesc> place_to_vertex;
    std::map<size_t, VertexDesc> transition_to_vertex;

    const auto& marking = matrix_ptpn.get_marking();

    // 添加库所
    for (size_t p = 0; p < matrix_ptpn.num_places(); ++p) {
      const auto& place = matrix_ptpn.get_place(p);
      VertexDesc v = add_place(graph, place.name, marking[p], place.capacity);
      place_to_vertex[p] = v;
    }

    // 添加变迁
    for (size_t t = 0; t < matrix_ptpn.num_transitions(); ++t) {
      const auto& trans = matrix_ptpn.get_transition(t);
      std::pair<int, int> const_time(
          trans.time_interval.earliest,
          trans.time_interval.latest == matrix_ptpn::INF
              ? std::numeric_limits<int>::max()
              : trans.time_interval.latest);
      VertexDesc v = add_transition(graph, trans.name, trans.priority,
                                    trans.core, const_time, trans.suspendable);
      transition_to_vertex[t] = v;
    }

    // 添加Pre弧（库所到变迁）
    const auto& Pre = matrix_ptpn.get_pre_matrix();
    for (size_t p = 0; p < Pre.size(); ++p) {
      for (size_t t = 0; t < Pre[p].size(); ++t) {
        if (Pre[p][t] > 0) {
          Edge e;
          e.weight = Pre[p][t];
          if (Pre[p][t] > 1) {
            e.label = std::to_string(Pre[p][t]);
          }
          boost::add_edge(place_to_vertex[p], transition_to_vertex[t], e,
                          graph);
        }
      }
    }

    // 添加Post弧（变迁到库所）
    const auto& Post = matrix_ptpn.get_post_matrix();
    for (size_t t = 0; t < Post.size(); ++t) {
      for (size_t p = 0; p < Post[t].size(); ++p) {
        if (Post[t][p] > 0) {
          Edge e;
          e.weight = Post[t][p];
          if (Post[t][p] > 1) {
            e.label = std::to_string(Post[t][p]);
          }
          boost::add_edge(transition_to_vertex[t], place_to_vertex[p], e,
                          graph);
        }
      }
    }

    spdlog::info("[GRAPH_PTPN] 转换完成: {} 个节点, {} 条边", num_vertices(graph), num_edges(graph));
  } catch (const std::exception& e) {
    spdlog::error("[GRAPH_PTPN] 转换失败: {}", e.what());
    throw;
  }
}

bool GraphPTPN::save_to_dot(const std::string& file_path) const {
  try {
    boost::filesystem::path dot_filename(file_path);

    // 确保父目录存在
    if (!dot_filename.parent_path().empty() &&
        !boost::filesystem::exists(dot_filename.parent_path())) {
      boost::filesystem::create_directories(dot_filename.parent_path());
    }

    std::ofstream ofs(dot_filename.string());
    if (!ofs) {
      spdlog::error("[GRAPH_PTPN] 无法打开DOT文件: {}", dot_filename.string());
      return false;
    }

    Graph& g = const_cast<Graph&>(graph);
    dynamic_properties dp;
    dp.property("node_id", boost::get(&Vertex::name, g));
    dp.property("label", boost::get(&Vertex::label, g));
    dp.property("shape", boost::get(&Vertex::shape, g));
    dp.property("label", boost::get(&Edge::label, g));

    // 写入DOT格式
    write_graphviz_dp(ofs, g, dp);
    ofs.close();

    std::string saved_path = boost::filesystem::absolute(dot_filename).string();
    spdlog::info("[GRAPH_PTPN] DOT文件已保存到: {}", saved_path);
    return true;
  } catch (const std::exception& e) {
    spdlog::error("[GRAPH_PTPN] 保存DOT文件时发生错误: {}", e.what());
    return false;
  }
}

}  // namespace graph_ptpn
