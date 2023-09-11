//
// Created by 张凯文 on 2023/6/26.
//

#include "probpetrinet.h"
#include <queue>

vertex_ptpn add_place(PTPN &g, const std::string &name, int token)
{
  PTPNVertex element = {name, token};
  return boost::add_vertex(element, g);
}

vertex_ptpn add_transition(PTPN &g, std::string &name, PTPNTransition pnt)
{
  PTPNVertex element = {name, pnt};
  return boost::add_vertex(element, g);
}

void ProbPetriNet::init()
{
  ptpn_dp.property("node_id", get(&PTPNVertex::name, ptpn));
  ptpn_dp.property("label", get(&PTPNVertex::label, ptpn));
  ptpn_dp.property("shape", get(&PTPNVertex::shape, ptpn));
  ptpn_dp.property("label", get(&PTPNEdge::label, ptpn));

  boost::ref_property_map<PTPN *, std::string> gname_pn(
      get_property(ptpn, boost::graph_name));
  ptpn_dp.property("name", gname_pn);
}

void ProbPetriNet::collect_task(GConfig &config) { config.prase_dag(); }

void ProbPetriNet::create_core_vertex(int num)
{
  for (int i = 0; i < num; i++)
  {
    std::string core_name = "core" + std::to_string(i);
    // vertex_tpn c = add_vertex(PetriNetElement{core_name, core_name, "circle",
    // 1}, time_petri_net);
    vertex_ptpn c = add_place(ptpn, core_name, 1);
    core_vertex.push_back(c);
  }
  BOOST_LOG_TRIVIAL(info) << "create core resource!";
}

void ProbPetriNet::create_lock_vertex(const GConfig &config)
{
  if (config.lock_set.empty())
  {
    return;
  }
  for (const auto &lock : config.lock_set)
  {
    vertex_ptpn v = add_place(ptpn, lock, 1);
    lock_vertex.insert(std::make_pair(lock, v));
  }
  BOOST_LOG_TRIVIAL(info) << "create lock resource!";
}

void ProbPetriNet::construct_sub_ptpn(GConfig &config)
{
  collect_task(config);
  BOOST_LOG_TRIVIAL(info) << "collect all task";
  pt_a = boost::chrono::steady_clock::now();
  Agraph_t *subgraph;
  // 对于每一个subgraph分别构建子网
  // 通过glb的task与subgraph的start,end任务链接
  for (subgraph = agfstsubg(config.graph); subgraph;
       subgraph = agnxtsubg(subgraph))
  {
    std::string current_sub = agnameof(subgraph);
    auto sub_tasks = config.sub_task_set.find(current_sub)->second;
    auto sub_core = config.sub_graph_config.find(current_sub)->second.core;
    for (auto &sub_t : sub_tasks)
    {
      if (sub_t.find("Wait") != std::string::npos)
      {
        std::string wait0 = "wait" + std::to_string(element_id);
        element_id += 1;
        PTPNTransition pt = {256, std::make_pair(0, 0), 256};
        vertex_ptpn wait_v_s = add_transition(ptpn, wait0, pt);
        std::vector<vertex_ptpn> task_v;
        task_v.push_back(wait_v_s);
        task_vertexes.insert(std::make_pair(sub_t, task_v));
        BOOST_LOG_TRIVIAL(debug) << "find wait vertex!";
        continue;
      }
      else if (sub_t.find("Distribute") != std::string::npos)
      {
        std::string dist = "distribute" + std::to_string(element_id);
        element_id += 1;
        PTPNTransition pt = {256, std::make_pair(0, 0), 256};
        vertex_ptpn dist_v_s = add_transition(ptpn, dist, pt);
        std::vector<vertex_ptpn> task_v;
        task_v.push_back(dist_v_s);
        task_vertexes.insert(std::make_pair(sub_t, task_v));
        BOOST_LOG_TRIVIAL(debug) << "find distribute vertex!";
        continue;
      }
      else
      {
        std::vector<vertex_ptpn> t_node;
        std::vector<std::vector<vertex_ptpn>> t_nodes;
        std::string task_entry = sub_t + "entry";
        std::string task_lock = sub_t + "get_lock";
        std::string task_deal = sub_t + "deal";
        std::string task_drop = sub_t + "drop_lock";
        std::string task_unlock = sub_t + "unlocked";
        std::string task_exec = sub_t + "exec";
        std::string task_exit = sub_t + "exit";
        SubTaskConfig stc = config.tasks_label.find(sub_t)->second;
        int task_priority = stc.priority;
        auto task_exec_t = stc.time[stc.time.size() - 1];
        vertex_ptpn entry = add_place(ptpn, task_entry, 0);
        vertex_ptpn exec = add_transition(
            ptpn, task_exec,
            PTPNTransition{task_priority, task_exec_t, sub_core});
        vertex_ptpn exit = add_place(ptpn, task_exit, 0);
        add_edge(exec, exit, ptpn);
        t_node.push_back(entry);
        if (stc.lock.empty())
        {
          add_edge(entry, exec, ptpn);
        }
        else
        {
          size_t lock_size = stc.lock.size();
          for (int i = 0; i < lock_size; i++)
          {
            auto lock_name = stc.lock[i];
            auto lock_get = task_lock + lock_name;
            vertex_ptpn get_lock =
                add_transition(ptpn, lock_get,
                               PTPNTransition{task_priority, {0, 0}, sub_core});
            vertex_ptpn deal = add_place(ptpn, task_deal + lock_name, 0);
            add_edge(t_node.back(), get_lock, ptpn);
            t_node.push_back(get_lock);
            t_node.push_back(deal);
            add_edge(get_lock, deal, ptpn);
          }
          auto link = t_node.back();

          for (int j = int(lock_size - 1); j >= 0; j--)
          {
            auto lock_name = stc.lock[j];
            auto t = stc.time[j];
            std::string lock_drop = task_drop + lock_name;
            vertex_ptpn drop_lock = add_transition(
                ptpn, lock_drop, PTPNTransition{task_priority, t, sub_core});
            vertex_ptpn unlocked = add_place(ptpn, task_unlock + lock_name, 0);
            add_edge(t_node.back(), drop_lock, ptpn);
            add_edge(drop_lock, unlocked, ptpn);
            if (lock_size == 1)
            {
              // add_edge(link, drop_lock, time_petri_net);
              add_edge(unlocked, exec, ptpn);
            }
            else if (j == 0)
            {
              add_edge(unlocked, exec, ptpn);
            }
            t_node.push_back(drop_lock);
            t_node.push_back(unlocked);
          }
        }
        t_node.push_back(exec);
        t_node.push_back(exit);
        t_nodes.push_back(t_node);
        task_vertexes.insert(std::make_pair(sub_t, t_node));
        preempt_task_vertexes.insert(std::make_pair(sub_t, t_nodes));
      }
    }
    BOOST_LOG_TRIVIAL(info) << "DEAL DAG EDGE";
    // Traverse the edges in the subgraph
    Agnode_t *node;
    Agedge_t *edge;
    for (node = agfstnode(subgraph); node; node = agnxtnode(subgraph, node))
    {
      for (edge = agfstout(subgraph, node); edge;
           edge = agnxtout(subgraph, edge))
      {
        Agnode_t *targetNode = aghead(edge);
        std::string source_name = agnameof(node);
        std::string target_name = agnameof(targetNode);
        vertex_ptpn source = task_vertexes.find(source_name)->second.back();
        vertex_ptpn target = task_vertexes.find(target_name)->second.front();

        if ((source_name.find("Wait") != std::string::npos) ||
            (source_name.find("Distribute") != std::string::npos))
        {
          add_edge(source, target, ptpn);
        }
        else if ((target_name.find("Wait") != std::string::npos) ||
                 (target_name.find("Distribute") != std::string::npos))
        {
          add_edge(source, target, ptpn);
        }
        else
        {
          std::string link_name = "link" + std::to_string(element_id);
          element_id += 1;
          vertex_ptpn link = add_transition(
              ptpn, link_name, PTPNTransition{256, std::make_pair(0, 0), 256});
          add_edge(source, link, ptpn);
          add_edge(link, target, ptpn);
        }
      }
    }

    BOOST_LOG_TRIVIAL(info) << "construct extern vertex";
    // 绑定subgraph的开始和结束任务
    std::string subgraph_entry = current_sub + "entry";
    std::string subgraph_get_core = current_sub + "get";
    std::string subgraph_time = current_sub + "time";
    std::string subgraph_timed = current_sub + "timed";
    std::string subgraph_deadline = current_sub + "deadline";
    std::string subgraph_no_end = current_sub + "timeout";
    std::string subgraph_t_end = current_sub + "restart";

    std::string subgraph_end = current_sub + "end";
    std::string subgraph_drop_core = current_sub + "drop";
    std::string subgraph_exit = current_sub + "exit";

    std::vector<vertex_ptpn> sub_node;
    std::vector<std::vector<vertex_ptpn>> sub_nodes;
    vertex_ptpn sub_entry = add_place(ptpn, subgraph_entry, 0);
    vertex_ptpn sub_get =
        add_transition(ptpn, subgraph_get_core,
                       PTPNTransition{256, std::make_pair(0, 0), sub_core});
    add_edge(sub_entry, sub_get, ptpn);
    sub_node.push_back(sub_entry);
    sub_node.push_back(sub_get);
    vertex_ptpn sub_drop =
        add_transition(ptpn, subgraph_drop_core,
                       PTPNTransition{256, std::make_pair(0, 0), sub_core});
    vertex_ptpn sub_exit = add_place(ptpn, subgraph_exit, 0);
    add_edge(sub_drop, sub_exit, ptpn);

    auto sub_start_t = config.sub_graph_start_end[current_sub].first;
    auto sub_end_t = config.sub_graph_start_end[current_sub].second;
    vertex_ptpn task_start_v = task_vertexes[sub_start_t].front();
    vertex_ptpn task_end_v = task_vertexes[sub_end_t].back();
    add_edge(sub_get, task_start_v, ptpn);
    add_edge(task_end_v, sub_drop, ptpn);

    if (config.sub_graph_config[current_sub].is_period)
    {
      auto sub_t = config.sub_graph_config[current_sub].period;
      std::vector<vertex_ptpn> sub_timed_v;
      vertex_ptpn sub_time = add_place(ptpn, subgraph_time, 0);
      sub_timed_v.push_back(sub_time);
      vertex_ptpn sub_timed = add_transition(ptpn, subgraph_timed,
                                             PTPNTransition{255, std::make_pair(sub_t, sub_t), sub_core});
      vertex_ptpn sub_deadline = add_place(ptpn, subgraph_deadline, 0);
      vertex_ptpn sub_no_end = add_transition(
          ptpn, subgraph_no_end,
          PTPNTransition{255, std::make_pair(0, 0), sub_core});
      vertex_ptpn sub_t_end = add_transition(
          ptpn, subgraph_t_end,
          PTPNTransition{256, std::make_pair(0, 0), sub_core});
      vertex_ptpn sub_end = add_place(ptpn, subgraph_end, 0);
      // add_edge(sub_get, sub_time, ptpn);
      add_edge(sub_time, sub_timed, ptpn);
      add_edge(sub_timed, sub_deadline, ptpn);
      add_edge(sub_deadline, sub_t_end, ptpn);
      add_edge(sub_deadline, sub_no_end, ptpn);
      add_edge(sub_t_end, sub_entry, ptpn);
      add_edge(sub_t_end, sub_time, ptpn);
      add_edge(sub_end, sub_t_end, ptpn);
      add_edge(sub_no_end, sub_entry, ptpn);
      add_edge(sub_no_end, sub_time, ptpn);
      add_edge(sub_drop, sub_end, ptpn);

      sub_node.push_back(sub_time);
      sub_node.push_back(sub_no_end);
      sub_node.push_back(sub_t_end);
      sub_node.push_back(sub_end);
      sub_task_timer.insert(std::make_pair(current_sub, sub_timed_v));
    }
    sub_node.push_back(sub_drop);
    sub_node.push_back(sub_exit);
    task_vertexes.insert(std::make_pair(current_sub, sub_node));
  }

  BOOST_LOG_TRIVIAL(info) << "Sub Task Petri Net Construction Completed!";
  construct_glb_ptpn(config);
  std::ofstream os;
  os.open("t.dot");
  write_graphviz_dp(os, ptpn, ptpn_dp);
}
void ProbPetriNet::construct_glb_ptpn(GConfig &config)
{
  int inSubgraph = 0;
  Agraph_t *subgraph;
  // Traverse the nodes in the main graph
  Agnode_t *node;
  std::vector<Agnode_t *> glb_task;
  for (node = agfstnode(config.graph); node;
       node = agnxtnode(config.graph, node))
  {
    inSubgraph = 0;
    for (subgraph = agfstsubg(config.graph); subgraph;
         subgraph = agnxtsubg(subgraph))
    {
      if (agsubnode(subgraph, node, 0))
      {
        inSubgraph = 1;
        break;
      }
    }
    if (!inSubgraph)
    {
      // 为该node任务创建petri net
      glb_task.push_back(node);
      std::string current_t = agnameof(node);
      auto current_config = config.tasks_label[current_t];
      int task_core = current_config.core;
      int task_priority = current_config.priority;
      if (current_t.find("Wait") != std::string::npos)
      {
        std::string wait0 = "wait" + std::to_string(element_id);
        element_id += 1;
        PTPNTransition pt = {256, std::make_pair(0, 0), 256};
        vertex_ptpn wait_v_s = add_transition(ptpn, wait0, pt);
        std::vector<vertex_ptpn> task_v;
        task_v.push_back(wait_v_s);
        task_vertexes.insert(std::make_pair(current_t, task_v));
        BOOST_LOG_TRIVIAL(debug) << "find wait vertex!";
        continue;
      }
      else if (current_t.find("Distribute") != std::string::npos)
      {
        std::string dist = "distribute" + std::to_string(element_id);
        element_id += 1;
        PTPNTransition pt = {256, std::make_pair(0, 0), 256};
        vertex_ptpn dist_v_s = add_transition(ptpn, dist, pt);
        std::vector<vertex_ptpn> task_v;
        task_v.push_back(dist_v_s);
        task_vertexes.insert(std::make_pair(current_t, task_v));
        BOOST_LOG_TRIVIAL(debug) << "find distribute vertex!";
        continue;
      }
      else
      {
        std::vector<vertex_ptpn> t_node;
        std::vector<std::vector<vertex_ptpn>> t_nodes;
        std::string task_entry = current_t + "entry";
        std::string task_get = current_t + "get";
        std::string task_ready = current_t + "ready";
        std::string task_lock = current_t + "get_lock";
        std::string task_deal = current_t + "deal";
        std::string task_drop = current_t + "drop_lock";
        std::string task_unlock = current_t + "unlocked";
        std::string task_exec = current_t + "exec";
        std::string task_exit = current_t + "exit";
        SubTaskConfig stc = config.tasks_label.find(current_t)->second;
        auto task_exec_t = stc.time[stc.time.size() - 1];
        vertex_ptpn entry = add_place(ptpn, task_entry, 0);
        vertex_ptpn get = add_transition(
            ptpn, task_get,
            PTPNTransition{task_priority, std::make_pair(0, 0), task_core});
        vertex_ptpn ready = add_place(ptpn, task_ready, 0);
        add_edge(entry, get, ptpn);
        add_edge(get, ready, ptpn);
        vertex_ptpn exec = add_transition(
            ptpn, task_exec,
            PTPNTransition{task_priority, task_exec_t, task_core});
        vertex_ptpn exit = add_place(ptpn, task_exit, 0);
        add_edge(exec, exit, ptpn);
        t_node.push_back(entry);
        t_node.push_back(get);
        t_node.push_back(ready);
        if (stc.lock.empty())
        {
          add_edge(ready, exec, ptpn);
        }
        else
        {
          size_t lock_size = stc.lock.size();
          for (int i = 0; i < lock_size; i++)
          {
            auto lock_name = stc.lock[i];
            auto lock_get = task_lock + lock_name;
            vertex_ptpn get_lock = add_transition(
                ptpn, lock_get,
                PTPNTransition{task_priority, {0, 0}, task_core});
            vertex_ptpn deal = add_place(ptpn, task_deal + lock_name, 0);
            add_edge(t_node.back(), get_lock, ptpn);
            t_node.push_back(get_lock);
            t_node.push_back(deal);
            add_edge(get_lock, deal, ptpn);
          }
          for (int j = int(lock_size - 1); j >= 0; j--)
          {
            auto lock_name = stc.lock[j];
            auto t = stc.time[j];
            std::string lock_drop = task_drop + lock_name;
            vertex_ptpn drop_lock = add_transition(
                ptpn, lock_drop, PTPNTransition{task_priority, t, task_core});
            vertex_ptpn unlocked = add_place(ptpn, task_unlock + lock_name, 0);
            add_edge(t_node.back(), drop_lock, ptpn);
            add_edge(drop_lock, unlocked, ptpn);
            if (lock_size == 1)
            {
              // add_edge(link, drop_lock, time_petri_net);
              add_edge(unlocked, exec, ptpn);
            }
            else if (j == 0)
            {
              add_edge(unlocked, exec, ptpn);
            }
            t_node.push_back(drop_lock);
            t_node.push_back(unlocked);
          }
        }
        t_node.push_back(exec);
        t_node.push_back(exit);
        t_nodes.push_back(t_node);
        task_vertexes.insert(std::make_pair(current_t, t_node));
        preempt_task_vertexes.insert(std::make_pair(current_t, t_nodes));
      }
    }
  }
  // 链接边
  Agedge_t *succ_edge, *prev_edge;
  for (auto n : glb_task)
  {
    // 链接后继边
    for (succ_edge = agfstout(config.graph, n); succ_edge;
         succ_edge = agnxtout(config.graph, succ_edge))
    {
      Agnode_t *targetNode = aghead(succ_edge);
      std::string source_name = agnameof(n);
      std::string target_name = agnameof(targetNode);
      vertex_ptpn source = task_vertexes.find(source_name)->second.back();
      vertex_ptpn target;
      vertex_ptpn sub_target = 0;
      // 后继的任务可能属于subgraph
      if (config.task_where_sub.find(target_name) !=
          config.task_where_sub.end())
      {
        // 如果属于subgraph
        std::string sub_g_name =
            config.task_where_sub.find(target_name)->second;
        target = task_vertexes.find(sub_g_name)->second.front();
        sub_target = sub_task_timer.find(sub_g_name)->second.front();
      }
      else
      {
        target = task_vertexes.find(target_name)->second.front();
      }
      if ((source_name.find("Wait") == 0) ||
          (source_name.find("Distribute") == 0))
      {
        add_edge(source, target, ptpn);
      }
      else if (target_name.find("Wait") == 0 ||
               (target_name.find("Distribute") == 0))
      {
        vertex_ptpn wait_vertex = task_vertexes[target_name].front();
        add_edge(source, wait_vertex, ptpn);
      }
      else
      {
        std::string link_name = "link" + std::to_string(element_id);
        element_id += 1;
        vertex_ptpn link = add_transition(
            ptpn, link_name, PTPNTransition{256, std::make_pair(0, 0), 256});
        add_edge(source, link, ptpn);
        add_edge(link, target, ptpn);
        if (sub_target > 0)
        {
          add_edge(link, sub_target, ptpn);
        }
      }
    }

    // 链接前驱边
    for (prev_edge = agfstin(config.graph, n); prev_edge;
         prev_edge = agnxtin(config.graph, prev_edge))
    {
      Agnode_t *sourceNode = agtail(prev_edge);
      std::string source_name = agnameof(sourceNode);
      std::string target_name = agnameof(n);
      vertex_ptpn target = task_vertexes.find(target_name)->second.front();
      vertex_ptpn source;
      // 前驱的任务可能属于subgraph
      Agnode_t *source_node = agsubnode(config.graph, sourceNode, 0);
      if (config.task_where_sub.find(source_name) !=
          config.task_where_sub.end())
      {
        // 如果属于subgraph
        std::string sub_g_name =
            config.task_where_sub.find(source_name)->second;
        source = task_vertexes.find(sub_g_name)->second.back();
      }
      else
      {
        source = task_vertexes.find(source_name)->second.back();
      }
      if ((source_name.find("Wait") == 0) ||
          (source_name.find("Distribute") == 0))
      {
        add_edge(source, target, ptpn);
      }
      else if ((target_name.find("Wait") == 0) ||
               (target_name.find("Distribute") == 0))
      {
        add_edge(source, target, ptpn);
      }
      else
      {
        std::string link_name = "link" + std::to_string(element_id);
        element_id += 1;
        vertex_ptpn link = add_transition(
            ptpn, link_name, PTPNTransition{256, std::make_pair(0, 0), 256});
        add_edge(source, link, ptpn);
        add_edge(link, target, ptpn);
      }
    }
  }
  BOOST_LOG_TRIVIAL(info) << "Glb Task Petri Net Construction Completed!";
  create_core_vertex(6);
  bind_task_core(config);
  create_lock_vertex(config);
  bind_task_priority(config);
  task_bind_lock(config);
  boost::chrono::duration<double> sec =
      boost::chrono::steady_clock::now() - pt_a;
  BOOST_LOG_TRIVIAL(info) << "transform petri net: " << sec.count();
  BOOST_LOG_TRIVIAL(info) << "petri net P+T num: " << boost::num_vertices(ptpn);
  BOOST_LOG_TRIVIAL(info) << "petri net F num: " << boost::num_edges(ptpn);
}
void ProbPetriNet::bind_task_core(const GConfig &config)
{
  for (auto &t : config.sub_task_collection)
  {
    auto task_vertex = task_vertexes[t];
    vertex_ptpn get = task_vertex[1];
    vertex_ptpn drop = task_vertex[(task_vertex.size() - 2)];
    vertex_ptpn core =
        core_vertex[config.sub_graph_config.find(t)->second.core];
    add_edge(core, get, ptpn);
    add_edge(drop, core, ptpn);
  }
  for (auto &t : config.glb_task_collection)
  {
    auto task_vertex = task_vertexes[t];
    vertex_ptpn get = task_vertex[1];
    vertex_ptpn drop = task_vertex[(task_vertex.size() - 2)];
    vertex_ptpn core = core_vertex[config.tasks_label.find(t)->second.core];
    add_edge(core, get, ptpn);
    add_edge(drop, core, ptpn);
  }
  BOOST_LOG_TRIVIAL(info) << "Task Bind Core Completed!";
}
void ProbPetriNet::task_bind_lock(const GConfig &config)
{
  // 绑定所有边界点
  for (auto &t : config.glb_task_collection)
  {
    auto task_V = preempt_task_vertexes.find(t)->second;
    for (auto v : task_V)
    {
      if (v.size() <= 5)
      {
        break;
      }
      else
      {
        int lock_N = (int)config.tasks_label.find(t)->second.lock.size();
        for (int i = 0; i < lock_N; i++)
        {
          // 按照锁的次序获得锁的类型
          std::string lock_type = config.tasks_label.find(t)->second.lock[i];
          vertex_ptpn glock = v[(3 + 2 * i)];
          vertex_ptpn unlock = v[(v.size() - 4 - 2 * i)];
          vertex_ptpn lock = lock_vertex.find(lock_type)->second;
          add_edge(lock, glock, ptpn);
          add_edge(unlock, lock, ptpn);
        }
      }
    }
  }
  for (auto &sub : config.sub_task_set)
  {
    for (auto &task : sub.second)
    {
      std::vector<std::vector<vertex_ptpn>> task_V;
      if (preempt_task_vertexes.find(task) != preempt_task_vertexes.end())
      {
        task_V = preempt_task_vertexes.find(task)->second;
      }
      else
      {
        continue;
      }
      for (const auto &v : task_V)
      {
        if (v.size() <= 3)
        {
          break;
        }
        else
        {
          int lock_N = (int)config.tasks_label.find(task)->second.lock.size();
          for (int i = 0; i < lock_N; i++)
          {
            // 按照锁的次序获得锁的类型
            std::string lock_type =
                config.tasks_label.find(task)->second.lock[i];
            vertex_ptpn glock = v[(1 + 2 * i)];
            vertex_ptpn unlock = v[(v.size() - 4 - 2 * i)];
            vertex_ptpn lock = lock_vertex.find(lock_type)->second;
            add_edge(lock, glock, ptpn);
            add_edge(unlock, lock, ptpn);
          }
        }
      }
    }
  }
}
void ProbPetriNet::bind_task_priority(GConfig &config)
{
  std::map<int, std::vector<SubTaskConfig>> stp = config.classify_priority();
  std::vector<SubTaskConfig>::iterator it1, it2;
  for (auto &t : stp)
  {
    BOOST_LOG_TRIVIAL(debug) << "core index: " << t.first;
    for (it1 = t.second.begin(); it1 != t.second.end() - 1; ++it1)
    {
      std::string task_name_t = it1->name;
      for (it2 = it1 + 1; it2 != t.second.end(); ++it2)
      {
        if (it1->priority == it2->priority)
        {
          continue;
        }
        BOOST_LOG_TRIVIAL(debug)
            << "priority " << it1->name << ": " << it1->priority << it2->name
            << ": " << it2->priority;
        auto it1_vertex = preempt_task_vertexes.find(it1->name);
        auto it2_vertex = preempt_task_vertexes.find(it2->name);

        for (auto it : it1_vertex->second)
        {
          // auto preempt_task_node = it[2];
          // 判断it中是否有锁
          // 以任务的配置文件判断，正好检查构建 Petri 网模型的正确性
          SubTaskConfig tc_it1 = config.tasks_label.find(it1->name)->second;
          SubTaskConfig tc_it2 = config.tasks_label.find(it2->name)->second;

          if (it1->is_sub)
          {
            // 被抢占的任务是子任务
            // 抢占的任务是子任务
            // 抢占的任务无锁
            if (tc_it1.lock.empty())
            {
              ptpn[it[1]].pnt.is_handle = true;
              create_priority_task(config, it2->name, it[0], it[1],
                                   (it2_vertex->second)[0][0],
                                   (it2_vertex->second)[0].back());
            }
            else
            {
              // 抢占的任务有锁
              ptpn[it[1]].pnt.is_handle = true;
              ptpn[it[it.size() - 2]].pnt.is_handle = true;
              create_priority_task(config, it2->name, it[0], it[1],
                                   (it2_vertex->second)[0][0],
                                   (it2_vertex->second)[0].back());
              create_priority_task(
                  config, it2->name, it[it.size() - 3], it[it.size() - 2],
                  (it2_vertex->second)[0][0], (it2_vertex->second)[0].back());
            }
          }
          else
          {
            // 被抢占的任务不是子任务
            if (tc_it1.lock.empty())
            {
              ptpn[it[3]].pnt.is_handle = true;
              ptpn[it[it.size() - 2]].pnt.is_handle = true;
              create_priority_task(config, it2->name, it[2], it[3],
                                   (it2_vertex->second)[0][0],
                                   (it2_vertex->second)[0].back());
            }
            else
            {
              ptpn[it[3]].pnt.is_handle = true;
              ptpn[it[it.size() - 2]].pnt.is_handle = true;
              create_priority_task(config, it2->name, it[2], it[3],
                                   (it2_vertex->second)[0][0],
                                   (it2_vertex->second)[0].back());
              create_priority_task(
                  config, it2->name, it[it.size() - 3], it[it.size() - 2],
                  (it2_vertex->second)[0][0], (it2_vertex->second)[0].back());
            }
          }
        }
        //          if (tc_it1.lock.empty()) {
        //            // mark the preempt task transition could be handled
        //            auto handle_vertex = it[2];
        //            ptpn[it[3]].pnt.is_handle = true;
        //            auto preempt_task_start =
        //            (it2_vertex->second)[0].front(); auto preempt_task_end =
        //            (it2_vertex->second)[0].back();
        //
        //            // 如果抢占的任务中使用锁
        //            //            if (it2_vertex->second.size() > 5) {
        //            std::string task_get =
        //                it2->name + "get_core" + std::to_string(element_id);
        //            std::string task_ready =
        //                it2->name + "ready" + std::to_string(element_id);
        //            std::string task_lock =
        //                it2->name + "get_lock" + std::to_string(element_id);
        //            std::string task_deal =
        //                it2->name + "deal" + std::to_string(element_id);
        //            std::string task_drop =
        //                it2->name + "drop_lock" +
        //                std::to_string(element_id);
        //            std::string task_unlock =
        //                it2->name + "unlocked" + std::to_string(element_id);
        //            std::string task_exec =
        //                it2->name + "exec" + std::to_string(element_id);
        //
        //            // get task prority and execute time
        //            std::vector<vertex_ptpn> node;
        //            int task_pror = tc_it2.priority;
        //            int task_core = tc_it2.core;
        //            auto task_exec_t = tc_it2.time[(tc_it2.time.size() -
        //            1)];
        //
        //            vertex_ptpn get_core = add_transition(
        //                ptpn, task_get,
        //                PTPNTransition{task_pror, std::make_pair(0, 0),
        //                task_core});
        //            vertex_ptpn ready = add_place(ptpn, task_ready, 0);
        //
        //            vertex_ptpn exec = add_transition(
        //                ptpn, task_exec,
        //                PTPNTransition{task_pror, task_exec_t, task_core});
        //
        //            add_edge(preempt_task_start, get_core, ptpn);
        //            add_edge(handle_vertex, get_core, ptpn);
        //            add_edge(get_core, ready, ptpn);
        //            add_edge(exec, preempt_task_end, ptpn);
        //            add_edge(exec, handle_vertex, ptpn);
        //            node.push_back(preempt_task_start);
        //            node.push_back(get_core);
        //            node.push_back(ready);
        //            if (tc_it2.lock.empty()) {
        //              add_edge(ready, exec, ptpn);
        //              element_id += 1;
        //            } else {
        //              for (int j = 0; j < tc_it2.lock.size(); j++) {
        //                auto lock_name = tc_it2.lock[j];
        //                auto gl = task_lock + lock_name +
        //                std::to_string(element_id); vertex_ptpn get_lock =
        //                add_transition(
        //                    ptpn, gl, PTPNTransition{256, {0, 0},
        //                    task_core});
        //                vertex_ptpn deal = add_place(
        //                    ptpn, task_deal + lock_name +
        //                    std::to_string(element_id), 0);
        //                node.push_back(get_lock);
        //                node.push_back(deal);
        //                add_edge(node.back(), get_lock, ptpn);
        //                add_edge(get_lock, deal, ptpn);
        //                //                if (j==0) {
        //                //                  add_edge(ready, get_lock,
        //                time_petri_net);
        //                //                }
        //              }
        //              auto link = node.back();
        //              for (int k = (tc_it2.lock.size() - 1); k >= 0; k--) {
        //                auto lock_name = tc_it2.lock[k];
        //                auto l_t = tc_it2.time[k];
        //                auto dl = task_drop + lock_name +
        //                std::to_string(element_id); vertex_ptpn drop_lock =
        //                add_transition(
        //                    ptpn, dl, PTPNTransition{256, l_t, task_core});
        //                vertex_ptpn unlocked = add_place(
        //                    ptpn, task_unlock + lock_name +
        //                    std::to_string(element_id), 0);
        //                add_edge(node.back(), drop_lock, ptpn);
        //                add_edge(drop_lock, unlocked, ptpn);
        //                if (tc_it2.lock.size() == 1) {
        //                  add_edge(unlocked, exec, ptpn);
        //                } else if (k == 0) {
        //                  add_edge(unlocked, exec, ptpn);
        //                }
        //                node.push_back(drop_lock);
        //                node.push_back(unlocked);
        //              }
        //              element_id += 1;
        //            }
        //            node.push_back(exec);
        //            node.push_back(preempt_task_end);
        //            preempt_task_vertexes.find(it2->name)->second.push_back(node);
        //          } else {
        //            //
        //            如果抢占的任务有锁，判断锁的类型是否可被抢占，无锁，按照正常排列
        //            auto preempt_vertex_front = it[2];
        //            auto preempt_vertex_back = it[it.size() - 3];
        //            //              auto lock_type =
        //            //              time_petri_net[preempt_vertex].name;
        //            create_priority_task(config, it2->name,
        //            preempt_vertex_front, it[3],
        //                                 (it2_vertex->second)[0][0],
        //                                 (it2_vertex->second)[0].back());
        //            create_priority_task(config, it2->name,
        //            preempt_vertex_back,
        //                                 it[it.size() - 2],
        //                                 (it2_vertex->second)[0][0],
        //                                 (it2_vertex->second)[0].back());
        //          }
        //        }
      }
    }
  }
  BOOST_LOG_TRIVIAL(info) << "bind task preempt";
}

void ProbPetriNet::create_priority_task(const GConfig &config,
                                        const std::string &name,
                                        vertex_ptpn preempt_vertex,
                                        size_t handle_t, vertex_ptpn start,
                                        vertex_ptpn end)
{
  ptpn[handle_t].pnt.is_handle = true;
  std::string task_get = name + "get_core" + std::to_string(element_id);
  std::string task_ready = name + "ready" + std::to_string(element_id);
  std::string task_lock = name + "get_lock" + std::to_string(element_id);
  std::string task_deal = name + "deal" + std::to_string(element_id);
  std::string task_drop = name + "drop_lock" + std::to_string(element_id);
  std::string task_unlock = name + "unlocked" + std::to_string(element_id);
  std::string task_exec = name + "exec" + std::to_string(element_id);
  // get task prority and execute time
  std::vector<std::vector<vertex_ptpn>> task_node_tmp;
  std::vector<vertex_ptpn> node;
  SubTaskConfig tc_t = config.tasks_label.find(name)->second;
  auto task_exec_t = tc_t.time[(tc_t.time.size() - 1)];
  int task_pror = tc_t.priority;
  int taks_core = tc_t.core;
  if (tc_t.is_sub)
  {
    // 高优先级任务是子任务
    node.push_back(start);
    vertex_ptpn exec = add_transition(
        ptpn, task_exec, PTPNTransition{task_pror, task_exec_t, taks_core});
    add_edge(exec, end, ptpn);
    add_edge(exec, preempt_vertex, ptpn);
    if (tc_t.lock.empty())
    {
      add_edge(start, exec, ptpn);
      add_edge(preempt_vertex, exec, ptpn);
      element_id += 1;
    }
    else
    {
      for (int j = 0; j < tc_t.lock.size(); j++)
      {
        auto lock_name = tc_t.lock[j];
        std::string gl = task_lock + lock_name;
        vertex_ptpn get_lock =
            add_transition(ptpn, gl, PTPNTransition{256, {0, 0}, taks_core});
        vertex_ptpn deal = add_place(ptpn, task_deal + lock_name, 0);
        add_edge(node.back(), get_lock, ptpn);
        node.push_back(get_lock);
        node.push_back(deal);
        add_edge(get_lock, deal, ptpn);
        if (j == 0)
        {
          add_edge(start, get_lock, ptpn);
          add_edge(preempt_vertex, get_lock, ptpn);
        }
      }
      // auto link = node.back();
      for (int k = (tc_t.lock.size() - 1); k >= 0; k--)
      {
        auto lock_name = tc_t.lock[k];
        auto t = tc_t.time[k];
        std::string dl = task_drop + lock_name;
        vertex_ptpn drop_lock =
            add_transition(ptpn, dl, PTPNTransition{256, t, taks_core});
        vertex_ptpn unlocked = add_place(ptpn, task_unlock + lock_name, 0);
        add_edge(node.back(), drop_lock, ptpn);
        add_edge(drop_lock, unlocked, ptpn);
        if (tc_t.lock.size() == 1)
        {
          add_edge(unlocked, exec, ptpn);
        }
        else if (k == 0)
        {
          add_edge(unlocked, exec, ptpn);
        }
        node.push_back(drop_lock);
        node.push_back(unlocked);
      }
      element_id += 1;
    }
    node.push_back(exec);
    node.push_back(end);
  }
  else
  {
    node.push_back(start);
    vertex_ptpn get_core = add_transition(
        ptpn, task_get,
        PTPNTransition{task_pror, std::make_pair(0, 0), taks_core});
    vertex_ptpn ready = add_place(ptpn, task_ready, 0);
    vertex_ptpn exec = add_transition(
        ptpn, task_exec, PTPNTransition{task_pror, task_exec_t, taks_core});
    add_edge(start, get_core, ptpn);
    add_edge(preempt_vertex, get_core, ptpn);
    add_edge(get_core, ready, ptpn);
    add_edge(exec, end, ptpn);
    add_edge(exec, preempt_vertex, ptpn);
    node.push_back(start);
    node.push_back(get_core);
    node.push_back(ready);
    if (tc_t.lock.empty())
    {
      add_edge(ready, exec, ptpn);
      element_id += 1;
    }
    else
    {
      for (int j = 0; j < tc_t.lock.size(); j++)
      {
        auto lock_name = tc_t.lock[j];
        std::string gl = task_lock + lock_name;
        vertex_ptpn get_lock =
            add_transition(ptpn, gl, PTPNTransition{256, {0, 0}, taks_core});
        vertex_ptpn deal = add_place(ptpn, task_deal + lock_name, 0);
        add_edge(node.back(), get_lock, ptpn);
        node.push_back(get_lock);
        node.push_back(deal);
        add_edge(get_lock, deal, ptpn);
      }
      // auto link = node.back();
      for (int k = (tc_t.lock.size() - 1); k >= 0; k--)
      {
        auto lock_name = tc_t.lock[k];
        auto t = tc_t.time[k];
        std::string dl = task_drop + lock_name;
        vertex_ptpn drop_lock =
            add_transition(ptpn, dl, PTPNTransition{256, t, taks_core});
        vertex_ptpn unlocked = add_place(ptpn, task_unlock + lock_name, 0);
        add_edge(node.back(), drop_lock, ptpn);
        add_edge(drop_lock, unlocked, ptpn);
        if (tc_t.lock.size() == 1)
        {
          add_edge(unlocked, exec, ptpn);
        }
        else if (k == 0)
        {
          add_edge(unlocked, exec, ptpn);
        }
        node.push_back(drop_lock);
        node.push_back(unlocked);
      }
      element_id += 1;
    }
    node.push_back(exec);
    node.push_back(end);
  }

  preempt_task_vertexes.find(name)->second.push_back(node);
}

StateClass ProbPetriNet::get_initial_state_class()
{
  BOOST_LOG_TRIVIAL(info) << "get_initial_state_class";
  ptpn[task_vertexes.find("start")->second.front()].token = 1;
  Marking initial_markings;
  //  std::set<std::size_t> h_t,H_t;
  //  std::unordered_map<size_t, int> enabled_t_time;
  std::set<T_wait> all_t;
  boost::graph_traits<PTPN>::vertex_iterator vi, vi_end;
  boost::graph_traits<PTPN>::in_edge_iterator in_i, in_end;
  for (boost::tie(vi, vi_end) = boost::vertices(ptpn); vi != vi_end; ++vi)
  {
    if (ptpn[*vi].shape == "circle")
    {
      if (ptpn[*vi].token == 1)
      {
        initial_markings.indexes.insert(index(*vi));
        initial_markings.labels.insert(ptpn[*vi].label);
      }
    }
    else
    {
      bool flag = true;
      for (boost::tie(in_i, in_end) = boost::in_edges(*vi, ptpn);
           in_i != in_end; ++in_i)
      {
        auto source_vertex = source(*in_i, ptpn);
        if (ptpn[source_vertex].token == 0)
        {
          flag = false;
          break;
        }
      }
      if (flag)
      {
        all_t.insert({*vi, 0});
      }
    }
  }

  return StateClass{initial_markings, all_t};
}
void ProbPetriNet::set_state_class(const StateClass &state_class)
{
  // 首先重置原有状态
  boost::graph_traits<PTPN>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = vertices(ptpn); vi != vi_end; ++vi)
  {
    ptpn[*vi].token = 0;
    ptpn[*vi].enabled = false;
    ptpn[*vi].pnt.runtime = 0;
  }
  // 2. 设置标识
  for (auto m : state_class.mark.indexes)
  {
    ptpn[m].token = 1;
  }
  // 3. 设置各个变迁的已等待时间
  //  for (auto t : state_class.t_sched) {
  //    time_petri_net[t].pnt.runtime = state_class.t_time.find(t)->second;
  //  }
  // 4. 可挂起变迁的已等待时间
  //  for (auto t : state_class.handle_t_sched) {
  //    time_petri_net[t].pnt.runtime = state_class.t_time.find(t)->second;
  //  }
  // 5.设置所有变迁的已等待时间
  for (const auto &t : state_class.all_t)
  {
    ptpn[t.t].pnt.runtime = t.time;
  }
}
void ProbPetriNet::generate_state_class()
{
  BOOST_LOG_TRIVIAL(info) << "Generating state class";
  auto pt_scg = boost::chrono::steady_clock::now();
  std::queue<StateClass> q;
  q.push(initial_state_class);
  // scg.insert(initial_state_class);

  std::string vertex_label = initial_state_class.to_scg_vertex();
  ScgVertexD v_id =
      boost::add_vertex(SCGVertex{vertex_label}, state_class_graph.scg);
  state_class_graph.scg_vertex_map.insert(
      std::make_pair(initial_state_class, v_id));
  int i = 0;
  while (!q.empty())
  {
    StateClass s = q.front();
    q.pop();
    ScgVertexD prev_vertex = state_class_graph.add_scg_vertex(s);

    std::vector<SchedT> sched_t = get_sched_t(s);

    for (auto &t : sched_t)
    {
      StateClass new_state = fire_transition(s, t);
      //      std::cout << i << std::endl;
      ++i;
      // BOOST_LOG_TRIVIAL(info) << "new state: ";
      // new_state.print_current_mark();
      if (state_class_graph.scg_vertex_map.find(new_state) != state_class_graph.scg_vertex_map.end())
      {
      }
      else
      {
        // scg.insert(new_state);
        q.push(new_state);
        // new_state.print_current_state();
      }

      auto succ_vertex = state_class_graph.add_scg_vertex(new_state);
      SCGEdge scg_e = {std::to_string(t.time.first)
                           .append(":")
                           .append(std::to_string(t.time.second)),
                       t.time};
      boost::add_edge(prev_vertex, succ_vertex, scg_e, state_class_graph.scg);
    }
    //    if (sec.count() >= timeout) {
    //        break;
    //    }
  }
  boost::chrono::duration<double> sec =
      boost::chrono::steady_clock::now() - pt_scg;
  BOOST_LOG_TRIVIAL(info) << "Generate SCG Time(s): " << sec.count();
  BOOST_LOG_TRIVIAL(info) << "SCG NUM: " << state_class_graph.scg_vertex_map.size();
  if (state_class_graph.check_deadlock())
  {
    BOOST_LOG_TRIVIAL(info) << "SYSTEM NO DEADLOCK!";
  }
  else
  {
    BOOST_LOG_TRIVIAL(info) << "SYSTEM HAS DEADLOCK!";
  }

  state_class_graph.write_to_dot("out.dot");
}
std::vector<SchedT> ProbPetriNet::get_sched_t(StateClass &state)
{
  BOOST_LOG_TRIVIAL(debug) << "get sched t";
  set_state_class(state);
  // 获得使能的变迁
  std::vector<vertex_ptpn> enabled_t_s;
  std::vector<SchedT> sched_T;
  boost::graph_traits<PTPN>::vertex_iterator vi, vi_end;
  boost::graph_traits<PTPN>::in_edge_iterator in_i, in_end;
  for (boost::tie(vi, vi_end) = vertices(ptpn); vi != vi_end; ++vi)
  {
    bool enable_t = true;
    if (ptpn[*vi].shape == "circle")
    {
      continue;
    }
    else
    {
      for (boost::tie(in_i, in_end) = in_edges(*vi, ptpn); in_i != in_end;
           ++in_i)
      {
        auto source_vertex = source(*in_i, ptpn);
        if (ptpn[source_vertex].token == 0)
        {
          enable_t = false;
          break;
        }
      }
      if (enable_t)
        enabled_t_s.push_back(vertex(*vi, ptpn));
    }
  }

  if (enabled_t_s.empty())
  {
    return {};
  }

  // 遍历这些变迁,找到符合发生区间的变迁集合
  std::pair<int, int> fire_time = {INT_MAX, INT_MAX};
  for (auto t : enabled_t_s)
  {
    // 如果等待时间已超过最短发生时间
    int f_min = ptpn[t].pnt.const_time.first - ptpn[t].pnt.runtime;
    if (f_min < 0)
    {
      f_min = 0;
    }
    int f_max = ptpn[t].pnt.const_time.second - ptpn[t].pnt.runtime;
    fire_time.first = (f_min < fire_time.first) ? f_min : fire_time.first;
    fire_time.second = (f_max < fire_time.second) ? f_max : fire_time.second;
  }
  if ((fire_time.first < 0) && (fire_time.second < 0))
  {
    // BOOST_LOG_TRIVIAL(error) << "fire time not leq zero";
  }
  // find transition which satifies the time domain
  std::pair<int, int> sched_time = {0, 0};
  int s_h, s_l;
  for (auto t : enabled_t_s)
  {
    int t_min = ptpn[t].pnt.const_time.first - ptpn[t].pnt.runtime;
    if (t_min < 0)
    {
      t_min = 0;
    }
    int t_max = ptpn[t].pnt.const_time.second - ptpn[t].pnt.runtime;
    if (t_min > fire_time.second)
    {
      // sched_time = fire_time;
      continue;
    }
    else if (t_max > fire_time.second)
    {
      // transition's max go beyond time_d
      if (t_min >= fire_time.first)
      {
        sched_time = {t_min, fire_time.second};
        sched_T.push_back(SchedT{t, sched_time});
      }
      else
      {
        BOOST_LOG_TRIVIAL(error) << "the time_d min value is error";
      }
    }
    else
    {
      if (t_min >= fire_time.first)
      {
        sched_time = {t_min, t_max};
        sched_T.push_back(SchedT{t, sched_time});
      }
      else
      {
        BOOST_LOG_TRIVIAL(error) << "the time_d min value is error";
      }
    }
  }

  // 删除优先级
  auto s_it = sched_T.begin();
  while (s_it != sched_T.end())
  {
    auto next = s_it + 1;
    while (next != sched_T.end())
    {
      if ((ptpn[(*s_it).t].pnt.priority > ptpn[(*next).t].pnt.priority) &&
          (ptpn[(*s_it).t].pnt.c == ptpn[(*next).t].pnt.c))
      {
        sched_T.erase(next);
        next = s_it + 1;
      }
      else
      {
        ++next;
      }
    }
    ++s_it;
  }
  for (auto d : sched_T)
  {
    BOOST_LOG_TRIVIAL(debug) << ptpn[d.t].label;
  }

  return sched_T;
}
StateClass ProbPetriNet::fire_transition(const StateClass &sc,
                                         SchedT transition)
{
  BOOST_LOG_TRIVIAL(debug) << "fire_transition: " << transition.t;
  // reset!
  set_state_class(sc);
  // fire transition
  // 1. 首先获取所有可调度变迁
  std::vector<std::size_t> enabled_t, old_enabled_t;
  boost::graph_traits<PTPN>::vertex_iterator vi, vi_end;
  boost::graph_traits<PTPN>::in_edge_iterator in_i, in_end;
  for (boost::tie(vi, vi_end) = vertices(ptpn); vi != vi_end; ++vi)
  {
    bool enable_t = true;
    if (ptpn[*vi].shape == "circle")
    {
      continue;
    }
    else
    {
      for (boost::tie(in_i, in_end) = in_edges(*vi, ptpn); in_i != in_end;
           ++in_i)
      {
        auto source_vertex = source(*in_i, ptpn);
        if (ptpn[source_vertex].token == 0)
        {
          enable_t = false;
          break;
        }
      }
      if (enable_t)
      {
        enabled_t.push_back(vertex(*vi, ptpn));
        old_enabled_t.push_back(vertex(*vi, ptpn));
      }
    }
  }

  // 删除优先级
  auto s_it = enabled_t.begin();
  while (s_it != enabled_t.end())
  {
    auto next = s_it + 1;
    while (next != enabled_t.end())
    {
      if ((ptpn[(*s_it)].pnt.priority > ptpn[(*next)].pnt.priority) &&
          (ptpn[(*s_it)].pnt.c == ptpn[(*next)].pnt.c))
      {
        enabled_t.erase(next);
        next = s_it + 1;
      }
      else
      {
        ++next;
      }
    }
    ++s_it;
  }
  // 2. 将除发生变迁外的使能时间+状态转移时间
  for (auto t : enabled_t)
  {
    if (transition.time.second == transition.time.first)
    {
      ptpn[t].pnt.runtime += transition.time.first;
    }
    else
    {
      ptpn[t].pnt.runtime += transition.time.second;
    }
  }
  ptpn[transition.t].pnt.runtime = 0;
  // 3. 发生该变迁
  Marking new_mark;
  //  std::set<std::size_t> h_t, H_t;
  //  std::unordered_map<std::size_t, int> time;
  std::set<T_wait> all_t;
  // 发生变迁的前置集为 0
  for (boost::tie(in_i, in_end) = in_edges(transition.t, ptpn); in_i != in_end;
       ++in_i)
  {
    vertex_ptpn place = source(*in_i, ptpn);
    if (ptpn[place].token < 0)
    {
      BOOST_LOG_TRIVIAL(error)
          << "place token <= 0, the transition not enabled";
    }
    else
    {
      ptpn[place].token = 0;
    }
  }
  // 发生变迁的后继集为 1
  typename boost::graph_traits<PTPN>::out_edge_iterator out_i, out_end;
  for (boost::tie(out_i, out_end) = out_edges(transition.t, ptpn);
       out_i != out_end; ++out_i)
  {
    vertex_ptpn place = target(*out_i, ptpn);
    if (ptpn[place].token > 1)
    {
      BOOST_LOG_TRIVIAL(error)
          << "safe petri net, the place not increment >= 1";
    }
    else
    {
      ptpn[place].token = 1;
    }
  }
  // 4.获取新的标识
  std::vector<std::size_t> new_enabled_t;
  for (boost::tie(vi, vi_end) = vertices(ptpn); vi != vi_end; ++vi)
  {
    bool enable_t = true;
    if (ptpn[*vi].shape == "circle")
    {
      if (ptpn[*vi].token == 1)
      {
        new_mark.indexes.insert(vertex(*vi, ptpn));
        new_mark.labels.insert(ptpn[*vi].label);
      }
    }
    else
    {
      for (boost::tie(in_i, in_end) = in_edges(*vi, ptpn); in_i != in_end;
           ++in_i)
      {
        auto source_vertex = source(*in_i, ptpn);
        if (ptpn[source_vertex].token == 0)
        {
          enable_t = false;
          break;
        }
      }
      if (enable_t)
        new_enabled_t.push_back(vertex(*vi, ptpn));
    }
  }

  // 5. 比较前后使能部分,前一种状态使能,而新状态不使能,则为可挂起变迁
  // 将所有变迁等待时间置为0

  // 5.1 公共部分
  std::set<std::size_t> common;
  std::set_intersection(old_enabled_t.begin(), old_enabled_t.end(),
                        new_enabled_t.begin(), new_enabled_t.end(),
                        std::inserter(common, common.begin()));
  for (unsigned long it : common)
  {
    all_t.insert({it, ptpn[it].pnt.runtime});
    //    h_t.insert(it);
    //    time.insert(std::make_pair(it, time_petri_net[it].pnt.runtime));
  }
  // 5.2 前状态使能而现状态不使能
  std::set<std::size_t> old_minus_new;
  std::set_difference(old_enabled_t.begin(), old_enabled_t.end(),
                      new_enabled_t.begin(), new_enabled_t.end(),
                      std::inserter(old_minus_new, old_minus_new.begin()));
  for (unsigned long it : old_minus_new)
  {
    // 若为可挂起变迁,则保存已运行时间
    if (ptpn[it].pnt.is_handle)
    {
      all_t.insert({it, ptpn[it].pnt.runtime});
      //      H_t.insert(it);
      //      time.insert(std::make_pair(it, time_petri_net[it].pnt.runtime));
    }
    else
    {
      ptpn[it].pnt.runtime = 0;
    }
  }
  // 5.3 现状态使能而前状态不使能
  std::set<std::size_t> new_minus_old;
  std::set_difference(new_enabled_t.begin(), new_enabled_t.end(),
                      old_enabled_t.begin(), old_enabled_t.end(),
                      std::inserter(new_minus_old, new_minus_old.begin()));
  for (unsigned long it : new_minus_old)
  {
    if (ptpn[it].pnt.is_handle)
    {
      all_t.insert({it, ptpn[it].pnt.runtime});
      //      h_t.insert(it);
      //      time.insert(std::make_pair(it, time_petri_net[it].pnt.runtime));
    }
    else
    {
      //      h_t.insert(it);
      //      time.insert(std::make_pair(it, 0));
      all_t.insert({it, 0});
    }
  }
  return {new_mark, all_t};
}
