#ifndef PPTPN_INCLUDE_UNSAFE_SCG_H
#define PPTPN_INCLUDE_UNSAFE_SCG_H

#include "priority_time_petri_net.h"
#include "state_class_graph.h"

struct UnsafeMark {
    std::set<std::pair<std::size_t, std::size_t>> indexes;
    std::set<std::string> labels;

    bool operator==(const UnsafeMark &other) const {
        if (indexes.size() != other.indexes.size()) {
        return false;
        }
        if (indexes == other.indexes) {
        return true;
        }
        return false;
    }

    bool operator<(const UnsafeMark &other) const {
        return indexes < other.indexes;
    }
};

struct UnsafeSchedT {
    std::size_t t = 0;
    std::pair<int, int> time = {0, 0};

    bool operator==(const UnsafeSchedT &other) const {
        if (t == other.t && time == other.time) {
            return true;
        }
        return false;
    }

    bool operator<(const UnsafeSchedT &other) const {
        if (t == other.t) {
            return time < other.time;
        }
        return t < other.t;
    }

    UnsafeSchedT() = default;
    UnsafeSchedT(std::size_t t, std::pair<int, int> time) : t(t), time(time) {}
};

struct UnsafeStateClass {
    UnsafeMark mark;
    std::set<UnsafeSchedT> all_t;

    bool operator==(const UnsafeStateClass &other) const {
        return mark == other.mark && all_t == other.all_t;
    }

    bool operator<(const UnsafeStateClass &other) const {
        if (mark == other.mark) {
            return all_t < other.all_t;
        }
        return mark < other.mark;
    }

    std::string to_unsafe_scg_vertex() {
        std::string labels;
        std::string times;
        for (const auto &l : mark.labels) {
            labels.append(l);
        }
        for (const auto &t : all_t) {
            times.append(std::to_string(t.t))
                .append(":")
                .append(std::to_string(t.time.first))
                .append("-")
                .append(std::to_string(t.time.second))
                .append(";");
        }
        return labels + times;
    }

    UnsafeStateClass() = default;
    UnsafeStateClass(UnsafeMark mark, std::set<UnsafeSchedT> all_t) : mark(mark), all_t(all_t) {}
};


namespace std {
    // 为Marking定义哈希函数
    template <> struct hash<UnsafeMark> {
    std::size_t operator()(const UnsafeMark &m) const {
        std::size_t hash_val = 0;
        for (const auto &index : m.indexes) {
        hash_val ^= std::hash<std::size_t>()(index.first);
        hash_val ^= std::hash<std::size_t>()(index.second);
        }
        return hash_val;
    }
    };

    template <> struct hash<UnsafeSchedT> {
    std::size_t operator()(const UnsafeSchedT &t) const {
        return std::hash<std::size_t>()(t.t) ^ std::hash<int>()(t.time.first) ^ std::hash<int>()(t.time.second);
    }
    };
} // namespace std

struct UnsafeStateClassHasher {
    std::size_t operator()(const UnsafeStateClass &k) const {
        std::size_t mark_hash = std::hash<UnsafeMark>()(k.mark);
        std::size_t all_t_hash = 0;
        for (const auto &t : k.all_t) {
            all_t_hash ^= std::hash<UnsafeSchedT>()(t);
        }

        return mark_hash ^ all_t_hash;
    }
    };
    
struct UnsafeStateClassEqual {
    bool operator()(const UnsafeStateClass &a, const UnsafeStateClass &b) const {
        return a == b;
    }
};

class UnsafeStateClassGraph {
private:
  std::unique_ptr<PTPN> init_ptpn;
  // 初始标识
  UnsafeMark init_mark; 
  // 初始状态类
  UnsafeStateClass init_state_class;
  std::set<UnsafeStateClass> sc_sets;
  std::unordered_map<UnsafeStateClass, ScgVertexD, UnsafeStateClassHasher, UnsafeStateClassEqual>  scg_vertex_map;
private:
  ScgVertexD add_scg_vertex(UnsafeStateClass sc);
  bool is_transition_enabled(const PTPN& ptpn, vertex_ptpn v);

  void set_state_class(const UnsafeStateClass &state_class);
  // 获得每个状态类下可调度的变迁集
  std::vector<SchedT> get_sched_transitions(const StateClass &state_class);
  // 发生变迁产生新的状态类
  StateClass fire_transition(const StateClass &sc, SchedT transition);


  // 获取初始状态下的等待时间集合
  UnsafeStateClass get_initial_state_class(const PTPN& source_ptpn);
  // 获取使能的变迁
  std::vector<vertex_ptpn> get_enabled_transitions();
  // 计算变迁的发生时间域
  std::pair<int, int> calculate_fire_time_domain(const std::vector<vertex_ptpn>& enabled_t_s);
  // 获取符合发生时间域的变迁
  std::vector<SchedT> get_time_satisfied_transitions(const std::vector<vertex_ptpn>& enabled_t_s, const std::pair<int, int>& fire_time);  
  // 应用优先级规则
  void apply_priority_rules(std::vector<SchedT>& sched_T);
  void apply_priority_rules(std::vector<std::size_t>& enabled_t);
  // 获取使能的变迁的前置变迁
  std::pair<std::vector<std::size_t>, std::vector<std::size_t>> get_enabled_transitions_with_history();
  // 更新变迁的等待时间
  void update_transition_times(const std::vector<std::size_t>& enabled_t, 
                           const SchedT& transition);  
  // 执行变迁
  void execute_transition(const SchedT& transition);
  // 获取新标识和新使能变迁
  std::pair<Marking, std::vector<std::size_t>> get_new_marking_and_enabled();
  // 计算新的等待时间集合
  std::set<T_wait> calculate_new_wait_times(const std::vector<std::size_t>& old_enabled_t, const std::vector<std::size_t>& new_enabled_t);

public:
  // 周期变迁 Names和Id
  std::vector<std::string> period_transitions;
  std::vector<std::size_t> period_transitions_id;
  // 构造函数
  UnsafeStateClassGraph(const PTPN& source_ptpn, std::vector<std::size_t> period_transitions_id) 
  : init_ptpn(deep_copy_graph(source_ptpn)), period_transitions_id(period_transitions_id) {
      init_state_class = get_initial_state_class(source_ptpn);
  }
  // 状态类图,insert唯一的stateclass
  SCG scg; 

  // 生成状态类图主函数
  void generate_state_class();
  // 死锁StateClass
  std::set<StateClass> deadlock_sc_sets;
};

#endif