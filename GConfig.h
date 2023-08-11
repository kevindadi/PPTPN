//
// Created by Kevin on 2023/8/8.
//

#ifndef PPTPN_GCONFIG_H
#define PPTPN_GCONFIG_H
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <graphviz/gvc.h>
#include <set>

using namespace boost;
namespace logging = boost::log;

struct SubGraphConfig {
  std::string name;
  int core;
  bool is_period;
  int period;
};

struct SubTaskConfig {
  std::string name;
  int core, priority;
  std::vector<std::pair<int, int>> time;
  std::vector<std::string> lock;
  bool is_sub;
};

class GConfig {
public:
  GConfig() = default;
  ~GConfig() {
    // Clean up
    dotFile = nullptr;
    agclose(graph);
    gvFreeContext(gvc);
  };
  char *dag_file = nullptr;
  explicit GConfig(char *dag_file);
  FILE *dotFile = nullptr;
  void init();

  Agraph_t *graph = nullptr;
  GVC_t *gvc = nullptr;

public: // 保存任务结构
  // subgraph的所有子任务集合
  std::map<std::string, std::vector<std::string>> sub_task_set;
  // 每个任务属于那个子任务
  std::unordered_map<std::string, std::string> task_where_sub;
  // subgraph的属性集合
  std::unordered_map<std::string, SubGraphConfig> sub_graph_config;
  // subgraph的开始和结束任务
  std::unordered_map<std::string, std::pair<std::string, std::string>>
      sub_graph_start_end;
  // sub task collection
  std::vector<std::string> sub_task_collection;
  // glb task collection
  std::vector<std::string> glb_task_collection;
  // 每个任务的属性
  std::unordered_map<std::string, SubTaskConfig> tasks_label;
  std::vector<SubTaskConfig> all_stc;
  // 任务的分类
  std::map<int, std::vector<SubTaskConfig>> classify_priority();
  std::unordered_map<std::string, SubTaskConfig> sub_tasks_label;
  std::unordered_map<std::string, SubTaskConfig> glb_tasks_label;
  SubTaskConfig prase_sub_task_label(const std::string &label);
  SubTaskConfig prase_glb_task_label(const std::string &label);

  std::set<std::string> lock_set;
  void prase_dag();
  void prase_subgraph(Agraph_t *subgraph);
};

#endif // PPTPN_GCONFIG_H
