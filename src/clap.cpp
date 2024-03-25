
#include "clap.h"
#include <regex>
#include <boost/exception/all.hpp>

struct LabelParseException : virtual boost::exception, virtual std::exception {
  std::string label;
  explicit LabelParseException(const std::string& msg) : label(msg) {}

  const char* what() const noexcept override {
    return label.c_str();
  }
};

struct TimeValueException : virtual LabelParseException {
  std::string time_values;
  explicit TimeValueException(const string &Msg, const std::string &msg) : LabelParseException(Msg), time_values(msg) {}

  const char* what() const noexcept override {
    return time_values.c_str();
  }
};

TDG::TDG(string tdg_file)
{
    this->tdg_file = std::move(tdg_file);
    logging::core::get()->set_filter(logging::trivial::severity >=
                                     logging::trivial::info);
}

// 解析 DAG 文件，获取任务的名字，核心，优先级，锁和时间
// 根据核心数量，取分配的最大核心数，锁根据名称分类
void TDG::parse_tdg() {
  boost::ref_property_map<TDG_RAP *, std::string> dag_name(
      get_property(tdg, boost::graph_name));
  tdg_dp.property("name", dag_name);
  tdg_dp.property("node_id", get(&DAGVertex::name, tdg));
  tdg_dp.property("label", get(&DAGVertex::label, tdg));
  tdg_dp.property("shape", get(&DAGVertex::shape, tdg));
  tdg_dp.property("xlabel", get(&DAGEdge::label, tdg));
  tdg_dp.property("style", get(&DAGEdge::style, tdg));

  std::ifstream tdg_stream(tdg_file);
  typename boost::property_map<TDG_RAP, boost::vertex_index_t>::type index =
      get(boost::vertex_index, tdg);
  if (read_graphviz(tdg_stream, tdg, tdg_dp)) {
    BOOST_LOG_TRIVIAL(info)
      << "Graph Name: " << get_property(tdg, boost::graph_name);
    // 遍历节点，确定节点类型
    BOOST_FOREACH (TDG_RAP::vertex_descriptor v, vertices(tdg)) {
            // TODO: id = label.name
            std::string id = get("node_id", tdg_dp, v);
            std::string label = get("label", tdg_dp, v);
            BOOST_LOG_TRIVIAL(debug) << label;
            NodeType node_type;
            try {
              node_type = parse_vertex_label(label);
            } catch (const LabelParseException &ex) {
              BOOST_LOG_TRIVIAL(error) << id << "'s label is wrong!";
            } catch (const boost::exception& ex) {
              std::cerr << "Boost Exception caught: " << boost::diagnostic_information(ex) << std::endl;
            } catch (const std::exception& ex) {
              std::cerr << "Standard Exception caught: " << ex.what() << std::endl;
            }
            if (holds_alternative<PeriodicTask>(node_type)) {
              PeriodicTask p_task = get<PeriodicTask>(node_type);
              string t_name = p_task.name;
              all_task.emplace_back(p_task);
              tasks_priority.insert(make_pair(t_name, p_task.priority));
              nodes_type.insert(make_pair(t_name, p_task));
              vertexes_type.insert(make_pair(t_name, VertexType::TASK));
              BOOST_LOG_TRIVIAL(info) << t_name << ": " << "type: TASK";
            } else if (holds_alternative<APeriodicTask>(node_type)) {
              APeriodicTask ap_task = get<APeriodicTask>(node_type);
              string t_name = ap_task.name;

              all_task.emplace_back(ap_task);
              tasks_priority.insert(make_pair(t_name, ap_task.priority));
              nodes_type.insert(make_pair(t_name, ap_task));
              vertexes_type.insert(make_pair(t_name, VertexType::INTERRUPT));
              BOOST_LOG_TRIVIAL(info) << t_name << ": " << "type: INTERRUPT";
            } else if (holds_alternative<SyncTask>(node_type)) {
              SyncTask s_task = get<SyncTask>(node_type);
              string t_name = s_task.name;
              nodes_type.insert(make_pair(t_name, s_task));
              vertexes_type.insert(make_pair(t_name, VertexType::SYNC));
              BOOST_LOG_TRIVIAL(info) << t_name << ": " << "type: SYNC";
            } else if (holds_alternative<DistTask>(node_type)) {
              DistTask d_task = get<DistTask>(node_type);
              string t_name = d_task.name;
              nodes_type.insert(make_pair(t_name, d_task));
              vertexes_type.insert(make_pair(t_name, VertexType::DIST));
              BOOST_LOG_TRIVIAL(info) << t_name << ": " << "type: DIST";
            } else {
              EmptyTask e_task = get<EmptyTask>(node_type);
              string t_name = e_task.name;
              nodes_type.insert(make_pair(t_name, e_task));
              vertexes_type.insert(make_pair(t_name, VertexType::EMPTY));
              BOOST_LOG_TRIVIAL(info) << t_name << ": " << "type: EMPTY";
            }
//            if (label.find("Wait") == std::string::npos) {
//              dag_task_name.push_back(label);
//            }
//            task_index.insert(std::make_pair(index[v], id));
          }
    // 遍历边，找到自环或回环，确定周期任务
    BOOST_FOREACH(TDG_RAP::edge_descriptor e, edges(tdg)) {
      auto source_name = tdg[source(e, tdg)].name;
      auto target_name = tdg[target(e, tdg)].name;
      // 前后节点相同的边为
      if (source_name == target_name) {
        // 保存为自环
        int period_time = stoi(tdg[e].label);
        period_task.emplace_back(source_name, target_name, period_time);
      }
      // 边类型为虚线
      if (tdg[e].style.find("dashed") != string::npos) {
        int period_time = stoi(tdg[e].label);
        period_task.emplace_back(source_name, target_name, period_time);
      }
      BOOST_LOG_TRIVIAL(debug) << "Edge: " << tdg[e].label << "weight: " << stoi(tdg[e].label);
    }
  }
}

// 按照 label 类型返回
NodeType TDG::parse_vertex_label(const string label) {
  NodeType node_type;
  regex rgx("\\{(.*?)\\}");
  smatch matches;
  if (regex_search(label, matches, rgx))
  {
    string content = matches[1].str();

    vector<std::string> parts;
    istringstream ss(content);
    string token;
    while (getline(ss, token, ';'))
    {
      parts.push_back(token);
    }
    string name = parts[0];
    // 根据长度判断 vertex 属于那种类型和是否包含锁变量
    //  size <=2，不属于任务类型, 根据名字开头字母判断属于同步、分发或空节点
    if (parts.size() <= 2 && parts[0].substr(0, 4) == "Wait"){
      return SyncTask {name};
    } else if (parts.size() <= 2 && parts[0].substr(0, 4) == "Dist") {
      return DistTask {name};
    } else if (parts.size() <= 2 && parts[0].substr(0, 4) == "Empty") {
      return EmptyTask {name};
    } else {
      int task_priority = stoi(parts[1]);
      int task_core = stoi(parts[2]);
      vector<pair<int, int>> task_times;
      vector<int> time_values = parse_time_vec(parts[3]);
      for (size_t i = 0; i < time_values.size(); i += 2) {
        task_times.emplace_back(time_values[i], time_values[i+1]);
      }
      vector<string> task_locks;
      bool is_lock = false;
      if (parts.size() >= 5) {
        is_lock = true;
        string lock_token;
        string locks_name = parts[4];
        istringstream lock_stream(locks_name);
        while (getline(lock_stream, lock_token, ',')) {
          task_locks.push_back(lock_token);
          lock_set.insert(lock_token);
          if (task_locks_map.find(name) == task_locks_map.end()) {
            vector<string> locks;
            locks.push_back(lock_token);
            task_locks_map.insert(make_pair(name, locks));
          }else {
            task_locks_map[name].push_back(lock_token);
          }
        }
      }

      // 如果命名中有中断任务
      if (name.substr(0, 9) == "Interrupt") {
        return APeriodicTask {
          name, task_core, task_priority, task_times, is_lock, task_locks, false};
      } else {
        return PeriodicTask {
            name, task_core, task_priority, task_times, is_lock, task_locks, true, 100};
      }
    }
  }else {
    // 未发现匹配表达式

    BOOST_THROW_EXCEPTION(LabelParseException("WRONG LABEL!"));
  }
}

vector<int> TDG::parse_time_vec(string times_string) {
  std::vector<int> values;
  std::regex rgx(R"(\[(\d+),(\d+)\])");
  std::smatch matches;

  std::string::const_iterator start = times_string.begin();
  std::string::const_iterator end = times_string.end();
  while (std::regex_search(start, end, matches, rgx))
  {
    values.push_back(std::stoi(matches[1].str()));
    values.push_back(std::stoi(matches[2].str()));
    start = matches[0].second;
  }
  return values;
}

// 根据优先级划分任务
std::unordered_map<int, vector<string>> TDG::classify_priority() {
  std::unordered_map<int, vector<string>> core_task;
  // 找个每个核心上的任务
  for (const auto& task : all_task) {
    if (holds_alternative<APeriodicTask>(task)) {
      auto result = get<APeriodicTask>(task);
      core_task[result.core].push_back(result.name);
    }else if (holds_alternative<PeriodicTask>(task)) {
      auto result = get<PeriodicTask>(task);
      core_task[result.core].push_back(result.name);
    } else {
      continue;
    }
  }
  // 根据任务优先级排序
  for (auto &tasks: core_task) {
    std::sort(tasks.second.begin(), tasks.second.end(),
              [&](const string& t1, const string& t2){
      return tasks_priority[t1] < tasks_priority[t2];
    });
  }

  for (auto it = core_task.begin(); it != core_task.end(); ++it) {
    BOOST_LOG_TRIVIAL(info) << "Core: " << it->first;
    for (auto task : it->second) {
      std::cout << task << " < ";
    }
    std::cout << std::endl;
  }
  return core_task;
}