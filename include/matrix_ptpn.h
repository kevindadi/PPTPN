#ifndef MATRIX_PTPN_H
#define MATRIX_PTPN_H

#include <climits>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

class TDG;
struct TaskConfig;
struct PeriodicTask;
struct APeriodicTask;
struct ForkTask;
struct JoinTask;
struct EmptyTask;
using NodeType =
    std::variant<PeriodicTask, APeriodicTask, ForkTask, JoinTask, EmptyTask>;

namespace matrix_ptpn {

// 无穷大时间常量
constexpr int INF = std::numeric_limits<int>::max();

// 时间区间结构体
struct TimeInterval {
  int earliest;  // α(t),最早可触发时间
  int latest;    // β(t),最晚可触发时间(可为 INF)

  TimeInterval(int e = 0, int l = INF) : earliest(e), latest(l) {
    if (earliest < 0) {
      throw std::invalid_argument("earliest time must be non-negative");
    }
    if (latest != INF && latest < earliest) {
      throw std::invalid_argument("latest time must be >= earliest time");
    }
  }

  [[nodiscard]] bool is_valid() const {
    return earliest >= 0 && (latest == INF || latest >= earliest);
  }

  [[nodiscard]] bool contains(int time) const {
    return time >= earliest && (latest == INF || time <= latest);
  }

  [[nodiscard]] std::string to_string() const {
    std::ostringstream oss;
    oss << "[" << earliest << ", ";
    if (latest == INF) {
      oss << "∞";
    } else {
      oss << latest;
    }
    oss << "]";
    return oss.str();
  }
};

// 库所
struct Place {
  std::string id;    // 位置 ID
  std::string name;  // 位置名称
  int capacity;      // 容量(可选,默认为 1)

  Place(const std::string& id = "", const std::string& name = "", int cap = 1)
      : id(id), name(name), capacity(cap) {}
};

// 变迁
struct Transition {
  std::string id;              // 变迁 ID
  std::string name;            // 变迁名称
  TimeInterval time_interval;  // I(t) = [α(t), β(t)]
  int priority;                // π(t),优先级(数值越小优先级越高)
  int core;                    // 分配的 CPU 核心 ID
  bool suspendable;            // 是否可挂起

  Transition(const std::string& id = "", const std::string& name = "",
             const TimeInterval& interval = TimeInterval(),
             int priority = INT_MAX, int core = 0, bool suspendable = false)
      : id(id),
        name(name),
        time_interval(interval),
        priority(priority),
        core(core),
        suspendable(suspendable) {}
};

// 标识向量类型:M[p] 表示位置 p 的 token 数量
using Marking = std::vector<int>;

// 优先级时间 Petri 网
class MatrixPTPN {
 public:
  MatrixPTPN() = default;

  size_t add_place(const std::string& name, int capacity = 1) {
    places.emplace_back(std::to_string(places.size()), name, capacity);

    Pre.emplace_back(std::vector<int>(transitions.size(), 0));
    M0.push_back(0);

    for (auto& row : Post) {
      row.push_back(0);
    }

    return places.size() - 1;
  }

  size_t add_transition(const std::string& name,
                        const TimeInterval& interval = TimeInterval(),
                        int priority = INT_MAX, int core = 0,
                        bool suspendable = false) {
    transitions.emplace_back(std::to_string(transitions.size()), name, interval,
                             priority, core, suspendable);

    for (auto& row : Pre) {
      row.push_back(0);
    }

    Post.emplace_back(std::vector<int>(places.size(), 0));

    return transitions.size() - 1;
  }

  void set_pre_arc(size_t place_idx, size_t trans_idx, int weight = 1) {
    if (place_idx >= Pre.size() || trans_idx >= transitions.size()) {
      throw std::out_of_range("Invalid place or transition index");
    }
    Pre[place_idx][trans_idx] = weight;
  }

  void set_post_arc(size_t trans_idx, size_t place_idx, int weight = 1) {
    if (trans_idx >= Post.size() || place_idx >= places.size()) {
      throw std::out_of_range("Invalid transition or place index");
    }
    Post[trans_idx][place_idx] = weight;
  }

  void set_initial_marking(const Marking& marking) {
    if (marking.size() != places.size()) {
      throw std::invalid_argument("Marking size must match number of places");
    }
    M0 = marking;
  }

  void set_initial_marking(size_t place_idx, int tokens) {
    if (place_idx >= places.size()) {
      throw std::out_of_range("Invalid place index");
    }
    if (tokens < 0) {
      throw std::invalid_argument("Token count cannot be negative");
    }
    M0[place_idx] = tokens;
  }

  [[nodiscard]] size_t num_places() const { return places.size(); }
  [[nodiscard]] size_t num_transitions() const { return transitions.size(); }

  [[nodiscard]] const Place& get_place(size_t idx) const {
    if (idx >= places.size()) {
      throw std::out_of_range("Invalid place index");
    }
    return places[idx];
  }

  [[nodiscard]] const Transition& get_transition(size_t idx) const {
    if (idx >= transitions.size()) {
      throw std::out_of_range("Invalid transition index");
    }
    return transitions[idx];
  }

  [[nodiscard]] const Marking& get_marking() const { return M0; }
  [[nodiscard]] const std::vector<std::vector<int>>& get_pre_matrix() const {
    return Pre;
  }
  [[nodiscard]] const std::vector<std::vector<int>>& get_post_matrix() const {
    return Post;
  }

  static bool is_enabled(const Marking& M, const MatrixPTPN& net,
                         size_t trans_idx) {
    if (trans_idx >= net.transitions.size()) {
      throw std::out_of_range("Invalid transition index");
    }
    if (M.size() != net.places.size()) {
      throw std::invalid_argument("Marking size must match number of places");
    }

    for (size_t p = 0; p < net.places.size(); ++p) {
      if (net.Pre[p][trans_idx] > 0) {
        if (M[p] < net.Pre[p][trans_idx]) {
          return false;
        }
      }
    }
    return true;
  }

  [[nodiscard]] bool is_enabled(size_t trans_idx) const {
    return is_enabled(M0, *this, trans_idx);
  }

  static Marking fire(const Marking& M, const MatrixPTPN& net,
                      size_t trans_idx) {
    if (!is_enabled(M, net, trans_idx)) {
      throw std::runtime_error("Transition is not enabled");
    }

    Marking new_marking = M;

    // M' = M - Pre + Post
    for (size_t p = 0; p < net.places.size(); ++p) {
      new_marking[p] -= net.Pre[p][trans_idx];
    }

    for (size_t p = 0; p < net.places.size(); ++p) {
      new_marking[p] += net.Post[trans_idx][p];

      if (net.places[p].capacity != INF &&
          new_marking[p] > net.places[p].capacity) {
        new_marking[p] = net.places[p].capacity;
      }
    }

    return new_marking;
  }

  void fire_transition(size_t trans_idx) { M0 = fire(M0, *this, trans_idx); }

  [[nodiscard]] std::string to_string() const {
    std::ostringstream oss;
    oss << "=== Matrix PTPN ===\n";
    oss << "Places (" << places.size() << "):\n";
    for (size_t i = 0; i < places.size(); ++i) {
      oss << "  P" << i << ": " << places[i].name << " [capacity="
          << (places[i].capacity == INF ? "∞"
                                        : std::to_string(places[i].capacity))
          << ", tokens=" << M0[i] << "]\n";
    }

    oss << "\nTransitions (" << transitions.size() << "):\n";
    for (size_t i = 0; i < transitions.size(); ++i) {
      oss << "  T" << i << ": " << transitions[i].name
          << " [time=" << transitions[i].time_interval.to_string()
          << ", priority=" << transitions[i].priority
          << ", core=" << transitions[i].core
          << ", suspendable=" << (transitions[i].suspendable ? "yes" : "no")
          << "]\n";
    }

    oss << "\nPre Matrix (" << Pre.size() << "x"
        << (Pre.empty() ? 0 : Pre[0].size()) << "):\n";
    for (size_t p = 0; p < Pre.size(); ++p) {
      oss << "  P" << p << ": ";
      for (size_t t = 0; t < Pre[p].size(); ++t) {
        oss << Pre[p][t] << " ";
      }
      oss << "\n";
    }

    oss << "\nPost Matrix (" << Post.size() << "x"
        << (Post.empty() ? 0 : Post[0].size()) << "):\n";
    for (size_t t = 0; t < Post.size(); ++t) {
      oss << "  T" << t << ": ";
      for (size_t p = 0; p < Post[t].size(); ++p) {
        oss << Post[t][p] << " ";
      }
      oss << "\n";
    }

    return oss.str();
  }

  [[nodiscard]] std::vector<size_t> get_enabled_transitions() const {
    std::vector<size_t> enabled;
    for (size_t t = 0; t < transitions.size(); ++t) {
      if (is_enabled(t)) {
        enabled.push_back(t);
      }
    }
    return enabled;
  }

  [[nodiscard]] std::vector<size_t> filter_by_priority(
      const std::vector<size_t>& enabled_transitions) const {
    if (enabled_transitions.empty()) {
      return {};
    }

    int highest_priority = INT_MAX;
    for (size_t t : enabled_transitions) {
      if (transitions[t].priority < highest_priority) {
        highest_priority = transitions[t].priority;
      }
    }

    std::vector<size_t> filtered;
    for (size_t t : enabled_transitions) {
      if (transitions[t].priority == highest_priority) {
        filtered.push_back(t);
      }
    }

    return filtered;
  }

  // 按核心和优先级过滤使能变迁(先按核心分组,再在每个核心内按优先级过滤)
  [[nodiscard]] std::vector<size_t> filter_by_core_and_priority(
      const std::vector<size_t>& enabled_transitions) const {
    if (enabled_transitions.empty()) {
      return {};
    }

    std::map<int, std::vector<size_t>> transitions_by_core;
    for (size_t t : enabled_transitions) {
      transitions_by_core[transitions[t].core].push_back(t);
    }

    std::vector<size_t> filtered;

    for (auto& [core_id, core_transitions] : transitions_by_core) {
      int highest_priority = INT_MAX;
      for (size_t t : core_transitions) {
        if (transitions[t].priority < highest_priority) {
          highest_priority = transitions[t].priority;
        }
      }

      for (size_t t : core_transitions) {
        if (transitions[t].priority == highest_priority) {
          filtered.push_back(t);
        }
      }
    }

    return filtered;
  }

  [[nodiscard]] std::vector<size_t> get_transitions_by_core(int core_id) const {
    std::vector<size_t> result;
    for (size_t t = 0; t < transitions.size(); ++t) {
      if (transitions[t].core == core_id) {
        result.push_back(t);
      }
    }
    return result;
  }

  // 获取指定核心的所有使能变迁
  [[nodiscard]] std::vector<size_t> get_enabled_transitions_by_core(
      int core_id) const {
    std::vector<size_t> result;
    for (size_t t = 0; t < transitions.size(); ++t) {
      if (transitions[t].core == core_id && is_enabled(t)) {
        result.push_back(t);
      }
    }
    return result;
  }

  // 验证网络结构
  [[nodiscard]] bool verify_structure() const;

  void transform_tdg_to_matrix_ptpn(TDG& tdg);

 private:
  void transform_vertices_from_tdg(TDG& tdg);
  void transform_edges_from_tdg(TDG& tdg);
  std::pair<size_t, size_t> add_node_matrix(const NodeType& node_type);
  std::pair<size_t, size_t> add_p_node_matrix(PeriodicTask& p_task);
  std::pair<size_t, size_t> add_ap_node_matrix(APeriodicTask& ap_task);
  void add_monitor_matrix(const std::string& task_name, int task_period_time,
                          size_t start, size_t end);
  void add_preempt_task_matrix(
      const std::unordered_map<int, std::vector<std::string>>& core_task,
      const std::unordered_map<std::string, TaskConfig>& tc,
      const std::unordered_map<std::string, NodeType>& nodes_type);
  void add_resources_and_bindings_matrix(TDG& tdg);
  void add_cpu_resource_matrix(int cpus, int cores_per_cpu);
  void add_lock_resource_matrix(const std::set<std::string>& locks_name);
  void task_bind_cpu_resource_matrix(const std::vector<NodeType>& all_task);
  void task_bind_lock_resource_matrix(
      const std::vector<NodeType>& all_task,
      std::map<std::string, std::vector<std::string>>& task_locks);
  void bind_task_locks_matrix(
      const std::string& task_name, const std::vector<std::string>& lock_types,
      const std::vector<size_t>& task_pt_chain,
      std::map<std::string, std::vector<std::string>>& task_locks);
  bool is_self_loop_edge(const std::string& source, const std::string& target);
  bool is_dashed_edge(const std::string& edge);
  void handle_self_loop_edge_matrix(const std::string& label,
                                    const std::string& source_name);
  void handle_dashed_edge_matrix(const std::string& source_name,
                                 const std::string& target_name);
  void handle_normal_edge_matrix(const std::string& source_name,
                                 const std::string& target_name);

 private:
  std::vector<Place> places;            // P:位置集合
  std::vector<Transition> transitions;  // T:变迁集合
  std::vector<std::vector<int>> Pre;    // Pre:|P|×|T| 输入矩阵
  std::vector<std::vector<int>> Post;   // Post:|T|×|P| 输出矩阵
  Marking M0;                           // M0:初始标识向量

  // TDG 转换相关的私有成员
  std::map<std::string, std::pair<size_t, size_t>>
      node_start_end_map;  // 节点名称 -> (开始节点索引, 结束节点索引)
  std::unordered_map<std::string, std::vector<size_t>>
      node_pn_map;                                      // 节点名称 -> Petri网链
  std::vector<size_t> cpus_place;                       // CPU资源库所索引
  std::unordered_map<std::string, size_t> locks_place;  // 锁资源库所索引
  int node_index = 0;                                   // 节点索引计数器
};

}  // namespace matrix_ptpn

#endif  // MATRIX_PTPN_H
