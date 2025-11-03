//
// Created by 张凯文 on 2024/3/22.
//
#include "priority_time_petri_net.h"
#include <boost/filesystem.hpp>
#include <boost/property_map/property_map.hpp>
#include <utility>
#include <boost/log/trivial.hpp>

namespace ptpn
{
  ptpn_v_desc add_place(PriorityTPNGraph &graph, const std::string &name,
                        const int token = 0, const int capacity = 1)
  {
    Vertex v;
    v.name = name;
    v.label = name; // 默认 label 与 name 相同
    v.shape = "circle";
    v.node = Place{};
    auto &[p_token, p_capacity] = std::get<Place>(v.node);
    p_token = token;
    p_capacity = capacity;
    return add_vertex(v, graph);
  }

  ptpn_v_desc add_transition(PriorityTPNGraph &graph, const std::string &name,
                             const int priority = 255, const int core = 255,
                             const std::pair<int, int> const_time = {0, 0},
                             const bool is_handle = false,
                             const std::pair<int, int> runtimes = {0, 0})
  {
    Vertex v;
    v.name = name;
    v.label = name;
    v.shape = "box";
    v.node = Transition{};
    auto &trans = std::get<Transition>(v.node);
    trans.priority = priority;
    trans.core = core;
    trans.const_time = const_time;
    trans.handle = is_handle;
    trans.runtimes = runtimes;
    return add_vertex(v, graph);
  }

  std::string PriorityTPN::save_ptpn_and_dot(const std::string &file_path)
  {
    boost::filesystem::path dot_filename;
    
    // 检查输入是目录还是文件
    if (const boost::filesystem::path path(file_path);
        boost::filesystem::is_directory(path))
    {
      // 如果是目录,在目录下创建ptpn_graph.dot
      dot_filename = path / "ptpn_graph.dot";
    }
    else
    {
      // 如果是文件,直接使用该文件名
      dot_filename = path;
    }
    
    // 确保父目录存在
    if (!boost::filesystem::exists(dot_filename.parent_path()))
    {
      try
      {
        boost::filesystem::create_directories(dot_filename.parent_path());
      }
      catch (const std::exception &e)
      {
        BOOST_LOG_TRIVIAL(error) << "[PTPN] 无法创建目录: " << e.what();
        return {};
      }
    }

    std::ofstream ofs(dot_filename.string());
    if (!ofs)
    {
      BOOST_LOG_TRIVIAL(error) << "[PTPN] 无法打开dot 文件: " << dot_filename.string();
      return {};
    }

    graph_dp.property("node_id", get(&Vertex::name, graph));
    graph_dp.property("label", get(&Vertex::label, graph));
    graph_dp.property("shape", get(&Vertex::shape, graph));
    graph_dp.property("label", get(&Edge::label, graph));

    // 移除可能有问题的graph_name属性设置
    // ref_property_map<PriorityTPN *, std::string> gname_pn(
    //     get_property(graph, graph_name));
    // graph_dp.property("name", gname_pn);

    write_graphviz_dp(ofs, graph, graph_dp);
    ofs.close();

    BOOST_LOG_TRIVIAL(info) << "[PTPN] DOT文件已保存到: " << dot_filename.string();
    return boost::filesystem::absolute(dot_filename).string();
  }

  bool PriorityTPN::export_to_tina(const std::string &file_path)
  {
    try
    {
      boost::filesystem::path path(file_path);

      if (boost::filesystem::path dir = path.parent_path();
          !dir.empty() && !boost::filesystem::exists(dir))
      {
        BOOST_LOG_TRIVIAL(error) << "[PTPN] 目录不存在: " << dir.string();
        return false;
      }

      std::ofstream tina_file(file_path);
      if (!tina_file)
      {
        BOOST_LOG_TRIVIAL(error) << "[PTPN] 无法打开文件: " << file_path;
        return false;
      }

      // 写入网络名称
      tina_file << "net {PriorityTimePetriNet}\n\n";

      // 写入所有库所
      BOOST_FOREACH (ptpn_v_desc v, vertices(graph))
      {
        if (graph[v].is_place())
        {
          const auto &[token, capacity] = graph[v].as_place();
          // Tina格式: pl <place> {:<label>} {(<marking>)} {<input> -> <output>}
          tina_file << "pl " << graph[v].name << " : {" << graph[v].label << "} (" << token << ")";

          // 获取输入变迁
          std::vector<std::string> inputs;
          BOOST_FOREACH (ptpn_v_desc in_v, inv_adjacent_vertices(v, graph))
          {
            if (graph[in_v].is_transition())
            {
              inputs.push_back(graph[in_v].name);
            }
          }

          // 获取输出变迁
          std::vector<std::string> outputs;
          BOOST_FOREACH (ptpn_v_desc out_v, adjacent_vertices(v, graph))
          {
            if (graph[out_v].is_transition())
            {
              outputs.push_back(graph[out_v].name);
            }
          }

          // 如果有输入和输出,添加到声明中
          if (!inputs.empty() && !outputs.empty())
          {
            tina_file << " ";
            for (size_t i = 0; i < inputs.size(); ++i)
            {
              tina_file << inputs[i];
              if (i < inputs.size() - 1)
                tina_file << " ";
            }

            tina_file << " -> ";

            for (size_t i = 0; i < outputs.size(); ++i)
            {
              tina_file << outputs[i];
              if (i < outputs.size() - 1)
                tina_file << " ";
            }
          }

          tina_file << "\n";
        }
      }

      tina_file << "\n";

      // 写入所有变迁
      BOOST_FOREACH (ptpn_v_desc v, vertices(graph))
      {
        if (graph[v].is_transition())
        {
          const Transition &trans = graph[v].as_transition();
          // Tina格式: tr <transition> {:<label>} {<interval>} {<tinput> -> <toutput>}
          tina_file << "tr " << graph[v].name << " : {" << graph[v].label << "} ";

          // 时间区间
          if (trans.const_time.first == 0 && trans.const_time.second == 0)
          {
            tina_file << "[0,w["; // 默认时间区间
          }
          else
          {
            tina_file << "[" << trans.const_time.first << "," << trans.const_time.second << "]";
          }

          // 获取输入库所
          std::vector<std::string> inputs;
          BOOST_FOREACH (ptpn_v_desc in_v, inv_adjacent_vertices(v, graph))
          {
            if (graph[in_v].is_place())
            {
              inputs.push_back(graph[in_v].name);
            }
          }

          // 获取输出库所
          std::vector<std::string> outputs;
          BOOST_FOREACH (ptpn_v_desc out_v, adjacent_vertices(v, graph))
          {
            if (graph[out_v].is_place())
            {
              outputs.push_back(graph[out_v].name);
            }
          }

          // 如果有输入和输出,添加到声明中
          if (!inputs.empty() && !outputs.empty())
          {
            tina_file << " ";
            for (size_t i = 0; i < inputs.size(); ++i)
            {
              tina_file << inputs[i];
              if (i < inputs.size() - 1)
                tina_file << " ";
            }

            tina_file << " -> ";

            for (size_t i = 0; i < outputs.size(); ++i)
            {
              tina_file << outputs[i];
              if (i < outputs.size() - 1)
                tina_file << " ";
            }
          }

          tina_file << "\n";
        }
      }

      // 添加优先级关系（如果有）
      std::map<int, std::vector<std::string>> priority_groups;

      // 按优先级分组所有变迁
      BOOST_FOREACH (ptpn_v_desc v, vertices(graph))
      {
        if (graph[v].is_transition())
        {
          if (const Transition &trans = graph[v].as_transition(); trans.priority != INT_MAX) // 跳过未设置优先级的变迁
          {
            priority_groups[trans.priority].push_back(graph[v].name);
          }
        }
      }

      // 写入优先级关系（高优先级 > 低优先级）
      std::vector<int> priorities;
      priorities.reserve(priority_groups.size());
      for (const auto &[fst, snd] : priority_groups)
      {
        priorities.push_back(fst);
      }

      // 按优先级排序
      std::sort(priorities.begin(), priorities.end());

      // 生成优先级规则
      if (priorities.size() > 1)
      {
        tina_file << "\n# 优先级关系\n";
        for (size_t i = 0; i < priorities.size() - 1; ++i)
        {
          for (size_t j = i + 1; j < priorities.size(); ++j)
          {
            // 低优先级数字大于高优先级数字,因此使用 ">"
            tina_file << "pr ";

            // 添加所有高优先级变迁
            for (size_t h = 0; h < priority_groups[priorities[i]].size(); ++h)
            {
              tina_file << priority_groups[priorities[i]][h];
              if (h < priority_groups[priorities[i]].size() - 1)
                tina_file << " ";
            }

            tina_file << " > ";

            // 添加所有低优先级变迁
            for (size_t l = 0; l < priority_groups[priorities[j]].size(); ++l)
            {
              tina_file << priority_groups[priorities[j]][l];
              if (l < priority_groups[priorities[j]].size() - 1)
                tina_file << " ";
            }

            tina_file << "\n";
          }
        }
      }

      tina_file.close();
      BOOST_LOG_TRIVIAL(info) << "[PTPN] Petri网已导出为Tina格式: " << file_path;
      return true;
    }
    catch (const std::exception &e)
    {
      BOOST_LOG_TRIVIAL(error) << "[PTPN] 导出Tina文件时发生错误: " << e.what();
      return false;
    }
  }

  bool PriorityTPN::export_to_romeo(const std::string &file_path)
  {
    try
    {
      boost::filesystem::path path(file_path);

      if (boost::filesystem::path dir = path.parent_path();
          !dir.empty() && !boost::filesystem::exists(dir))
      {
        BOOST_LOG_TRIVIAL(error) << "[PTPN] 目录不存在: " << dir.string();
        return false;
      }

      std::ofstream romeo_file(file_path);
      if (!romeo_file)
      {
        BOOST_LOG_TRIVIAL(error) << "[PTPN] 无法打开文件: " << file_path;
        return false;
      }

      // XML头部
      romeo_file << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n";
      romeo_file << "<TPN name=\"" << boost::filesystem::absolute(path).string() << "\">\n";

      // 写入所有库所
      int place_id = 1;
      std::map<ptpn_v_desc, int> place_id_map;
      BOOST_FOREACH (ptpn_v_desc v, vertices(graph))
      {
        if (graph[v].is_place())
        {
          const auto &[token, capacity] = graph[v].as_place();
          place_id_map[v] = place_id;

          romeo_file << "  <place id=\"" << place_id << "\" "
                     << "identifier=\"" << graph[v].name << "\" "
                     << "label=\"" << graph[v].label << "\" "
                     << "initialMarking=\"" << token << "\" "
                     << "eft=\"0\" lft=\"inf\">\n";
          romeo_file << "      <graphics color=\"0\">\n";
          romeo_file << "         <position x=\"" << (place_id * 120) << "\" y=\"121\"/>\n";
          romeo_file << "         <deltaLabel deltax=\"32\" deltay=\"-11\"/>\n";
          romeo_file << "      </graphics>\n";
          romeo_file << "      <scheduling gamma=\"1\" omega=\"1\"/>\n";
          romeo_file << "  </place>\n\n";

          place_id++;
        }
      }

      // 写入所有变迁
      int trans_id = 1;
      std::map<ptpn_v_desc, int> trans_id_map;
      BOOST_FOREACH (ptpn_v_desc v, vertices(graph))
      {
        if (graph[v].is_transition())
        {
          const Transition &trans = graph[v].as_transition();
          trans_id_map[v] = trans_id;

          // 由于Romeo不支持同时设置优先级和时间,我们这里只保留时间信息
          romeo_file << "  <transition id=\"" << trans_id << "\" "
                     << "identifier=\"" << graph[v].name << "\" "
                     << "label=\"" << graph[v].label << "\" ";

          if (trans.const_time.first == 0 && trans.const_time.second == 0)
          {
            romeo_file << R"(eft="0" lft="0" )";
          }
          else
          {
            romeo_file << "eft=\"" << trans.const_time.first << "\" "
                       << "lft=\"" << trans.const_time.second << "\" ";
          }

          romeo_file << "speed=\"1\" obs=\"1\" guard=\"\">\n";
          romeo_file << "     <graphics color=\"0\">\n";
          romeo_file << "        <position x=\"" << (trans_id * 120) << "\" y=\"181\"/>\n";
          romeo_file << "        <deltaLabel deltax=\"34\" deltay=\"-16\"/>\n";
          romeo_file << "        <deltaGuard deltax=\"20\" deltay=\"-20\"/>\n";
          romeo_file << "        <deltaUpdate deltax=\"20\" deltay=\"10\"/>\n";
          romeo_file << "        <deltaSpeed deltax=\"-20\" deltay=\"5\"/>\n";
          romeo_file << "     </graphics>\n";
          romeo_file << "     <update></update>\n";
          romeo_file << "  </transition>\n\n";

          trans_id++;
        }
      }

      // 写入所有弧
      BOOST_FOREACH (ptpn_v_desc v, vertices(graph))
      {
        // 处理输出弧
        BOOST_FOREACH (ptpn_v_desc out_v, adjacent_vertices(v, graph))
        {
          if (graph[v].is_place() && graph[out_v].is_transition())
          {
            // 库所到变迁的弧
            romeo_file << "  <arc place=\"" << place_id_map[v] << "\" "
                       << "transition=\"" << trans_id_map[out_v] << "\" "
                       << "type=\"PlaceTransition\" weight=\"1\">\n";
            romeo_file << "    <nail xnail=\"0\" ynail=\"0\"/>\n";
            romeo_file << "    <graphics  color=\"0\">\n";
            romeo_file << "     </graphics>\n";
            romeo_file << "  </arc>\n\n";
          }
          else if (graph[v].is_transition() && graph[out_v].is_place())
          {
            // 变迁到库所的弧
            romeo_file << "  <arc place=\"" << place_id_map[out_v] << "\" "
                       << "transition=\"" << trans_id_map[v] << "\" "
                       << "type=\"TransitionPlace\" weight=\"1\">\n";
            romeo_file << "     <nail xnail=\"0\" ynail=\"0\"/>\n";
            romeo_file << "     <graphics  color=\"0\">\n";
            romeo_file << "     </graphics>\n";
            romeo_file << "  </arc>\n\n";
          }
        }

        // 处理优先级关系,将其转换为抑制弧
        if (graph[v].is_transition())
        {
          const Transition &trans = graph[v].as_transition();
          BOOST_FOREACH (ptpn_v_desc other_v, vertices(graph))
          {
            if (graph[other_v].is_transition() && other_v != v)
            {
              // 如果other_trans优先级更高,添加抑制弧
              if (const Transition &other_trans =
                      graph[other_v].as_transition();
                  other_trans.priority < trans.priority)
              {
                // 为高优先级变迁添加一个虚拟的控制库所
                romeo_file << "  <arc place=\"" << place_id_map[v] << "\" "
                           << "transition=\"" << trans_id_map[other_v] << "\" "
                           << "type=\"timedInhibitor\" weight=\"1\">\n";
                romeo_file << "    <nail xnail=\"0\" ynail=\"0\"/>\n";
                romeo_file << "    <graphics  color=\"0\">\n";
                romeo_file << "     </graphics>\n";
                romeo_file << "  </arc>\n\n";
              }
            }
          }
        }
      }

      // XML尾部
      romeo_file << "  <declaration>// insert here your type definitions using C-like syntax\n\n\n"
                 << "// insert here your function definitions \n"
                 << "// using C-like syntax</declaration>\n\n"
                 << "  <initialization>// insert here the state variables declarations \n"
                 << "// and possibly some code to initialize them \n"
                 << "// using C-like syntax</initialization>\n\n"
                 << "  <preferences>\n"
                 << "      <colorPlace  c0=\"SkyBlue2\"  c1=\"#ffbebe\"  c2=\"cyan\"  c3=\"green\"  c4=\"yellow\"  c5=\"brown\" />\n"
                 << "      <colorTransition  c0=\"yellow\"  c1=\"gray\"  c2=\"cyan\"  c3=\"green\"  c4=\"SkyBlue2\"  c5=\"brown\" />\n"
                 << "      <colorArc  c0=\"black\"  c1=\"gray\"  c2=\"blue\"  c3=\"#beb760\"  c4=\"#be5c7e\"  c5=\"#46be90\" />\n"
                 << "  </preferences>\n"
                 << "</TPN>\n";

      romeo_file.close();
      BOOST_LOG_TRIVIAL(info) << "[PTPN] Petri网已导出为Romeo格式: " << file_path;
      return true;
    }
    catch (const std::exception &e)
    {
      BOOST_LOG_TRIVIAL(error) << "[PTPN] 导出Romeo文件时发生错误: " << e.what();
      return false;
    }
  }

  void PriorityTPN::transform_tdg_to_ptpn(TDG &tdg)
  {
    try
    {
      // 1. 转换顶点
      BOOST_LOG_TRIVIAL(info) << "[PTPN] 开始转换顶点...";
      transform_vertices(tdg);

      // 2. 转换边
      BOOST_LOG_TRIVIAL(info) << "[PTPN] 开始转换边...";
      transform_edges(tdg);

      // 3. 创建优先级抢占关系
      BOOST_LOG_TRIVIAL(info) << "[PTPN] 开始创建优先级抢占关系...";
      add_preempt_task_ptpn(tdg.classify_priority(), tdg.tasks_config,
                            tdg.nodes_type);

      // 4. 添加资源和绑定
      BOOST_LOG_TRIVIAL(info) << "[PTPN] 开始添加资源和绑定...";
      add_resources_and_bindings(tdg);

      // 5. 输出网络信息
      BOOST_LOG_TRIVIAL(info) << "[PTPN] 开始输出网络信息...";
      // 暂时跳过log_network_info()以避免总线错误
      // log_network_info();
      BOOST_LOG_TRIVIAL(info) << "[PTPN] 跳过网络统计信息输出";

      BOOST_LOG_TRIVIAL(info) << "[PTPN] 开始保存DOT文件...";
      save_ptpn_and_dot("../example/");
    }
    catch (const std::exception &e)
    {
      BOOST_LOG_TRIVIAL(error) << "[PTPN] Failed to transform TDG to PTPN: " << e.what();
      throw;
    }

    BOOST_LOG_TRIVIAL(info) << "[PTPN] 开始验证Petri网结构...";
    verify_petri_net_structure();
  }

  void PriorityTPN::transform_vertices(TDG &tdg)
  {
    BOOST_FOREACH (const TDG_RAP::vertex_descriptor v, vertices(tdg.tdg))
    {
      const string &vertex_name = tdg.tdg[v].name;
          BOOST_LOG_TRIVIAL(debug) << "[PTPN] Processing vertex: " << vertex_name;

      try
      {
        auto node_type_it = tdg.nodes_type.find(vertex_name);
        if (node_type_it == tdg.nodes_type.end())
        {
          throw std::runtime_error("Node type not found for: " + vertex_name);
        }

        add_node_ptpn(node_type_it->second);
        // Fix: 当非周期任务作为最后一个任务时,需要补充一个变迁来消耗任务所转换成的库所变迁链中 Exit 库所中的 token
        if (out_degree(v, tdg.tdg) == 0) {
          const auto end_node = node_start_end_map.find(vertex_name)->second.second;
          const ptpn_v_desc consume_token = add_transition(graph, vertex_name + "_consume", 255, 255, {0, 0});
          add_edge(end_node, consume_token, graph);
          BOOST_LOG_TRIVIAL(debug) << "[PTPN] Added consume token transition for APeriodicTask: " << vertex_name;
        }
      }
      catch (const std::exception &e)
      {
        BOOST_LOG_TRIVIAL(error) << "[PTPN] Failed to transform vertex " << vertex_name << ": " << e.what();
        throw;
      }
    }
    BOOST_LOG_TRIVIAL(info) << "[PTPN] Vertex transformation completed";
  }

  void PriorityTPN::transform_edges(TDG &tdg)
  {
    BOOST_FOREACH (TDG_RAP::edge_descriptor e, edges(tdg.tdg))
    {
      try
      {
        const string &source_name = tdg.tdg[source(e, tdg.tdg)].name;
        const string &target_name = tdg.tdg[target(e, tdg.tdg)].name;

        if (is_self_loop_edge(source_name, target_name))
        {
          handle_self_loop_edge(tdg, e, source_name);
          continue;
        }

        if (is_dashed_edge(tdg.tdg[e].style))
        {
          handle_dashed_edge(source_name, target_name);
          continue;
        }

        handle_normal_edge(source_name, target_name);
      }
      catch (const std::exception &exception)
      {
        BOOST_LOG_TRIVIAL(error) << "[PTPN] Failed to transform edge: " << exception.what();
        throw;
      }
    }
  }

  bool PriorityTPN::is_self_loop_edge(const string &source,
                                      const string &target)
  {
    return source == target;
  }

  bool PriorityTPN::is_dashed_edge(const string &edge)
  {
    return edge.find("dashed") != string::npos;
  }

  void PriorityTPN::handle_self_loop_edge(TDG &tdg, TDG_RAP::edge_descriptor e,
                                          const string &source_name)
  {
    int task_period_time = std::stoi(tdg.tdg[e].label);
    auto task_start_end = node_start_end_map.find(source_name);
    if (task_start_end == node_start_end_map.end())
    {
      throw std::runtime_error("Start/end nodes not found for: " + source_name);
    }

    add_monitor_ptpn(source_name, task_period_time, task_start_end->second.first,
                     task_start_end->second.second);
  }

  void PriorityTPN::handle_dashed_edge(const string &source_name,
                                       const string &target_name)
  {
    // 虚线链接的尾节点为开始节点,头节点为结束节点
    // TODO: 实现虚线边的处理逻辑
  }

  void PriorityTPN::handle_normal_edge(const string &source_name,
                                       const string &target_name)
  {
    const auto source_it = node_start_end_map.find(source_name);
    const auto target_it = node_start_end_map.find(target_name);

    if (source_it == node_start_end_map.end() ||
        target_it == node_start_end_map.end())
    {
      throw std::runtime_error("Node mapping not found for edge: " + source_name +
                               " -> " + target_name);
    }

    const ptpn_v_desc source_node = source_it->second.second;
    const ptpn_v_desc target_node = target_it->second.first;

    BOOST_LOG_TRIVIAL(debug) << "[PTPN] source_name: " << source_name;
    BOOST_LOG_TRIVIAL(debug) << "[PTPN] target_name: " << target_name;

    // 如果链接的某个节点属于Dist,Sync则直接链接
    if (source_name.substr(0, 4) == "Dist" ||
        source_name.substr(0, 4) == "Wait")
    {
      add_edge(source_node, target_node, Edge{.weight = 1}, graph);
      return;
    }
    if (target_name.substr(0, 4) == "Dist" ||
        target_name.substr(0, 4) == "Wait")
    {
      add_edge(source_node, target_node, Edge{.weight = 1}, graph);
      return;
    }

    // 添加中间变迁
    const string trans_name = source_name + "_to_" + target_name;
    const ptpn_v_desc middle_trans =
        add_transition(graph, trans_name, 255, 255, {0, 0}, false, {0, 0});

    add_edge(source_node, middle_trans, graph);
    add_edge(middle_trans, target_node, graph);
    BOOST_LOG_TRIVIAL(debug) << "[PTPN] Added edge: " << source_node << " -> " << target_node;
  }

  void PriorityTPN::add_resources_and_bindings(TDG &tdg)
  {
    // 添加CPU资源
    add_cpu_resource(tdg.num_cpus, tdg.cores_per_cpu);

    // 添加锁资源
    add_lock_resource(tdg.lock_set);

    // 绑定任务到CPU
    task_bind_cpu_resource(tdg.all_task);

    // 绑定任务到锁
    task_bind_lock_resource(tdg.all_task, tdg.task_locks_map);
  }

  void PriorityTPN::log_network_info() const {
    try {
      BOOST_LOG_TRIVIAL(info) << "[PTPN] Petri net statistics:";
      
      // 安全地获取顶点数量
      size_t vertex_count = 0;
      try {
        vertex_count = num_vertices(graph);
        BOOST_LOG_TRIVIAL(info) << "[PTPN] - Places + Transitions: " << vertex_count;
      }
      catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "[PTPN] 获取顶点数量时发生错误: " << e.what();
        return;
      }
      
      // 安全地获取边数量
      size_t edge_count = 0;
      try {
        edge_count = num_edges(graph);
        BOOST_LOG_TRIVIAL(info) << "[PTPN] - Flows: " << edge_count;
      }
      catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "[PTPN] 获取边数量时发生错误: " << e.what();
        return;
      }
      
      BOOST_LOG_TRIVIAL(info) << "[PTPN] 网络统计信息获取完成";
    }
    catch (const std::exception &e) {
      BOOST_LOG_TRIVIAL(error) << "[PTPN] 获取网络统计信息时发生错误: " << e.what();
    }
  }

  /// 为锁资源创建库所
  void PriorityTPN::add_lock_resource(const set<string> &locks_name)
  {
    if (locks_name.empty())
    {
      BOOST_LOG_TRIVIAL(info) << "[PTPN] TDG_RAP without locks!";
      return;
    }
    for (const auto &lock_name : locks_name)
    {
      // TODO: 锁类型扩展
      ptpn_v_desc l = add_place(graph, lock_name, 1, 1);
      locks_place.insert(make_pair(lock_name, l));
    }
    BOOST_LOG_TRIVIAL(info) << "[PTPN] create lock resource!";
  }

  /// 为处理器资源创建库所
  void PriorityTPN::add_cpu_resource(const int cpus, const int cores_per_cpu)
  {
    for (int i = 0; i < cpus; i++)
    {
      // TODO: cpu 数量与核心扩展
      string cpu_name = "core" + std::to_string(i);
      ptpn_v_desc c = add_place(graph, cpu_name, cores_per_cpu, cores_per_cpu);
      cpus_place.push_back(c);
    }
    BOOST_LOG_TRIVIAL(info) << "[PTPN] create core resource!";
  }

  /// 根据任务类型绑定不同位置的 CPU
  void PriorityTPN::task_bind_cpu_resource(const vector<NodeType> &all_task)
  {
    for (const auto &task : all_task)
    {
      if (holds_alternative<APeriodicTask>(task))
      {
        auto ap_task = get<APeriodicTask>(task);
        const int cpu_index = ap_task.core;
        auto task_pt_chains = node_pn_map.find(ap_task.name)->second;
        // Random -> Trigger -> Start -> Get CPU -> Run -> Drop CPU -> End
        add_edge(cpus_place[cpu_index], task_pt_chains[1], graph);
        add_edge(task_pt_chains[task_pt_chains.size() - 2], cpus_place[cpu_index],
                 graph);
      }
      else if (holds_alternative<PeriodicTask>(task))
      {
        auto p_task = get<PeriodicTask>(task);
        const int cpu_index = p_task.core;
        auto task_pt_chains = node_pn_map.find(p_task.name)->second;
        // Start -> Get CPU -> Run -> Drop CPU -> End
        add_edge(cpus_place[cpu_index], task_pt_chains[1], graph);
        add_edge(task_pt_chains[task_pt_chains.size() - 2], cpus_place[cpu_index],
                 graph);
      }
      else
      {
        continue;
      }
    }
  }
  /// 根据任务种锁的数量和类型绑定
  void PriorityTPN::task_bind_lock_resource(
      const vector<NodeType> &all_task, std::map<string, vector<string>> &task_locks)
  {
    if (task_locks.empty()) {
      BOOST_LOG_TRIVIAL(info) << "[PTPN] No task locks to bind";
      return;
    }

    // 处理单个任务的锁资源绑定
    auto bind_task_locks =
        [&](const string &task_name, const vector<string> &lock_types,
            const vector<vector<ptpn_v_desc>> &task_pt_chains)
    {
      for (const auto &task_pt_chain : task_pt_chains)
      {
        // 检查链长度是否满足最小要求
        if (constexpr size_t MIN_CHAIN_LENGTH = 5;
            task_pt_chain.size() < MIN_CHAIN_LENGTH)
        {
          BOOST_LOG_TRIVIAL(debug) << "[PTPN] Skip chain for " << task_name << ": too short for locks";
          continue;
        }

        // 获取任务的锁数量
        auto task_locks_it = task_locks.find(task_name);
        if (task_locks_it == task_locks.end())
        {
          BOOST_LOG_TRIVIAL(warning) << "[PTPN] No locks found for task: " << task_name;
          continue;
        }
        const size_t lock_nums = task_locks_it->second.size();
        BOOST_LOG_TRIVIAL(debug) << "[PTPN] chains size for task : " << task_name << " is: " << task_pt_chain.size();
        // 为每个锁添加获取和释放边
        for (size_t i = 0; i < lock_nums; i++)
        {
          try
          {
            // 获取锁类型
            const string &lock_type = lock_types[i];

            // 计算获取和释放锁的节点索引
            const ptpn_v_desc get_lock = task_pt_chain[3 + 2 * i];
            const ptpn_v_desc drop_lock =
                task_pt_chain[task_pt_chain.size() - 2 - 2 * (i + 1)];

            // 查找对应的锁库所
            auto lock_it = locks_place.find(lock_type);
            if (lock_it == locks_place.end())
            {
              throw std::runtime_error("Lock place not found: " + lock_type);
            }
            const ptpn_v_desc lock = lock_it->second;

            // 添加边
            add_edge(lock, get_lock, graph);
            add_edge(drop_lock, lock, graph);

            BOOST_LOG_TRIVIAL(debug) << "[PTPN] Bound lock " << lock_type << " to task " << task_name;
          }
          catch (const std::exception &e)
          {
            BOOST_LOG_TRIVIAL(error) << "[PTPN] Failed to bind lock " << i << " for task " << task_name << ": " << e.what();
            throw;
          }
        }
      }
    };

    // 处理所有任务
    for (const auto &task : all_task)
    {
      try
      {
        if (holds_alternative<APeriodicTask>(task))
        {
          const auto &ap_task = get<APeriodicTask>(task);
          if (auto chains_it = task_pn_map.find(ap_task.name);
              chains_it != task_pn_map.end())
          {
            bind_task_locks(ap_task.name, ap_task.lock, chains_it->second);
          }
        }
        else if (holds_alternative<PeriodicTask>(task))
        {
          const auto &p_task = get<PeriodicTask>(task);
          auto chains_it = task_pn_map.find(p_task.name);
          if (chains_it != task_pn_map.end())
          {
            bind_task_locks(p_task.name, p_task.lock, chains_it->second);
          }
        }
        // 其他类型的任务忽略
      }
      catch (const std::exception &e)
      {
        BOOST_LOG_TRIVIAL(error) << "[PTPN] Failed to process task: " << e.what();
        throw;
      }
    }

    BOOST_LOG_TRIVIAL(info) << "[PTPN] Completed lock resource binding for all tasks";
  }

  /// 映射规则主函数,包含不同类型,便于之后扩展
  /// 根据不同类型进行相应的转换,并返回转换后的开始和结束节点,以进行前后链接
  pair<ptpn_v_desc, ptpn_v_desc> PriorityTPN::add_node_ptpn(NodeType node_type)
  {

    if (holds_alternative<PeriodicTask>(node_type))
    {
      auto p_task = get<PeriodicTask>(node_type);
      auto result = add_p_node_ptpn(p_task);
      node_start_end_map.insert(make_pair(p_task.name, result));
      BOOST_LOG_TRIVIAL(debug) << "[PTPN] " << p_task.name << "'s petri net start node: " << result.first;
      BOOST_LOG_TRIVIAL(debug) << "[PTPN] " << p_task.name << "'s petri net end node: " << result.second;
      return result;
    }
    else if (holds_alternative<APeriodicTask>(node_type))
    {
      auto ap_task = get<APeriodicTask>(node_type);
      auto result = add_ap_node_ptpn(ap_task);
      node_start_end_map.insert(make_pair(ap_task.name, result));
      BOOST_LOG_TRIVIAL(debug) << "[PTPN] " << ap_task.name << "'s petri net start node: " << result.first;
      BOOST_LOG_TRIVIAL(debug) << "[PTPN] " << ap_task.name << "'s petri net end node: " << result.second;
      return result;
    }
    else if (holds_alternative<SyncTask>(node_type))
    {
      auto [name, time] = get<SyncTask>(node_type);
      string t_name = name;
      ptpn_v_desc result = add_transition(graph, "Sync" + to_string(node_index),
                                          255, 255, {0, 0}, false);

      node_index += 1;
      node_start_end_map.insert(make_pair(t_name, make_pair(result, result)));
      BOOST_LOG_TRIVIAL(debug) << "[PTPN] " << t_name << ": type: SYNC";
      return std::make_pair(result, result);
    }
    else if (holds_alternative<DistTask>(node_type))
    {
      auto [name, time] = get<DistTask>(node_type);
      string t_name = name;
      ptpn_v_desc result = add_transition(graph, "Dist" + to_string(node_index),
                                          255, 255, make_pair(0, 0));
      node_index += 1;
      node_start_end_map.insert(make_pair(t_name, make_pair(result, result)));
      BOOST_LOG_TRIVIAL(debug) << "[PTPN] " << t_name << ": type: DIST";
      return std::make_pair(result, result);
    }
    else
    {
      auto [name] = get<EmptyTask>(node_type);
      string t_name = name;
      ptpn_v_desc result =
          add_place(graph, "Empty" + to_string(node_index), 0, 1);
      node_index += 1;
      node_start_end_map.insert(make_pair(t_name, make_pair(result, result)));
            BOOST_LOG_TRIVIAL(debug) << "[PTPN] " << t_name << ": type: EMPTY";
      return std::make_pair(result, result);
    }
  }

  // 处理锁资源的辅助函数
  ptpn_v_desc PriorityTPN::handle_locks(const TaskVertexsNames &names,
                                        vector<ptpn_v_desc> &chain,
                                        const vector<string> &locks,
                                        const vector<pair<int, int>> &times,
                                        const int priority, const int core)
  {
    ptpn_v_desc last_lock_place = 0;
    ptpn_v_desc first_unlock_trans = 0;

    // 获取ready库所（在chain中的倒数第三个位置）
    const ptpn_v_desc ready = chain[chain.size() - 3];

    for (size_t i = 0; i < locks.size(); i++)
    {
      // 获取锁
      string gl = names.get_lock + locks[i];
      ptpn_v_desc get_lock = add_transition(graph, gl, priority, core, {0, 0});
      ptpn_v_desc deal = add_place(graph, names.deal + locks[i], 0);

      // 如果是第一个锁,将ready库所与获取锁的变迁连接
      if (i == 0)
      {
        add_edge(ready, get_lock, graph);
      }
      else
      {
        add_edge(chain.back(), get_lock, graph);
      }
      chain.push_back(get_lock);
      chain.push_back(deal);
      add_edge(get_lock, deal, graph);

      // 记录最后一个锁的库所
      last_lock_place = deal;
    }

    ptpn_v_desc last_unlocked_place = 0;
    // 释放锁
    for (int j = static_cast<int>(locks.size() - 1); j >= 0; j--)
    {
      string dl = names.drop_lock + locks[j];
      ptpn_v_desc drop_lock = add_transition(graph, dl, priority, core, times[j]);
      ptpn_v_desc unlocked = add_place(graph, names.unlock + locks[j], 0);

      add_edge(chain.back(), drop_lock, graph);
      add_edge(drop_lock, unlocked, graph);

      chain.push_back(drop_lock);
      chain.push_back(unlocked);

      // 记录第一个解锁的变迁
      if (j == locks.size() - 1)
      {
        first_unlock_trans = drop_lock;
      }
      if (j == 0)
      {
        last_unlocked_place = unlocked;
      }
    }

    // 将最后一个锁的库所与第一个解锁的变迁连接起来
    if (last_lock_place && first_unlock_trans)
    {
      add_edge(last_lock_place, first_unlock_trans, graph);
    }

    return last_unlocked_place;
  }

  pair<ptpn_v_desc, ptpn_v_desc>
  PriorityTPN::add_p_node_ptpn(PeriodicTask &p_task)
  {
    const TaskVertexsNames names(p_task.name);

    // 创建周期任务特有的随机触发结构
    const string task_random_period = p_task.name + "random";
    const ptpn_v_desc random = add_place(graph, task_random_period, 1, 1);
    const ptpn_v_desc fire =
        add_transition(graph, p_task.name + "fire", 255, 255,
                       p_task.period_time);

    // 创建基本任务结构
    BasicTaskChains basic(graph, names, p_task.priority, p_task.core,
                          p_task.time.back());

    // 添加周期任务特有的边
    add_edge(random, fire, graph);
    add_edge(fire, random, graph);
    add_edge(fire, basic.entry, graph);

    // 处理锁资源
    if (!p_task.lock.empty())
    {
      ptpn_v_desc last_place =
          handle_locks(names, basic.task_pt_chain, p_task.lock, p_task.time,
                       p_task.priority, p_task.core);
      add_edge(last_place, basic.exec, graph);
    }
    else
    {
      add_edge(basic.ready, basic.exec, graph);
    }

    // 完成任务链
    basic.task_pt_chain.push_back(basic.exec);
    basic.task_pt_chain.push_back(basic.exit);

    // 记录任务映射
    vector<vector<ptpn_v_desc>> task_pt_chains{basic.task_pt_chain};
    node_pn_map.insert({p_task.name, basic.task_pt_chain});
    task_pn_map.insert({p_task.name, task_pt_chains});

    return {basic.entry, basic.exit};
  }

  pair<ptpn_v_desc, ptpn_v_desc>
  PriorityTPN::add_ap_node_ptpn(APeriodicTask &ap_task)
  {
    ptpn::TaskVertexsNames names(ap_task.name);

    // 创建基本任务结构
    BasicTaskChains basic(graph, names, ap_task.priority, ap_task.core,
                          ap_task.time.back());

    // 处理锁资源
    if (!ap_task.lock.empty())
    {
      ptpn_v_desc last_place =
          handle_locks(names, basic.task_pt_chain, ap_task.lock, ap_task.time,
                       ap_task.priority, ap_task.core);
      add_edge(last_place, basic.exec, graph);
    }
    else
    {
      add_edge(basic.ready, basic.exec, graph);
    }

    // 完成任务链
    basic.task_pt_chain.push_back(basic.exec);
    basic.task_pt_chain.push_back(basic.exit);

    // 记录任务映射
    vector<vector<ptpn_v_desc>> task_pt_chains{basic.task_pt_chain};
    node_pn_map.insert({ap_task.name, basic.task_pt_chain});
    task_pn_map.insert({ap_task.name, task_pt_chains});

    return {basic.entry, basic.exit};
  }
  /// 增加看门狗子网结构,只链接每个周期任务的开始和结束节点
  /// 检测每个路径(包括抢占路径)的任务执行流
  void PriorityTPN::add_monitor_ptpn(const string &task_name,
                                     int task_period_time, ptpn_v_desc start,
                                     ptpn_v_desc end)
  {
    // namespace
    string place_deadline = task_name + "deadline";
    string place_timeout = task_name + "timeout";
    string place_ok = task_name + "ok";
    string place_t_end = task_name + "end";

    string transition_t_ending = task_name + "ending";
    string transition_timed = task_name + "timed";
    string transition_t_ok = task_name + "complete";
    string transition_t_out = task_name + "out";

    auto period_time = make_pair(task_period_time, task_period_time);
    ptpn_v_desc task_timed =
        add_transition(graph, transition_timed, 255, 255, period_time);
    ptpn_v_desc task_deadline = add_place(graph, place_deadline, 0, 1);
    // 下面两个变迁在255 处理器上, ok的优先级高于 out, 以表示到达周期后优先触发 ok

    ptpn_v_desc task_complete =
        add_transition(graph, transition_t_ok, 255, 255, {0, 0});
    ptpn_v_desc task_tout =
        add_transition(graph, transition_t_out, 255, 255, {0, 0});

    ptpn_v_desc task_tend =
        add_transition(graph, transition_t_ending, 255, 255, {0, 0});
    ptpn_v_desc task_t_end = add_place(graph, place_t_end, 0, 1);
    ptpn_v_desc task_ok = add_place(graph, place_ok, 0, 1);
    ptpn_v_desc task_timeout = add_place(graph, place_timeout, 0, 1);

    add_edge(end, task_tend, graph);
    add_edge(task_tend, task_t_end, graph);
    add_edge(task_t_end, task_complete, graph);
    add_edge(task_complete, task_ok, graph);
    add_edge(task_deadline, task_ok, graph);

    add_edge(task_deadline, task_tout, graph);
    add_edge(task_tout, task_timeout, graph);
    add_edge(start, task_timed, graph);
    add_edge(task_timed, task_deadline, graph);
  }

  /// \brief
  /// 为每个任务添加抢占的执行序列,根据任务的分配的处理器资源,对每个处理器上的任务从低优先级开始
  /// (t1, t2,
  ///  t3,...) t2 抢占 t1, t3 抢占 t1 和 t2,以此类推
  /// \param core_task 分配到同一处理器的任务名称数组
  /// \param tc 任务属性映射
  /// \param nodes_type 任务类型映射
  void PriorityTPN::add_preempt_task_ptpn(
      const std::unordered_map<int, vector<string>> &core_task,
      const std::unordered_map<string, TaskConfig> &tc,
      const std::unordered_map<string, NodeType> &nodes_type)
  {

    // 处理单个任务的抢占
    auto handle_task_preemption =
        [&](const string &l_t_name, const string &h_t_name,
            const TaskConfig &l_tc, const TaskConfig &h_tc,
            const vector<ptpn_v_desc> &l_t_pn, const vector<ptpn_v_desc> &h_t_pn,
            const bool is_interrupt)
    {
      const int task_pn_size = static_cast<int>(l_t_pn.size());
      if (graph[l_t_pn[task_pn_size - 2]].is_transition())
      {
        graph[l_t_pn[task_pn_size - 2]].as_transition().handle = true;
      }

      if (is_interrupt)
      {
        create_task_priority(h_t_name, l_t_pn[task_pn_size - 3],
                             l_t_pn[task_pn_size - 2], l_t_pn[0],
                             l_t_pn.back(), nodes_type.at(h_t_name));
      }
      else
      {
        create_hlf_task_priority(
            h_t_name, l_t_pn[task_pn_size - 3], l_t_pn[task_pn_size - 2],
            h_t_pn[0], l_t_pn[0], h_t_pn[2], h_tc.priority, h_tc.core);
      }

      // 处理锁
      if (!l_tc.locks.empty())
      {
        constexpr size_t MIN_CHAIN_LENGTH = 9;
        // 检查链长度是否满足最小要求
        if (l_t_pn.size() < MIN_CHAIN_LENGTH)
        {
          BOOST_LOG_TRIVIAL(debug) << "[PTPN] Skip chain for " << l_t_name << ": too short for locks";
          return;
        }
        for (size_t i = 0; i < l_tc.locks.size(); i++)
        {
          if (l_tc.locks[i].find("spin") != string::npos)
            break;

          size_t idx = task_pn_size - 2 - 2 * (i + 1);
          graph[l_t_pn[idx]].as_transition().handle = true;

          if (is_interrupt)
          {
            create_task_priority(h_t_name, l_t_pn[idx - 1], l_t_pn[idx],
                                 l_t_pn[0], l_t_pn.back(),
                                 nodes_type.at(h_t_name));
          }
          else
          {
            create_hlf_task_priority(h_t_name, l_t_pn[idx - 1], l_t_pn[idx],
                                     h_t_pn[0], l_t_pn[0], h_t_pn[2],
                                     h_tc.priority, h_tc.core);
          }
        }
      }
    };

    // 主循环
    for (const auto &[core_id, tasks] : core_task)
    {
      BOOST_LOG_TRIVIAL(debug) << "[PTPN] Processing core: " << core_id;

      for (auto l_t = tasks.begin(); l_t != tasks.end() - 1; l_t++)
      {
        const string &l_t_name = *l_t;
        const TaskConfig &l_tc = tc.at(l_t_name);

        for (auto h_t = l_t + 1; h_t != tasks.end(); h_t++)
        {
          const string &h_t_name = *h_t;
          const TaskConfig &h_tc = tc.at(h_t_name);

          if (l_tc.priority == h_tc.priority)
            continue;

          auto l_t_pns = task_pn_map.find(l_t_name);
          auto h_t_pns = task_pn_map.find(h_t_name);
          if (l_t_pns == task_pn_map.end() || h_t_pns == task_pn_map.end())
            continue;

          const auto &h_t_pn = h_t_pns->second[0];
          bool is_interrupt = false;

          if (auto node_type = nodes_type.find(h_t_name);
              node_type != nodes_type.end() &&
              holds_alternative<PeriodicTask>(node_type->second))
          {
            auto p_task = get<PeriodicTask>(node_type->second);
            is_interrupt = (p_task.task_type == TaskType::INTERRUPT);
          }

          for (const auto &l_t_pn : l_t_pns->second)
          {
            handle_task_preemption(l_t_name, h_t_name, l_tc, h_tc, l_t_pn, h_t_pn,
                                   is_interrupt);
          }

            BOOST_LOG_TRIVIAL(debug) << "[PTPN] Priority " << l_t_name << " : " << l_tc.priority << " " << h_t_name;
        }
      }
    }
  }

  /// \brief 根据软件学报论文创建抢占变迁
  /// \param name 高优先级任务的名字
  /// \param preempt_vertex 发生抢占的库所
  /// \param handle_t 可挂起的变迁
  /// \param start 高优先级的开始库所
  /// \param end 高优先级的结束库所
  /// \param task_type 高优先级任务类型
  void PriorityTPN::create_task_priority(const std::string &name,
                                         ptpn_v_desc preempt_vertex,
                                         size_t handle_t, ptpn_v_desc start,
                                         ptpn_v_desc end, NodeType task_type)
  {

    // 设置处理变迁标志
    graph[handle_t].as_transition().handle = true;

    // 名字被占用,加index区分
    struct TaskNodeNames
    {
      string get_core, ready, get_lock, deal, drop_lock, unlock, exec;

      explicit TaskNodeNames(const string &base_name, int index)
      {
        get_core = base_name + "get_core" + to_string(index);
        ready = base_name + "ready" + to_string(index);
        get_lock = base_name + "get_lock" + to_string(index);
        deal = base_name + "deal" + to_string(index);
        drop_lock = base_name + "drop_lock" + to_string(index);
        unlock = base_name + "unlocked" + to_string(index);
        exec = base_name + "exec" + to_string(index);
      }
    };

    TaskNodeNames node_names(name, node_index);
    std::vector<ptpn_v_desc> node{start};

    // 创建基本任务结构的辅助函数
    auto create_basic_structure = [&](int priority, int core,
                                      const std::pair<int, int> &exec_time)
    {
      // 创建获取CPU和就绪节点
      ptpn_v_desc get_core =
          add_transition(graph, node_names.get_core, priority, core, {0, 0});
      ptpn_v_desc ready = add_place(graph, node_names.ready, 0);
      ptpn_v_desc exec =
          add_transition(graph, node_names.exec, priority, core, exec_time);

      // 添加基本边
      add_edge(start, get_core, graph);
      add_edge(preempt_vertex, get_core, graph);
      add_edge(get_core, ready, graph);
      add_edge(exec, end, graph);
      add_edge(exec, preempt_vertex, graph);

      // 更新节点序列
      node.push_back(start);
      node.push_back(get_core);
      node.push_back(ready);

      return std::make_tuple(ready, exec);
    };

    // 处理锁相关结构的辅助函数
    auto handle_locks = [&](const vector<string> &locks,
                            const vector<pair<int, int>> &times, int core)
    {
      if (locks.empty())
      {
        return;
      }
      // 添加获取锁的结构
      for (const auto & lock : locks)
      {
        if (lock.find("spin") != string::npos)
          break;
        std::string gl = node_names.get_lock + lock + to_string(node_index);
        ptpn_v_desc get_lock = add_transition(graph, gl, 256, core, {0, 0});
        ptpn_v_desc deal = add_place(graph, node_names.deal + lock, 0, 1);

        add_edge(node.back(), get_lock, graph);
        node.push_back(get_lock);
        node.push_back(deal);
        add_edge(get_lock, deal, graph);
      }

      // 添加释放锁的结构
      for (int k = static_cast<int>(locks.size() - 1); k >= 0; k--)
      {
        std::string dl = node_names.drop_lock + locks[k] + to_string(node_index);
        ptpn_v_desc drop_lock = add_transition(graph, dl, 256, core, times[k]);
        ptpn_v_desc unlocked = add_place(graph, node_names.unlock + locks[k], 0, 1);

        add_edge(node.back(), drop_lock, graph);
        add_edge(drop_lock, unlocked, graph);

        if (locks.size() == 1 || k == 0)
        {
          add_edge(unlocked, node.back(), graph);
        }

        node.push_back(drop_lock);
        node.push_back(unlocked);
      }
    };

    // 处理不同类型的任务
    if (holds_alternative<APeriodicTask>(task_type))
    {
      auto task = get<APeriodicTask>(task_type);
      auto [ready, exec] =
          create_basic_structure(task.priority, task.core, task.time.back());

      if (task.lock.empty())
      {
        add_edge(ready, exec, graph);
      }
      else
      {
        handle_locks(task.lock, task.time, task.core);
        add_edge(node.back(), exec, graph);
      }

      node.push_back(exec);
      node.push_back(end);
      task_pn_map.find(task.name)->second.push_back(node);
    }
    else if (holds_alternative<PeriodicTask>(task_type))
    {
      auto task = get<PeriodicTask>(task_type);
      auto [ready, exec] =
          create_basic_structure(task.priority, task.core, task.time.back());

      if (task.lock.empty())
      {
        add_edge(ready, exec, graph);
      }
      else
      {
        handle_locks(task.lock, task.time, task.core);
        add_edge(node.back(), exec, graph);
      }

      node.push_back(exec);
      node.push_back(end);
      task_pn_map.find(task.name)->second.push_back(node);
    }
    else
    {
      BOOST_LOG_TRIVIAL(error) << "[PTPN] unreachable!";
      return;
    }

    node_index++;
  }

  /// \brief 根据 ACM 论文创建的抢占变迁
  /// \param name 高优先级任务的名字
  /// \param preempt_vertex 发生抢占的库所
  /// \param handle_t 可挂起的变迁
  /// \param h_start 高优先级任务的开始库所
  /// \param l_start 低优先级任务的开始库所
  /// \param h_ready 高优先级任务获得处理器资源后的库所
  /// \param task_priority 高优先级任务的优先级
  /// \param task_core 高优先级任务的处理器资源
  void PriorityTPN::create_hlf_task_priority(const std::string &name,
                                             ptpn_v_desc preempt_vertex,
                                             size_t handle_t, ptpn_v_desc h_start,
                                             ptpn_v_desc l_start,
                                             ptpn_v_desc h_ready,
                                             int task_priority, int task_core)
  {
    graph[handle_t].as_transition().handle = true;
    string task_get = name + "get_core" + std::to_string(node_index);

    ptpn_v_desc get_core = add_transition(graph, task_get, task_priority,
                                          task_core, std::make_pair(0, 0));

    add_edge(h_start, get_core, graph);
    add_edge(preempt_vertex, get_core, graph);
    add_edge(get_core, h_ready, graph);
    add_edge(get_core, l_start, graph);

    node_index += 1;
  }

  bool PriorityTPN::verify_petri_net_structure()
  {
    bool is_valid = true;

    // 遍历所有顶点
    for (auto [vi, vi_end] = vertices(graph); vi != vi_end; ++vi)
    {
      const auto &vertex = graph[*vi];

      if (vertex.shape == "box")
      { // 变迁
        // 检查1:变迁的前后节点必须是库所且不能为空
        bool has_input = false;
        bool has_output = false;
        bool all_inputs_are_places = true;
        bool all_outputs_are_places = true;

        // 检查入边
        for (auto [ei, ei_end] = in_edges(*vi, graph); ei != ei_end; ++ei)
        {
          has_input = true;
          ptpn_v_desc pre = source(*ei, graph);
          if (graph[pre].shape != "circle")
          {
            all_inputs_are_places = false;
            BOOST_LOG_TRIVIAL(error) << "[PTPN] 变迁 " << vertex.name << " 的前置节点 " << graph[pre].name << " 不是库所";
          }
        }

        // 检查出边
        for (auto [ei, ei_end] = out_edges(*vi, graph); ei != ei_end; ++ei)
        {
          has_output = true;
          ptpn_v_desc suc = target(*ei, graph);
          if (graph[suc].shape != "circle")
          {
            all_outputs_are_places = false;
            BOOST_LOG_TRIVIAL(error) << "[PTPN] 变迁 " << vertex.name << " 的后继节点 " << graph[suc].name << " 不是库所";
          }
        }

        if (!has_input || !has_output)
        {
          is_valid = false;
          BOOST_LOG_TRIVIAL(error) << "[PTPN] 变迁 " << vertex.name << " 的前置或后继节点为空";
        }

        if (!all_inputs_are_places || !all_outputs_are_places)
        {
          is_valid = false;
        }

        // 检查4:变迁时间约束的有效性
        if (vertex.is_transition())
        {
          auto transition = vertex.as_transition();
          if (transition.const_time.first < 0 ||
              transition.const_time.second < transition.const_time.first)
          {
            is_valid = false;
            BOOST_LOG_TRIVIAL(error) << "[PTPN] 变迁 " << vertex.name << " 的时间约束无效: [" << transition.const_time.first << ", " << transition.const_time.second << "]";
          }
        }
      }
      else if (vertex.shape == "circle")
      { // 库所
        auto place = vertex.as_place();
        // 检查2:库所的前后节点必须是变迁（但可以为空）
        bool all_inputs_are_transitions = true;
        bool all_outputs_are_transitions = true;

        // 检查入边
        for (auto [ei, ei_end] = in_edges(*vi, graph); ei != ei_end; ++ei)
        {
          ptpn_v_desc pre = source(*ei, graph);
          if (graph[pre].shape != "box")
          {
            all_inputs_are_transitions = false;
            BOOST_LOG_TRIVIAL(error) << "[PTPN] 库所 " << vertex.name << " 的前置节点 " << graph[pre].name << " 不是变迁";
          }
        }

        // 检查出边
        for (auto [ei, ei_end] = out_edges(*vi, graph); ei != ei_end; ++ei)
        {
          ptpn_v_desc suc = target(*ei, graph);
          if (graph[suc].shape != "box")
          {
            all_outputs_are_transitions = false;
            BOOST_LOG_TRIVIAL(error) << "[PTPN] 库所 " << vertex.name << " 的后继节点 " << graph[suc].name << " 不是变迁";
          }
        }

        if (!all_inputs_are_transitions || !all_outputs_are_transitions)
        {
          is_valid = false;
        }

        // 检查3:token数量必须是0或1
        if (place.token < 0 || place.token > 1)
        {
          is_valid = false;
          BOOST_LOG_TRIVIAL(error) << "[PTPN] 库所 " << vertex.name << " 的token数量无效: " << place.token;
        }
      }
    }

    if (is_valid)
    {
      BOOST_LOG_TRIVIAL(info) << "[PTPN] Petri网结构验证通过";
    }
    else
    {
      BOOST_LOG_TRIVIAL(error) << "[PTPN] Petri网结构验证失败";
    }

    return is_valid;
  }

  void PriorityTPN::import_ptpn_from_dot(const std::string &file_path)
  {
    try
    {
          BOOST_LOG_TRIVIAL(info) << "[PTPN] 开始从DOT文件导入Petri网: " << file_path;
      graph.clear();

      std::ifstream dot_file(file_path);
      if (!dot_file)
      {
        BOOST_LOG_TRIVIAL(error) << "[PTPN] 无法打开DOT文件: " << file_path;
        return;
      }

      PriorityTPNGraph dot_graph;
      boost::dynamic_properties dp;
      dp.property("node_id", boost::get(&Vertex::name, dot_graph));
      dp.property("label", boost::get(&Vertex::label, dot_graph));
      dp.property("shape", boost::get(&Vertex::shape, dot_graph));
      dp.property("xlabel", boost::get(&Edge::label, dot_graph));

      // 读取DOT文件到临时图
      if (!boost::read_graphviz(dot_file, dot_graph, dp))
      {
        BOOST_LOG_TRIVIAL(error) << "[PTPN] 无法解析DOT文件: " << file_path;
        return;
      }

      // 解析节点
      std::map<std::string, ptpn_v_desc> name_to_vertex;

      for (auto [vi, vi_end] = boost::vertices(dot_graph); vi != vi_end; ++vi)
      {
        std::string node_name = dot_graph[*vi].name;
        std::string node_label = dot_graph[*vi].label;

        // 检查节点形状（通过label中的shape属性） TODO: 需要优化
        bool is_place = false;
        bool is_transition = false;

        // 解析label属性
        if (node_label.find("shape=circle") != std::string::npos ||
            node_label.find("shape=\"circle\"") != std::string::npos)
        {
          is_place = true;
        }
        else if (node_label.find("shape=box") != std::string::npos ||
                 node_label.find("shape=\"box\"") != std::string::npos)
        {
          is_transition = true;
        }

        // 如果没有明确的shape,尝试从label内容推断
        if (!is_place && !is_transition)
        {
          if (node_label.find("token=") != std::string::npos)
          {
            is_place = true;
          }
          else if (node_label.find("time=") != std::string::npos ||
                   node_label.find("priority=") != std::string::npos)
          {
            is_transition = true;
          }
        }

        ptpn_v_desc vertex;

        if (is_place)
        {
          // 解析库所属性
          int token = 0;
          int capacity = 1;

          // 解析token和capacity
          size_t token_pos = node_label.find("token=");
          if (token_pos != std::string::npos)
          {
            size_t start = token_pos + 6;
            size_t end = node_label.find(';', start);
            if (end == std::string::npos)
              end = node_label.find('"', start);
            if (end == std::string::npos)
              end = node_label.length();
            token = std::stoi(node_label.substr(start, end - start));
          }

          size_t capacity_pos = node_label.find("capacity=");
          if (capacity_pos != std::string::npos)
          {
            size_t start = capacity_pos + 9;
            size_t end = node_label.find(';', start);
            if (end == std::string::npos)
              end = node_label.find('"', start);
            if (end == std::string::npos)
              end = node_label.length();
            capacity = std::stoi(node_label.substr(start, end - start));
          }

          vertex = add_place(graph, node_name, token, capacity);
            BOOST_LOG_TRIVIAL(debug) << "[PTPN] 添加库所: " << node_name << " (token=" << token << ", capacity=" << capacity << ")";
        }
        else if (is_transition)
        {
          // 解析变迁属性
          int priority = 255;
          int core = 255;
          std::pair<int, int> const_time = {0, 0};
          std::pair<int, int> runtimes = {0, 0};
          bool is_handle = false;

          // 解析time属性
          size_t time_pos = node_label.find("time=");
          if (time_pos != std::string::npos)
          {
            size_t start = time_pos + 5;
            size_t end = node_label.find(';', start);
            if (end == std::string::npos)
              end = node_label.find('"', start);
            if (end == std::string::npos)
              end = node_label.length();

            std::string time_str = node_label.substr(start, end - start);
            // 解析[1,3]格式
            if (time_str.find('[') != std::string::npos && time_str.find(']') != std::string::npos)
            {
              size_t lb_start = time_str.find('[') + 1;
              size_t lb_end = time_str.find(',', lb_start);
              size_t ub_start = lb_end + 1;
              size_t ub_end = time_str.find(']', ub_start);

              if (lb_end != std::string::npos && ub_end != std::string::npos)
              {
                const_time.first = std::stoi(time_str.substr(lb_start, lb_end - lb_start));
                const_time.second = std::stoi(time_str.substr(ub_start, ub_end - ub_start));
              }
            }
          }

          // 解析priority属性
          size_t priority_pos = node_label.find("priority=");
          if (priority_pos != std::string::npos)
          {
            size_t start = priority_pos + 9;
            size_t end = node_label.find(';', start);
            if (end == std::string::npos)
              end = node_label.find('"', start);
            if (end == std::string::npos)
              end = node_label.length();
            priority = std::stoi(node_label.substr(start, end - start));
          }

          // 解析core属性
          size_t core_pos = node_label.find("core=");
          if (core_pos != std::string::npos)
          {
            size_t start = core_pos + 5;
            size_t end = node_label.find(';', start);
            if (end == std::string::npos)
              end = node_label.find('"', start);
            if (end == std::string::npos)
              end = node_label.length();
            core = std::stoi(node_label.substr(start, end - start));
          }

          vertex = add_transition(graph, node_name, priority, core, const_time, is_handle, runtimes);
          BOOST_LOG_TRIVIAL(debug) << "[PTPN] 添加变迁: " << node_name << " (priority=" << priority << ", core=" << core << ", time=[" << const_time.first << "," << const_time.second << "])";
                         
        }
        else
        {
          BOOST_LOG_TRIVIAL(warning) << "[PTPN] 无法确定节点类型: " << node_name;
          continue;
        }

        name_to_vertex[node_name] = vertex;
      }

      // 解析边
      for (auto [ei, ei_end] = boost::edges(dot_graph); ei != ei_end; ++ei)
      {
        std::string source_name = dot_graph[boost::source(*ei, dot_graph)].name;
        std::string target_name = dot_graph[boost::target(*ei, dot_graph)].name;

        auto source_it = name_to_vertex.find(source_name);
        auto target_it = name_to_vertex.find(target_name);

        if (source_it != name_to_vertex.end() && target_it != name_to_vertex.end())
        {
          // 解析边权重
          int weight = 1;
          std::string edge_label = dot_graph[*ei].label;

          size_t weight_pos = edge_label.find("weight=");
          if (weight_pos != std::string::npos)
          {
            size_t start = weight_pos + 7;
            size_t end = edge_label.find(';', start);
            if (end == std::string::npos)
              end = edge_label.find('"', start);
            if (end == std::string::npos)
              end = edge_label.length();
            weight = std::stoi(edge_label.substr(start, end - start));
          }

          // 添加边
          auto edge = boost::add_edge(source_it->second, target_it->second, graph).first;
          graph[edge].weight = weight;
          graph[edge].label = edge_label;

          BOOST_LOG_TRIVIAL(debug) << "[PTPN] 添加边: " << source_name << " -> " << target_name << " (weight=" << weight << ")";
        }
        else
        {
          BOOST_LOG_TRIVIAL(warning) << "[PTPN] 边的端点不存在: " << source_name << " -> " << target_name;
        }
      }

      BOOST_LOG_TRIVIAL(info) << "[PTPN] 成功从DOT文件导入Petri网,共 " << boost::num_vertices(graph) << " 个节点," << boost::num_edges(graph) << " 条边";
                    
    }
    catch (const std::exception &e)
    {
      BOOST_LOG_TRIVIAL(error) << "[PTPN] 导入DOT文件时发生错误: " << e.what();
    }
  }

  void PriorityTPN::import_ptpn_from_json(const std::string &file_path)
  {
    std::ifstream file(file_path);
    std::string line;
    while (std::getline(file, line))
    {
      std::cout << line << std::endl;
    }
  }

} // namespace ptpn
