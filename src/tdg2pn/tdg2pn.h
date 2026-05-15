#ifndef TDG2PN_H
#define TDG2PN_H

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "../types/types.h"
#include "../tdg/tdg.h"
#include "../petri/petri.h"

namespace converter {

class TDG2PN {
 public:
  static void transform(const tdg::TDG& tdg, petri::PTPN& ptpn);

 private:
  static void transform_vertices(petri::PTPN& ptpn, const tdg::TDG& tdg);
  static void transform_edges(petri::PTPN& ptpn, const tdg::TDG& tdg);
  static std::pair<size_t, size_t> add_node_matrix(petri::PTPN& ptpn, const NodeType& node_type);
  static std::pair<size_t, size_t> add_p_node_matrix(petri::PTPN& ptpn, PeriodicTask& p_task);
  static std::pair<size_t, size_t> add_ap_node_matrix(petri::PTPN& ptpn, APeriodicTask& ap_task);
  static void add_monitor_matrix(petri::PTPN& ptpn, const std::string& task_name,
                                  int task_period_time, size_t start, size_t end);
  static void add_preempt_task_matrix(
      petri::PTPN& ptpn,
      const std::unordered_map<int, std::vector<std::string>>& core_task,
      const std::unordered_map<std::string, TaskConfig>& tc,
      const std::unordered_map<std::string, NodeType>& nodes_type);
  static void add_resources_and_bindings_matrix(petri::PTPN& ptpn, const tdg::TDG& tdg);
  static void add_cpu_resource_matrix(petri::PTPN& ptpn, int cpus, int cores_per_cpu);
  static void add_lock_resource_matrix(petri::PTPN& ptpn, const std::set<std::string>& locks_name);
  static void task_bind_cpu_resource_matrix(petri::PTPN& ptpn, const std::vector<NodeType>& all_task);
  static void task_bind_lock_resource_matrix(
      petri::PTPN& ptpn,
      const std::vector<NodeType>& all_task,
      std::map<std::string, std::vector<std::string>>& task_locks);
  static void bind_task_locks_matrix(
      petri::PTPN& ptpn,
      const std::string& task_name, const std::vector<std::string>& lock_types,
      const std::vector<size_t>& task_pt_chain,
      std::map<std::string, std::vector<std::string>>& task_locks);
  static bool is_self_loop_edge(const std::string& source, const std::string& target);
  static bool is_dashed_edge(const std::string& edge);
  static void handle_self_loop_edge_matrix(petri::PTPN& ptpn,
                                            const std::string& label,
                                            const std::string& source_name);
  static void handle_dashed_edge_matrix(petri::PTPN& ptpn,
                                         const std::string& source_name,
                                         const std::string& target_name);
  static void handle_normal_edge_matrix(petri::PTPN& ptpn,
                                         const std::string& source_name,
                                         const std::string& target_name);
};

}  // namespace converter

#endif  // TDG2PN_H