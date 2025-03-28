#include "unsafe_scg.h"

bool UnsafeStateClassGraph::is_transition_enabled(const PTPN& ptpn, vertex_ptpn v) {
    // 检查是否是变迁
    if (ptpn[v].shape != "box") {
        return false;
    }

    bool has_non_place = false;
    bool all_places_enabled = true;

    // 遍历变迁的所有入边
    for (auto [ei, ei_end] = boost::in_edges(v, ptpn); ei != ei_end; ++ei) {
        vertex_ptpn source = boost::source(*ei, ptpn);
        
        if (ptpn[source].shape != "circle") {
            has_non_place = true;
            BOOST_LOG_TRIVIAL(error) << "Transition " << ptpn[v].name 
                                    << " has non-place predecessor: " 
                                    << ptpn[source].name;
            continue;
        }

        // 检查库所token数量
        if (ptpn[source].token < ptpn[*ei].weight) {
            all_places_enabled = false;
            break;
        }
    }

    if (has_non_place) {
        return false;
    }

    return all_places_enabled;
}

UnsafeStateClass UnsafeStateClassGraph::get_initial_state_class(const PTPN& source_ptpn) {
    UnsafeMark mark;
    std::set<UnsafeSchedT> all_t;
    for (auto [vi, vi_end] = vertices(source_ptpn); vi != vi_end; ++vi) {
      if (source_ptpn[*vi].shape == "circle" && source_ptpn[*vi].token > 0) {
        mark.indexes.insert(std::make_pair(*vi, source_ptpn[*vi].token));
        mark.labels.insert(source_ptpn[*vi].name);
      }
      
      if (source_ptpn[*vi].shape == "box" && is_transition_enabled(source_ptpn, *vi)) {
        all_t.insert(UnsafeSchedT(*vi, {0, 0}));
      }
    } 
    return UnsafeStateClass(mark, all_t);
}

ScgVertexD UnsafeStateClassGraph::add_scg_vertex(UnsafeStateClass sc) {
  ScgVertexD svd;
  if (scg_vertex_map.find(sc) != scg_vertex_map.end()) {
    svd = scg_vertex_map.find(sc)->second;
  } else {
    svd = add_vertex(SCGVertex{sc.to_unsafe_scg_vertex(), sc.to_unsafe_scg_vertex()},
                            scg);
    scg_vertex_map.insert(std::make_pair(sc, svd));
  }
  return svd;
}

void UnsafeStateClassGraph::set_state_class(const UnsafeStateClass &state_class) {
  // 首先重置原有状态
  boost::graph_traits<PTPN>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = vertices(*init_ptpn); vi != vi_end; ++vi) {
    (*init_ptpn)[*vi].token = 0;
    (*init_ptpn)[*vi].enabled = false;
    (*init_ptpn)[*vi].pnt.runtime = 0;
  }
  // 2. 设置标识
  for (auto m : state_class.mark.indexes) {
    (*init_ptpn)[m.first].token = m.second;
  }
  // 3. 设置各个变迁的已等待时间
  //  for (auto t : state_class.t_sched) {
  //    ptpn[t].pnt.runtime = state_class.t_time.find(t)->second;
  //  }
  // 4. 可挂起变迁的已等待时间
  //  for (auto t : state_class.handle_t_sched) {
  //    ptpn[t].pnt.runtime = state_class.t_time.find(t)->second;
  //  }
  // 5.设置所有变迁的已等待时间
  for (auto t : state_class.all_t) {
    (*init_ptpn)[t.t].pnt.runtime = t.time.first;
  }
}