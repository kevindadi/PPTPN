#include "clap.h"
#include "json_tdg.h"

#include <spdlog/spdlog.h>
#include <boost/exception/all.hpp>
#include <algorithm>
#include <fstream>
#include <regex>
#include <utility>

struct LabelParseException : virtual boost::exception, virtual std::exception {
  std::string label;
  LabelParseException() = default;
  explicit LabelParseException(std::string msg) : label(std::move(msg)) {}

  const char *what() const noexcept override { return label.c_str(); }
};

struct TimeValueException : virtual LabelParseException {
  std::string time_values;
  TimeValueException() = default;
  explicit TimeValueException(const string &Msg, std::string msg)
      : LabelParseException(Msg), time_values(std::move(msg)) {}

  const char *what() const noexcept override { return time_values.c_str(); }
};

// 解析 DAG 文件,获取任务的名字,核心,优先级,锁和时间
// 根据核心数量,取分配的最大核心数,锁根据名称分类
void TDG::parse_tdg() {
  boost::ref_property_map<TDG_RAP *, std::string> dag_name(
      get_property(tdg, boost::graph_name));
  tdg_dp.property("name", dag_name);
  tdg_dp.property("node_id", get(&DAGVertex::name, tdg));
  tdg_dp.property("label", get(&DAGVertex::label, tdg));
  tdg_dp.property("shape", get(&DAGVertex::shape, tdg));
  tdg_dp.property("xlabel", get(&DAGEdge::label, tdg));
  tdg_dp.property("style", get(&DAGEdge::style, tdg));

  // typename boost::property_map<TDG_RAP, boost::vertex_index_t>::type index =
  //     get(boost::vertex_index, tdg);
  if (std::ifstream tdg_stream(tdg_file);
      read_graphviz(tdg_stream, tdg, tdg_dp)) {
    spdlog::info("[TDG] Graph Name: {}", get_property(tdg, boost::graph_name));
    // 遍历节点,确定节点类型
    BOOST_FOREACH (TDG_RAP::vertex_descriptor v, vertices(tdg)) {
      // TODO: id = label.name
      std::string id = get("node_id", tdg_dp, v);
      std::string label = get("label", tdg_dp, v);
      vertex_index.insert({id, v});
      spdlog::debug << "[TDG] " << label;
      NodeType node_type;
      try {
        node_type = parse_vertex_label(label);
      } catch (const LabelParseException &ex) {
        spdlog::error << "[TDG] " << id << "的label错误!";
      } catch (const boost::exception &ex) {
        std::cerr << "Boost Exception caught: "
                  << boost::diagnostic_information(ex) << std::endl;
      } catch (const std::exception &ex) {
        std::cerr << "Standard Exception caught: " << ex.what() << std::endl;
      }
      if (holds_alternative<PeriodicTask>(node_type)) {
        auto p_task = get<PeriodicTask>(node_type);
        string t_name = p_task.name;
        all_task.emplace_back(p_task);
        tasks_priority.insert(make_pair(t_name, p_task.priority));
        nodes_type.insert(make_pair(t_name, p_task));
        vertexes_type.insert(make_pair(t_name, TDGVertexType::TASK));
        info("[TDG] {}: {}", t_name, TaskTypeToString[p_task.task_type]);
      } else if (holds_alternative<APeriodicTask>(node_type)) {
        auto ap_task = get<APeriodicTask>(node_type);
        string t_name = ap_task.name;

        all_task.emplace_back(ap_task);
        tasks_priority.insert(make_pair(t_name, ap_task.priority));
        nodes_type.insert(make_pair(t_name, ap_task));
        vertexes_type.insert(make_pair(t_name, TDGVertexType::TASK));
        spdlog::info("[TDG] ") << t_name << ": "
                                << TaskTypeToString[ap_task.task_type];
      } else if (holds_alternative<JoinTask>(node_type)) {
        auto s_task = get<JoinTask>(node_type);
        string t_name = s_task.name;
        nodes_type.insert(make_pair(t_name, s_task));
        vertexes_type.insert(make_pair(t_name, TDGVertexType::JOIN));
        info("[TDG] {}: type: JOIN", t_name);
      } else if (holds_alternative<ForkTask>(node_type)) {
        auto d_task = get<ForkTask>(node_type);
        string t_name = d_task.name;

        //              BOOST_STATIC_ASSERT_MSG(res > 0, "ID must be equal
        //              label.0");
        nodes_type.insert(make_pair(t_name, d_task));
        vertexes_type.insert(make_pair(t_name, TDGVertexType::FORK));
        info("[TDG] {}: type: FORK", t_name);
      } else {
        auto e_task = get<EmptyTask>(node_type);
        string t_name = e_task.name;
        nodes_type.insert(make_pair(t_name, e_task));
        vertexes_type.insert(make_pair(t_name, TDGVertexType::EMPTY));
        info("[TDG] {}: type: EMPTY", t_name);
      }
    }
    // 遍历边,找到自环或回环,确定周期任务
    BOOST_FOREACH (TDG_RAP::edge_descriptor e, edges(tdg)) {
      //      auto source_name = tdg[source(e, tdg)].name;
      //      auto target_name = tdg[target(e, tdg)].name;
      spdlog::debug << "[TDG] Edge: " << tdg[e].label;
    }
  }
}

bool is_time_range(const string &str) {
  return str.front() == '[' && str.back() == ']' &&
         str.find(',') != string::npos;
}

// 按照 label 类型返回
NodeType TDG::parse_vertex_label(const string &label) {
  if (label.empty()) {
    BOOST_THROW_EXCEPTION(LabelParseException("Label cannot be empty"));
  }
  info("[TDG] Parsing label: ", label);
  NodeType node_type;
  regex rgx("\\{(.*?)\\}");
  if (smatch matches; regex_search(label, matches, rgx)) {
    string content = matches[1].str();
    if (content.empty()) {
      BOOST_THROW_EXCEPTION(
          LabelParseException("Empty content in label: " + label));
    }
    vector<std::string> parts;
    istringstream ss(content);
    string token;
    while (getline(ss, token, ';')) {
      parts.push_back(token);
    }

    if (parts.empty()) {
      BOOST_THROW_EXCEPTION(LabelParseException("No task name found in label"));
    }

    string name = parts[0];
    if (name.empty()) {
      BOOST_THROW_EXCEPTION(LabelParseException("Task name cannot be empty"));
    }

    // 首先处理特殊节点
    if (parts.size() <= 2) {
      if (name.substr(0, 4) == "Wait") {
        return JoinTask{name};
      } else if (name.substr(0, 4) == "Dist") {
        return ForkTask{name};
      } else if (name.substr(0, 5) == "Empty") {
        return EmptyTask{name};
      }
    }

    bool has_period = false;
    vector<int> period_times;
    try {
      period_times = parse_time_vec(parts[1]);
      has_period = true;
    } catch (const TimeValueException &e) {
      error("[TDG] parse non-periodic task: ");
    }

    if (has_period) {
      // 处理周期性任务
      pair<int, int> task_period_time =
          make_pair(period_times[0], period_times[1]);
      // int task_priority = stoi(parts[2]);
      // int task_core = stoi(parts[3]);
      vector<pair<int, int>> task_times;
      vector<int> time_values = parse_time_vec(parts[4]);

      for (size_t i = 0; i < time_values.size(); i += 2) {
        task_times.emplace_back(time_values[i], time_values[i + 1]);
      }

      vector<string> task_locks;
      bool is_lock = false;
      if (parts.size() >= 6) {
        is_lock = true;
        const string &locks_name = parts[5];
        istringstream lock_stream(locks_name);
        string lock_token;
        while (getline(lock_stream, lock_token, ',')) {
          task_locks.push_back(lock_token);
          lock_set.insert(lock_token);
          if (task_locks_map.find(name) == task_locks_map.end()) {
            task_locks_map[name] = {lock_token};
          } else {
            task_locks_map[name].push_back(lock_token);
          }
        }
      }

      TaskType task_type = TaskType::PERIOD;
      if (name.substr(0, 9) == "Interrupt") {
        task_type = TaskType::PERIOD;
      } else if (name.substr(0, 8) == "Sporadic") {
        task_type = TaskType::APERIOD;
      }

      tasks_type.insert(make_pair(name, task_type));
      return PeriodicTask{name,       stoi(parts[3]),  stoi(parts[2]),
                          task_times, is_lock,         task_locks,
                          task_type,  task_period_time};
    } else {
      // 处理非周期性任务
      if (parts.size() < 4) {
        BOOST_THROW_EXCEPTION(LabelParseException(
            "Insufficient parameters for aperiodic task: " + name));
      }

      // int task_priority = stoi(parts[1]);
      // int task_core = stoi(parts[2]);
      vector<pair<int, int>> task_times;
      vector<int> time_values = parse_time_vec(parts[3]);

      for (size_t i = 0; i < time_values.size(); i += 2) {
        task_times.emplace_back(time_values[i], time_values[i + 1]);
      }

      vector<string> task_locks;
      bool is_lock = false;
      if (parts.size() >= 5) {
        is_lock = true;
        const string &locks_name = parts[4];
        istringstream lock_stream(locks_name);
        string lock_token;
        while (getline(lock_stream, lock_token, ',')) {
          task_locks.push_back(lock_token);
          lock_set.insert(lock_token);
          if (task_locks_map.find(name) == task_locks_map.end()) {
            task_locks_map[name] = {lock_token};
          } else {
            task_locks_map[name].push_back(lock_token);
          }
        }
      }

      tasks_type.insert(make_pair(name, TaskType::NORMAL));
      return APeriodicTask{name,       stoi(parts[2]), stoi(parts[1]),
                           task_times, is_lock,        task_locks};
    }
  }
  BOOST_THROW_EXCEPTION(LabelParseException("Invalid label format: " + label));
}

vector<int> TDG::parse_time_vec(string times) {
  if (times.empty()) {
    BOOST_THROW_EXCEPTION(
        TimeValueException("Empty time string", "Time string cannot be empty"));
  }
  std::vector<int> values;
  const std::regex rgx(R"(\[(\d+),(\d+)\])");

  try {
    std::smatch matches;
    std::string::const_iterator start = times.begin();
    std::string::const_iterator end = times.end();

    bool found_match = false;
    while (std::regex_search(start, end, matches, rgx)) {
      found_match = true;

      // 检查匹配组的数量
      if (matches.size() != 3) {  // 完整匹配 + 两个捕获组
        BOOST_THROW_EXCEPTION(TimeValueException(
            "Invalid time format", "Expected format: [number,number]"));
      }

      int first_value, second_value;
      try {
        first_value = std::stoi(matches[1].str());
        second_value = std::stoi(matches[2].str());
      } catch (const std::invalid_argument &) {
        BOOST_THROW_EXCEPTION(TimeValueException(
            "Invalid number format",
            "Failed to convert string to integer in: " + times));
      } catch (const std::out_of_range &) {
        BOOST_THROW_EXCEPTION(TimeValueException(
            "Number out of range", "Number too large in: " + times));
      }

      // 验证时间值的合理性
      if (first_value < 0 || second_value < 0) {
        BOOST_THROW_EXCEPTION(TimeValueException(
            "Invalid time value", "Time values must be non-negative"));
      }

      if (first_value > second_value) {
        BOOST_THROW_EXCEPTION(TimeValueException(
            "Invalid time range",
            "Start time must be less than or equal to end time"));
      }

      values.push_back(first_value);
      values.push_back(second_value);
      start = matches.suffix().first;
    }

    if (!found_match) {
      BOOST_THROW_EXCEPTION(TimeValueException(
          "No valid time ranges found",
          "Input string does not contain any valid time ranges: " + times));
    }

    // 检查结果向量的合理性
    if (values.size() % 2 != 0) {
      BOOST_THROW_EXCEPTION(TimeValueException(
          "Invalid number of values", "Number of parsed values must be even"));
    }
  } catch (const std::regex_error &e) {
    BOOST_THROW_EXCEPTION(TimeValueException(
        "Regex error",
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
  for (auto &[fst, snd] : core_task) {
    std::sort(snd.begin(), snd.end(), [&](const string &t1, const string &t2) {
      return tasks_priority[t1] < tasks_priority[t2];
    });
  }

  for (auto &[fst, snd] : core_task) {
    std::stringstream ss;
    ss << "[TDG] Core: " << fst << " [ ";
    for (const auto &task : snd) {
      ss << task << " < ";
    }
    ss << " ]";
    spdlog::info("{}", ss.str());
  }

  return core_task;
}

// 静态方法:检测输入文件格式
InputFormat TDG::detect_format(const std::string& file_path) {
  size_t len = file_path.length();
  if (len >= 5) {
    std::string ext = file_path.substr(len - 5);
    for (char& c : ext) c = std::tolower((unsigned char)c);
    if (ext == ".json") {
      return InputFormat::JSON;
    }
  }
  if (len >= 4) {
    std::string ext = file_path.substr(len - 4);
    for (char& c : ext) c = std::tolower((unsigned char)c);
    if (ext == ".dot") {
      return InputFormat::DOT;
    }
  }

  // 尝试通过内容检测
  std::ifstream file(file_path);
  if (file.is_open()) {
    std::string first_line;
    if (std::getline(file, first_line)) {
      // JSON 通常以 { 开始
      if (first_line.find('"') != std::string::npos &&
          first_line.find("nodes") != std::string::npos) {
        return InputFormat::JSON;
      }
    }
  }
  return InputFormat::DOT;
}

// JSON 文件解析入口
void TDG::parse_json(const std::string& json_file) {
  info("[TDG] Starting JSON parsing: ", json_file);

  json_tdg::JsonTDGParser parser;
  auto result = parser.parse_file(json_file);

  if (!result.success) {
    spdlog::error << "[TDG] JSON parsing failed: " << result.error_message;
    BOOST_THROW_EXCEPTION(LabelParseException("JSON parsing failed: " + result.error_message));
    return;
  }

  // 记录配置
  num_cpus = parser.get_num_cpus();
  cores_per_cpu = parser.get_cores_per_cpu();
  tdg_file = json_file;

  spdlog::info("[TDG] Configuration: ") << num_cpus << " CPUs, "
                           << cores_per_cpu << " cores per CPU";

  // 转换节点
  for (const auto& json_node : parser.get_nodes()) {
    std::string id = json_node.id;
    NodeType node_type = json_node.to_node_type();

    if (std::holds_alternative<PeriodicTask>(node_type)) {
      const auto& task = std::get<PeriodicTask>(node_type);
      all_task.emplace_back(node_type);
      tasks_priority.insert({task.name, task.priority});
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::TASK});
      tasks_type.insert({task.name, TaskType::PERIOD});

      // 记录锁
      for (const auto& lock : task.lock) {
        lock_set.insert(lock);
        task_locks_map[task.name].push_back(lock);
      }

      spdlog::info("[TDG] Node '") << task.name << "' -> periodic "
                               << "(priority=" << task.priority
                               << ", core=" << task.core << ")";

    } else if (std::holds_alternative<APeriodicTask>(node_type)) {
      const auto& task = std::get<APeriodicTask>(node_type);
      all_task.emplace_back(node_type);
      tasks_priority.insert({task.name, task.priority});
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::TASK});
      tasks_type.insert({task.name, TaskType::NORMAL});

      // 记录锁
      for (const auto& lock : task.lock) {
        lock_set.insert(lock);
        task_locks_map[task.name].push_back(lock);
      }

      spdlog::info("[TDG] Node '") << task.name << "' -> aperiodic "
                               << "(priority=" << task.priority
                               << ", core=" << task.core << ")";

    } else if (std::holds_alternative<ForkTask>(node_type)) {
      const auto& task = std::get<ForkTask>(node_type);
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::FORK});
      info("[TDG] Node '{}' -> fork", task.name);

    } else if (std::holds_alternative<JoinTask>(node_type)) {
      const auto& task = std::get<JoinTask>(node_type);
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::JOIN});
      info("[TDG] Node '{}' -> join", task.name);

    } else if (std::holds_alternative<EmptyTask>(node_type)) {
      const auto& task = std::get<EmptyTask>(node_type);
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::EMPTY});
      info("[TDG] Node '{}' -> empty", task.name);
    }
  }

  // 转换边
  for (const auto& edge : parser.get_edges()) {
    tdg_edges.emplace_back(edge.source, edge.target, edge.label, edge.style);
    spdlog::debug << "[TDG] Edge: " << edge.source << " -> " << edge.target
                              << " (style=" << edge.style << ")";
  }

  spdlog::info("[TDG] JSON parsing completed: ")
                           << nodes_type.size() << " nodes, " << tdg_edges.size()
                           << " edges";
}

// 从 JSON 字符串解析
void TDG::parse_json_string(const std::string& json_content) {
  json_tdg::JsonTDGParser parser;
  auto result = parser.parse_string(json_content);

  if (!result.success) {
    throw std::runtime_error("JSON parsing failed: " + result.error_message);
  }

  // 更新配置
  num_cpus = parser.get_num_cpus();
  cores_per_cpu = parser.get_cores_per_cpu();

  // 转换节点
  for (const auto& json_node : parser.get_nodes()) {
    std::string id = json_node.id;
    NodeType node_type = json_node.to_node_type();

    if (std::holds_alternative<PeriodicTask>(node_type)) {
      const auto& task = std::get<PeriodicTask>(node_type);
      all_task.emplace_back(node_type);
      tasks_priority.insert({task.name, task.priority});
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::TASK});
      tasks_type.insert({task.name, TaskType::PERIOD});

      for (const auto& lock : task.lock) {
        lock_set.insert(lock);
        task_locks_map[task.name].push_back(lock);
      }

    } else if (std::holds_alternative<APeriodicTask>(node_type)) {
      const auto& task = std::get<APeriodicTask>(node_type);
      all_task.emplace_back(node_type);
      tasks_priority.insert({task.name, task.priority});
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::TASK});
      tasks_type.insert({task.name, TaskType::NORMAL});

      for (const auto& lock : task.lock) {
        lock_set.insert(lock);
        task_locks_map[task.name].push_back(lock);
      }

    } else if (std::holds_alternative<ForkTask>(node_type)) {
      const auto& task = std::get<ForkTask>(node_type);
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::FORK});

    } else if (std::holds_alternative<JoinTask>(node_type)) {
      const auto& task = std::get<JoinTask>(node_type);
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::JOIN});

    } else if (std::holds_alternative<EmptyTask>(node_type)) {
      const auto& task = std::get<EmptyTask>(node_type);
      nodes_type.insert({task.name, node_type});
      vertexes_type.insert({task.name, TDGVertexType::EMPTY});
    }
  }

  // 转换边
  for (const auto& edge : parser.get_edges()) {
    tdg_edges.emplace_back(edge.source, edge.target, edge.label, edge.style);
  }
}

// 导出为 DOT 格式字符串
std::string TDG::to_dot_string() const {
  std::ostringstream oss;

  oss << "digraph G {\n";

  // 导出节点
  for (const auto& [name, node] : nodes_type) {
    oss << "    " << name << " [label = \"" << json_tdg::node_to_dot_label(node)
        << "\";];\n";
  }

  // 导出边
  for (const auto& edge : tdg_edges) {
    std::string source, target, label, style;
    std::tie(source, target, label, style) = edge;

    oss << "    " << source << " -> " << target;
    if (!label.empty() || !style.empty()) {
      oss << " [";
      if (!label.empty()) {
        oss << "xlabel = \"" << label << "\"";
      }
      if (!style.empty()) {
        if (!label.empty()) oss << "; ";
        oss << "style = \"" << style << "\"";
      }
      oss << ";]";
    }
    oss << ";\n";
  }

  oss << "}\n";
  return oss.str();
}

// 导出到 DOT 文件
void TDG::export_to_dot(const std::string& output_path) {
  info("[DOT] Exporting to: ", output_path);

  std::ofstream file(output_path);
  if (!file.is_open()) {
    spdlog::error << "[DOT] Failed to create file: " << output_path;
    return;
  }

  std::string dot_content = to_dot_string();
  file << dot_content;
  file.close();

  spdlog::info("[DOT] Exported ") << nodes_type.size()
                           << " nodes, " << tdg_edges.size() << " edges to "
                           << output_path;
}