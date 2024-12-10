#include "clap.h"

#include <boost/exception/all.hpp>
#include <boost/static_assert.hpp>
#include <regex>

struct LabelParseException : virtual boost::exception, virtual std::exception {
  std::string label;
  LabelParseException() : label("") {} // 添加默认构造函数
  explicit LabelParseException(const std::string &msg) : label(msg) {}

  const char *what() const noexcept override { return label.c_str(); }
};

struct TimeValueException : virtual LabelParseException {
  std::string time_values;
  TimeValueException() : LabelParseException(), time_values("") {} // 添加默认构造函数
  explicit TimeValueException(const string &Msg, const std::string &msg)
      : LabelParseException(Msg), time_values(msg) {}

  const char *what() const noexcept override { return time_values.c_str(); }
};

TDG::TDG(string tdg_file) {
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
      vertex_index.insert({id, v});
      BOOST_LOG_TRIVIAL(debug) << label;
      NodeType node_type;
      try {
        node_type = parse_vertex_label(label);
      } catch (const LabelParseException &ex) {
        BOOST_LOG_TRIVIAL(error) << id << "'s label is wrong!";
      } catch (const boost::exception &ex) {
        std::cerr << "Boost Exception caught: "
                  << boost::diagnostic_information(ex) << std::endl;
      } catch (const std::exception &ex) {
        std::cerr << "Standard Exception caught: " << ex.what() << std::endl;
      }
      if (holds_alternative<PeriodicTask>(node_type)) {
        PeriodicTask p_task = get<PeriodicTask>(node_type);
        string t_name = p_task.name;
        all_task.emplace_back(p_task);
        tasks_priority.insert(make_pair(t_name, p_task.priority));
        nodes_type.insert(make_pair(t_name, p_task));
        vertexes_type.insert(make_pair(t_name, VertexType::TASK));
        BOOST_LOG_TRIVIAL(info)
            << t_name << ": " << TaskTypeToString[p_task.task_type];
      } else if (holds_alternative<APeriodicTask>(node_type)) {
        APeriodicTask ap_task = get<APeriodicTask>(node_type);
        string t_name = ap_task.name;

        all_task.emplace_back(ap_task);
        tasks_priority.insert(make_pair(t_name, ap_task.priority));
        nodes_type.insert(make_pair(t_name, ap_task));
        vertexes_type.insert(make_pair(t_name, VertexType::TASK));
        BOOST_LOG_TRIVIAL(info)
            << t_name << ": " << TaskTypeToString[ap_task.task_type];
      } else if (holds_alternative<SyncTask>(node_type)) {
        SyncTask s_task = get<SyncTask>(node_type);
        string t_name = s_task.name;
        nodes_type.insert(make_pair(t_name, s_task));
        vertexes_type.insert(make_pair(t_name, VertexType::SYNC));
        BOOST_LOG_TRIVIAL(info) << t_name << ": " << "type: SYNC";
      } else if (holds_alternative<DistTask>(node_type)) {
        DistTask d_task = get<DistTask>(node_type);
        string t_name = d_task.name;

        //              BOOST_STATIC_ASSERT_MSG(res > 0, "ID must be equal
        //              label.0");
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
    BOOST_FOREACH (TDG_RAP::edge_descriptor e, edges(tdg)) {
      //      auto source_name = tdg[source(e, tdg)].name;
      //      auto target_name = tdg[target(e, tdg)].name;
      BOOST_LOG_TRIVIAL(debug)
          << "Edge: " << tdg[e].label << "weight: " << stoi(tdg[e].label);
    }
  }
}

// 按照 label 类型返回
NodeType TDG::parse_vertex_label(const string &label) {
  if (label.empty()) {
    BOOST_THROW_EXCEPTION(LabelParseException("Label cannot be empty"));
  }
  NodeType node_type;
  regex rgx("\\{(.*?)\\}");
  smatch matches;
  if (regex_search(label, matches, rgx)) {
    string content = matches[1].str();
    if (content.empty()) {
      BOOST_THROW_EXCEPTION(LabelParseException("Empty content in label: " + label));
    } 
    vector<std::string> parts;
    istringstream ss(content);
    string token;
    while (getline(ss, token, ';')) {
      parts.push_back(token);
    }

    // 检查是否至少包含名称
  if (parts.empty()) {
    BOOST_THROW_EXCEPTION(LabelParseException("No task name found in label"));
  }

  string name = parts[0];
  if (name.empty()) {
    BOOST_THROW_EXCEPTION(LabelParseException("Task name cannot be empty"));
  }

  // 检查是否有足够的部分用于解析
  if (parts.size() < 2) {
    BOOST_THROW_EXCEPTION(
        LabelParseException("Insufficient parameters in label for task: " + name));
  }

    // 根据长度判断 vertex 属于那种类型和是否包含锁变量
    // 判断第二个元素是否是时间
    try {
    vector<int> period_times = parse_time_vec(parts[1]);
    if (period_times.empty()) {
      //  size <=2，不属于任务类型,
      //  根据名字开头字母判断属于同步、分发或空节点
      if (parts.size() <= 2 && parts[0].substr(0, 4) == "Wait") {
        return SyncTask{name};
      } else if (parts.size() <= 2 && parts[0].substr(0, 4) == "Dist") {
        return DistTask{name};
      } else if (parts.size() <= 2 && parts[0].substr(0, 4) == "Empty") {
        return EmptyTask{name};
      } else {
        
        int task_priority, task_core;
        try {
          task_priority = stoi(parts[1]);
          task_core = stoi(parts[2]);
        } catch (const std::invalid_argument& e) {
          BOOST_THROW_EXCEPTION(
              TimeValueException("Invalid number format in task parameters for: " + name,
                               "Failed to parse priority or core count"));
        }

        if (task_priority < 0) {
          BOOST_THROW_EXCEPTION(
              TimeValueException("Invalid priority value for task: " + name,
                               "Priority must be non-negative"));
        }
        if (task_core < 0) {
          BOOST_THROW_EXCEPTION(
              TimeValueException("Invalid core count for task: " + name,
                               "Core count must be non-negative"));
        }
        vector<pair<int, int>> task_times;
        vector<int> time_values = parse_time_vec(parts[3]);
        for (size_t i = 0; i < time_values.size(); i += 2) {
          task_times.emplace_back(time_values[i], time_values[i + 1]);
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
            } else {
              task_locks_map[name].push_back(lock_token);
            }
          }
        }

        tasks_type.insert(make_pair(name, TaskType::NORMAL));
        return APeriodicTask{name,       task_core, task_priority,
                             task_times, is_lock,   task_locks};
      }
    } else {
      pair<int, int> task_period_time =
          make_pair(period_times[0], period_times[1]);
      int task_priority = stoi(parts[2]);
      int task_core = stoi(parts[3]);
      vector<pair<int, int>> task_times;
      vector<int> time_values = parse_time_vec(parts[4]);
      for (size_t i = 0; i < time_values.size(); i += 2) {
        task_times.emplace_back(time_values[i], time_values[i + 1]);
      }
      vector<string> task_locks;
      bool is_lock = false;
      if (parts.size() >= 6) {
        is_lock = true;
        string lock_token;
        string locks_name = parts[5];
        istringstream lock_stream(locks_name);
        while (getline(lock_stream, lock_token, ',')) {
          task_locks.push_back(lock_token);
          lock_set.insert(lock_token);
          if (task_locks_map.find(name) == task_locks_map.end()) {
            vector<string> locks;
            locks.push_back(lock_token);
            task_locks_map.insert(make_pair(name, locks));
          } else {
            task_locks_map[name].push_back(lock_token);
          }
        }
      }
      // 如果命名中有中断任务
      if (name.substr(0, 9) == "Interrupt") {
        tasks_type.insert(make_pair(name, TaskType::INTERRUPT));
        BOOST_LOG_TRIVIAL(debug) << "Interrupt task:" << name;
        return PeriodicTask{
            name,    task_core,  task_priority,       task_times,
            is_lock, task_locks, TaskType::INTERRUPT, task_period_time};
      } else if (name.substr(0, 9) == "Sporadic") {
        BOOST_LOG_TRIVIAL(debug) << "Sporadic task:" << name;
        tasks_type.insert(make_pair(name, TaskType::APERIOD));
        return PeriodicTask{
            name,    task_core,  task_priority,     task_times,
            is_lock, task_locks, TaskType::APERIOD, task_period_time};
      } else {
        BOOST_LOG_TRIVIAL(debug) << "Periodic task:" << name;
        tasks_type.insert(make_pair(name, TaskType::PERIOD));
        return PeriodicTask{
            name,    task_core,  task_priority,    task_times,
            is_lock, task_locks, TaskType::PERIOD, task_period_time};
        }
      }
    } catch (const TimeValueException& e) {
      throw;
    }catch (const std::exception &ex) {
      BOOST_THROW_EXCEPTION(LabelParseException("Error parsing task parameters: " + string(ex.what())));
    } 
  } else {
    BOOST_THROW_EXCEPTION(LabelParseException("Invalid label format: " + label));
  }
}

vector<int> TDG::parse_time_vec(string times_string) {
  if (times_string.empty()) {
    BOOST_THROW_EXCEPTION(
        TimeValueException("Empty time string", 
                         "Time string cannot be empty"));
  }
  std::vector<int> values;
  std::regex rgx(R"(\[(\d+),(\d+)\])");
  std::smatch matches;

  try {
    std::string::const_iterator start = times_string.begin();
    std::string::const_iterator end = times_string.end();
    
    bool found_match = false;
    while (std::regex_search(start, end, matches, rgx)) {
      found_match = true;
      
      // 检查匹配组的数量
      if (matches.size() != 3) { // 完整匹配 + 两个捕获组
        BOOST_THROW_EXCEPTION(
            TimeValueException("Invalid time format", 
                             "Expected format: [number,number]"));
      }

      int first_value, second_value;
      try {
        first_value = std::stoi(matches[1].str());
        second_value = std::stoi(matches[2].str());
      } catch (const std::invalid_argument&) {
        BOOST_THROW_EXCEPTION(
            TimeValueException("Invalid number format", 
                             "Failed to convert string to integer in: " + times_string));
      } catch (const std::out_of_range&) {
        BOOST_THROW_EXCEPTION(
            TimeValueException("Number out of range", 
                             "Number too large in: " + times_string));
      }

      // 验证时间值的合理性
      if (first_value < 0 || second_value < 0) {
        BOOST_THROW_EXCEPTION(
            TimeValueException("Invalid time value", 
                             "Time values must be non-negative"));
      }

      if (first_value > second_value) {
        BOOST_THROW_EXCEPTION(
            TimeValueException("Invalid time range", 
                             "Start time must be less than or equal to end time"));
      }

      values.push_back(first_value);
      values.push_back(second_value);
      start = matches.suffix().first;
    }

    if (!found_match) {
      BOOST_THROW_EXCEPTION(
          TimeValueException("No valid time ranges found", 
                           "Input string does not contain any valid time ranges: " + times_string));
    }

    // 检查结果向量的合理性
    if (values.size() % 2 != 0) {
      BOOST_THROW_EXCEPTION(
          TimeValueException("Invalid number of values", 
                           "Number of parsed values must be even"));
    }

  } catch (const std::regex_error& e) {
    BOOST_THROW_EXCEPTION(
        TimeValueException("Regex error", 
                         "Error in regular expression matching: " + string(e.what())));
  }

  return values;
}

// 根据优先级划分任务
std::unordered_map<int, vector<string>> TDG::classify_priority() {
  std::unordered_map<int, vector<string>> core_task;
  // 找个每个核心上的任务
  for (const auto &task : all_task) {
    if (holds_alternative<APeriodicTask>(task)) {
      auto result = get<APeriodicTask>(task);
      TaskConfig tc = {result.core, result.priority, result.time, result.lock};
      tasks_config.insert({result.name, tc});
      core_task[result.core].push_back(result.name);
    } else if (holds_alternative<PeriodicTask>(task)) {
      auto result = get<PeriodicTask>(task);
      TaskConfig tc = {result.core, result.priority, result.time, result.lock};
      tasks_config.insert({result.name, tc});
      core_task[result.core].push_back(result.name);
    } else {
      continue;
    }
  }
  // 根据任务优先级排序
  for (auto &tasks : core_task) {
    std::sort(tasks.second.begin(), tasks.second.end(),
              [&](const string &t1, const string &t2) {
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