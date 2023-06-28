//
// Created by 张凯文 on 2023/6/26.
//

#include "timepetrinet.h"

void TimePetriNet::init_graph() {
  tpn_dp.property("node_id", get(&TPetriNetElement::name, time_petri_net));
  tpn_dp.property("label", get(&TPetriNetElement::label, time_petri_net));
  tpn_dp.property("shape", get(&TPetriNetElement::shape, time_petri_net));
  tpn_dp.property("label", get(&TPetriNetEdge::label, time_petri_net));

  boost::ref_property_map<TPN *, std::string> gname_pn(get_property(time_petri_net, boost::graph_name));
  tpn_dp.property("name", gname_pn);
}

void TimePetriNet::construct_petri_net(const Config& config) {
  collect_task(config);
//  PetriNet::construct_petri_net();
  for (const auto &id : task_name) {
    if (id.second.find("Wait") != std::string::npos) {
      std::string wait = "wait" + std::to_string(element_id);
      element_id += 1;
      TPetriNetTransition pt = {0, INT_MAX, std::make_pair(0, 0)};
      vertex_tpn wait_vertex = add_transition(time_petri_net, wait, false, pt);
      id_start_end.insert(std::pair<std::size_t, boost::tuple<vertex_tpn, vertex_tpn>>(id.first,
                                                                                       boost::tie(wait_vertex,
                                                                                                  wait_vertex)));
      id_str_start_end.insert(std::make_pair(id.second, std::make_pair(wait_vertex, wait_vertex)));
      BOOST_LOG_TRIVIAL(debug) << "find wait vertex!";
      continue;
    }
    std::string prefix = id.second;
    std::string task_entry = prefix + "entry";
    std::string task_get = prefix + "get_core";
    std::string task_ready = prefix + "ready";
    std::string task_lock = prefix + "get_lock";
    std::string task_deal = prefix + "deal";
    std::string task_drop = prefix + "drop_lock";
    std::string task_unlock = prefix + "unlocked";
    std::string task_exec = prefix + "exec";
    std::string task_exit = prefix + "exit";

    // get task prority and execute time
    std::vector<std::vector<vertex_tpn>> task_node_tmp;
    std::vector<vertex_tpn> node;
    TaskConfig tc_t = config.task.find(prefix)->second;
    int task_prior = tc_t.priority;
    auto task_exec_t = tc_t.time[(tc_t.time.size() - 1)];
    vertex_tpn entry = add_place(time_petri_net, task_entry, 0);
    vertex_tpn get_core = add_transition(time_petri_net, task_get, false,
                                         TPetriNetTransition{0, 0, std::make_pair(0, 0)});
    vertex_tpn ready = add_place(time_petri_net, task_ready, 0);

    vertex_tpn exec = add_transition(time_petri_net, task_exec, false,
                                     TPetriNetTransition{0, 0, task_exec_t});
    vertex_tpn exit = add_place(time_petri_net, task_exit, 0);

    add_edge(entry, get_core, time_petri_net);
    add_edge(get_core, ready, time_petri_net);
    add_edge(exec, exit, time_petri_net);
    node.push_back(entry);
    node.push_back(get_core);
    node.push_back(ready);
    if (tc_t.lock.empty()) {
      add_edge(ready, exec, time_petri_net);
    } else {
      size_t lock_n = tc_t.lock.size();
      for (int i = 0; i < lock_n; i++) {
        auto lock_name = tc_t.lock[i];
        vertex_tpn get_lock = add_transition(time_petri_net, task_lock + lock_name, false,
                                            TPetriNetTransition{0, 0, {0, 0}});
        vertex_tpn deal = add_place(time_petri_net, task_deal + lock_name, 0);
        node.push_back(get_lock);
        node.push_back(deal);
        add_edge(get_lock, deal, time_petri_net);
        if (i == 0) {
          add_edge(ready, get_lock, time_petri_net);
        }
      }
      auto link = node.back();

      for (int j = (lock_n - 1); j >= 0; j--) {
        auto lock_name = tc_t.lock[j];
        auto t = tc_t.time[j];
        vertex_tpn drop_lock = add_transition(time_petri_net, task_drop + lock_name, false,
                                             TPetriNetTransition{0, 0, t});
        vertex_tpn unlocked = add_place(time_petri_net, task_unlock + lock_name, 0);
        add_edge(drop_lock, unlocked, time_petri_net);
        if (lock_n == 1) {
          add_edge(link, drop_lock, time_petri_net);
          add_edge(unlocked, exec, time_petri_net);
        } else {
          if (j == (lock_n - 1)) {
            add_edge(link, drop_lock, time_petri_net);
          } else if (j == 0) {
            add_edge(unlocked, exec, time_petri_net);
          }
        }
        node.push_back(drop_lock);
        node.push_back(unlocked);
      }
    }
    node.push_back(exec);
    node.push_back(exit);
    task_node_tmp.push_back(node);
    single_task_node.insert(std::make_pair(prefix, node));
    id_start_end.insert(std::pair<std::size_t, boost::tuple<vertex_tpn, vertex_tpn>>(id.first, boost::tie(entry, exit)));
    id_str_start_end.insert(std::make_pair(id.second, std::make_pair(entry, exit)));
    multi_task_node.insert(std::make_pair(prefix, task_node_tmp));
  }

  typename boost::graph_traits<DAG>::edge_iterator ei, ei_end;
  typename boost::property_map<DAG, boost::vertex_index_t>::type index
      = get(boost::vertex_index, config.dag);
  BOOST_LOG_TRIVIAL(info) << "DEAL DAG EDGE";
  for (boost::tie(ei, ei_end) = edges(config.dag); ei != ei_end; ++ei) {
    auto source_vertex = index[source(*ei, config.dag)];
    auto target_vertex = index[target(*ei, config.dag)];
    if (source_vertex == target_vertex) {
      std::string n = (*(id_name_map_me.find(index[source(*ei, config.dag)]))).second;

      auto edge_desc = edge(source_vertex, target_vertex, config.dag);
      auto edge_label = get("label", config.dag_dp, edge_desc.first);
      auto spin_time_s = edge_label.substr(edge_label.find(',') + 1, 2);
      int spin_time = stoi(spin_time_s);
      std::string n_start = n + "start";
      std::string n_timer = n + "timer";
      vertex_tpn v1 = add_place(time_petri_net, n_start, 1);
      vertex_tpn v2 = add_transition(time_petri_net, n_timer, false,
                                    TPetriNetTransition{0, 0, std::make_pair(spin_time, spin_time)});
      // TODO: 初始化marking需要Refactor
      // initial_state.insert(v1);
      add_edge(v1, v2, time_petri_net);
      auto v_start = id_start_end[source_vertex];
      vertex_tpn v_s = v_start.head;
      add_edge(v2, v_s, time_petri_net);
    } else {
      std::string source_vertex_name = (*(id_name_map_me.find(source_vertex))).second;
      std::string target_vertex_name = (*(id_name_map_me.find(target_vertex))).second;
      if (source_vertex_name.find("Wait") == 0) {
        vertex_tpn wait_vertex = id_start_end[source_vertex].get<0>();
        vertex_tpn next_task = id_start_end[target_vertex].get<0>();
        add_edge(wait_vertex, next_task, time_petri_net);
      } else if (target_vertex_name.find("Wait") == 0) {
        vertex_tpn wait_vertex = id_start_end[target_vertex].get<0>();
        vertex_tpn last_task = id_start_end[source_vertex].get<1>();
        add_edge(last_task, wait_vertex, time_petri_net);
        // std::cout << "target:" << target_vertex_name << std::endl;
      } else {
        std::string link_name = "link" + std::to_string(element_id);
        element_id += 1;
        vertex_tpn link = add_transition(time_petri_net, link_name, false,
                                        TPetriNetTransition{0, INT_MAX, std::make_pair(0, 0)});
        auto v_s = id_start_end[source_vertex];
        vertex_tpn v_s_end = v_s.get<1>();
        add_edge(v_s_end, link, time_petri_net);
        auto v_t = id_start_end[target_vertex];
        vertex_tpn v_t_start = v_t.get<0>();
        add_edge(link, v_t_start, time_petri_net);
      }
    }
  }
  BOOST_LOG_TRIVIAL(info) << "Base Petri Net Construction Completed!";
  create_core_vertex(3);
  bind_task_core(config);
  create_lock_vertex(config);
  bind_task_priority(const_cast<Config &>(config));
  task_bind_lock(config);
  std::ofstream os;
  os.open("t.dot");
  boost::write_graphviz_dp(os, time_petri_net, tpn_dp);
}

bool TimePetriNet::collect_task(const Config& config) {
  id_name_map_me = config.task_index;
  typename boost::property_map<DAG, boost::vertex_index_t>::type index
      = get(boost::vertex_index, config.dag);
  BOOST_FOREACH (DAG::vertex_descriptor v, vertices(config.dag)) {
    std::string id = get("node_id", config.dag_dp, v);

    task_name.emplace_back(index[v], id);
  }
  BOOST_LOG_TRIVIAL(info) << "COLLECT DAG TASK";
  return true;
}

TimePetriNet::vertex_tpn TimePetriNet::add_place(TPN &g, std::string name, int token) {
  TPetriNetElement element = {name, token};
  return add_vertex(element, g);
}

TimePetriNet::vertex_tpn TimePetriNet::add_transition(TPN &g, std::string name, bool enable, TPetriNetTransition pnt) {
  TPetriNetElement element = {name, enable, pnt};
  return add_vertex(element, g);
}

bool TimePetriNet::create_core_vertex(int num) {
  for (int i = 0; i < num; i++) {
    std::string core_name = "core" + std::to_string(i);
    // vertex_tpn c = add_vertex(PetriNetElement{core_name, core_name, "circle", 1}, time_petri_net);
    vertex_tpn c = add_place(time_petri_net, core_name, 1);
    core_vertex.push_back(c);
  }
  BOOST_LOG_TRIVIAL(info) << "create core resource!";
  return true;
}

void TimePetriNet::bind_task_core(const Config& config) {
  for (auto t : config.j_task_name) {
    auto task_pair = single_task_node.find(t);
    auto task_vertex = task_pair->second;
    vertex_tpn get = task_vertex[1];
    vertex_tpn drop = task_vertex[(task_vertex.size() - 2)];
    vertex_tpn core = core_vertex[config.task.find(t)->second.core];
    add_edge(core, get, time_petri_net);
    add_edge(drop, core, time_petri_net);
  }
  BOOST_LOG_TRIVIAL(info) << "Task Bind Core Completed!";
}

bool TimePetriNet::create_lock_vertex(const Config& config) {
  if(config.locks.empty()) {
    return false;
  }
  for (const auto &lock : config.locks) {
    vertex_tpn v = add_place(time_petri_net, lock, 1);
    lock_vertex.insert(std::make_pair(lock, v));
  }
  BOOST_LOG_TRIVIAL(info) << "create lock resource!";
  return true;
}

void TimePetriNet::task_bind_lock(const Config& config){
  // 绑定所有边界点
  for (auto t : config.j_task_name) {
    auto task_V = multi_task_node.find(t)->second;
    for(auto v : task_V) {
      if(v.size() <= 5) {
        break;
      }else {
        int lock_N = config.task.find(t)->second.lock.size();
        for(int i = 0; i < lock_N; i++) {
          // 按照锁的次序获得锁的类型
          std::string lock_type = config.task.find(t)->second.lock[i];
          vertex_tpn glock = v[(3+2*i)];
          vertex_tpn unlock = v[(v.size() - 4 - 2 * i)];
          vertex_tpn lock = lock_vertex.find(lock_type)->second;
          add_edge(lock, glock, time_petri_net);
          add_edge(unlock, lock, time_petri_net);
        }
      }
    }
  }
  BOOST_LOG_TRIVIAL(info) << "Task Bind Lock Completed!";
}

void TimePetriNet::bind_task_priority(Config& config) {
  std::map<int, std::vector<TaskConfig>> tp = config.classify_priority();
  std::vector<TaskConfig>::iterator it1, it2;
  for (auto t : tp) {
    BOOST_LOG_TRIVIAL(debug) << "core index: " << t.first;
    for (it1 = t.second.begin(); it1 != t.second.end() - 1; ++it1) {
      std::string task_name_t = it1->name;
      for (it2 = t.second.begin() + 1; it2 != t.second.end(); ++it2) {
        if (it1->priority == it2->priority) {
          continue;
        }
        BOOST_LOG_TRIVIAL(info) << "priority "
                                << it1->name << ": " << it1->priority
                                << it2->name << ": " << it2->priority;
        auto it1_vertex = multi_task_node.find(it1->name);
        auto it2_vertex = multi_task_node.find(it2->name);

        for (auto it : it1_vertex->second) {
          // auto preempt_task_node = it[2];
          // 判断it中是否有锁
          // 以任务的配置文件判断，正好检查构建 Petri 网模型的正确性
          TaskConfig tc_it1 = config.task.find(it1->name)->second;
          TaskConfig tc_it2 = config.task.find(it2->name)->second;
          if (tc_it1.lock.empty()) {
            // mark the preempt task transition could be handled
            auto handle_vertex = (it1_vertex->second)[0][2];
            time_petri_net[it[3]].pnt.is_handle = true;
            auto preempt_task_start = (it2_vertex->second)[0].front();
            auto preempt_task_end = (it2_vertex->second)[0].back();

            // 如果抢占的任务中使用锁
//            if (it2_vertex->second.size() > 5) {
            std::string task_get = it2->name + "get_core" + std::to_string(element_id);
            std::string task_ready = it2->name + "ready" + std::to_string(element_id);
            std::string task_lock = it2->name + "get_lock" + std::to_string(element_id);
            std::string task_deal = it2->name + "deal" + std::to_string(element_id);
            std::string task_drop = it2->name + "drop_lock" + std::to_string(element_id);
            std::string task_unlock = it2->name + "unlocked" + std::to_string(element_id);
            std::string task_exec = it2->name + "exec" + std::to_string(element_id);

            // get task prority and execute time
            std::vector<vertex_tpn> node;
            int task_pror = tc_it2.priority;
            auto task_exec_t = tc_it2.time[(tc_it2.time.size() - 1)];

            vertex_tpn get_core = add_transition(time_petri_net, task_get, false,
                                                TPetriNetTransition{0, 0, std::make_pair(0, 0)});
            vertex_tpn ready = add_place(time_petri_net, task_ready, 0);

            vertex_tpn exec = add_transition(time_petri_net, task_exec, false,
                                            TPetriNetTransition{0, 0, task_exec_t});

            add_edge(preempt_task_start, get_core, time_petri_net);
            add_edge(handle_vertex, get_core, time_petri_net);
            add_edge(get_core, ready, time_petri_net);
            add_edge(exec, preempt_task_end, time_petri_net);
            add_edge(exec, handle_vertex, time_petri_net);
            node.push_back(preempt_task_start);
            node.push_back(get_core);
            node.push_back(ready);
            if (tc_it2.lock.empty()) {
              add_edge(ready, exec, time_petri_net);
              element_id += 1;
            } else {
              for(int j = 0; j < tc_it2.lock.size(); j++) {
                auto lock_name = tc_it2.lock[j];
                vertex_tpn get_lock = add_transition(time_petri_net, task_lock + lock_name + std::to_string(element_id), false,
                                                    TPetriNetTransition{0, 0, {0, 0}});
                vertex_tpn deal = add_place(time_petri_net, task_deal + lock_name + std::to_string(element_id), 0);
                node.push_back(get_lock);
                node.push_back(deal);
                add_edge(get_lock, deal, time_petri_net);
                if (j==0) {
                  add_edge(ready, get_lock, time_petri_net);
                }
              }
              auto link = node.back();
              for (int k = (tc_it2.lock.size() - 1); k >= 0; k--) {
                auto lock_name = tc_it2.lock[k];
                auto l_t = tc_it2.time[k];
                vertex_tpn drop_lock = add_transition(time_petri_net, task_drop + lock_name + std::to_string(element_id), false,
                                                     TPetriNetTransition{ 0, 0, l_t});
                vertex_tpn unlocked = add_place(time_petri_net, task_unlock + lock_name + std::to_string(element_id), 0);
                add_edge(drop_lock, unlocked, time_petri_net);
                if (tc_it2.lock.size() == 1) {
                  add_edge(link, drop_lock, time_petri_net);
                  add_edge(unlocked, exec, time_petri_net);
                }else {
                  if (k == (tc_it2.lock.size() - 1)) {
                    add_edge(link, drop_lock, time_petri_net);
                  } else if (k == 0) {
                    add_edge(unlocked, exec, time_petri_net);
                  }
                }
                node.push_back(drop_lock);
                node.push_back(unlocked);
              }
              element_id += 1;
            }
            node.push_back(exec);
            node.push_back(preempt_task_end);
            multi_task_node.find(it2->name)->second.push_back(node);
          } else {
            // 如果抢占的任务有锁，判断锁的类型是否可被抢占，无锁，按照正常排列
            int task_v_num = it.size();
            int lock_n = config.task.find(it1->name)->second.lock.size();
            // 不需要看锁的数量，直接遍历任务结构，第二个place 到倒数第二个 place 都可以抢占
            for (int p = 2; p <= task_v_num; p = p+2) {
              auto preempt_vertex = (it1_vertex->second)[0][p];
              auto lock_type = time_petri_net[preempt_vertex].name;
              if (lock_type.find("spin") != lock_type.npos) {
                continue;
              }else {
                time_petri_net[(p+1)].pnt.is_handle = true;
                auto preempt_task_start = (it2_vertex->second)[0][0];
                auto preempt_task_end = (it2_vertex->second)[0].back();

                std::string task_get = it2->name + "get_core" + std::to_string(element_id);
                std::string task_ready = it2->name + "ready" + std::to_string(element_id);
                std::string task_lock = it2->name + "get_lock" + std::to_string(element_id);
                std::string task_deal = it2->name + "deal" + std::to_string(element_id);
                std::string task_drop = it2->name + "drop_lock" + std::to_string(element_id);
                std::string task_unlock = it2->name + "unlocked" + std::to_string(element_id);
                std::string task_exec = it2->name + "exec" + std::to_string(element_id);

                // get task prority and execute time
                std::vector<std::vector<vertex_tpn>> task_node_tmp;
                std::vector<vertex_tpn> node;
                TaskConfig tc_t = config.task.find(it2->name)->second;
                int task_pror = tc_t.priority;
                auto task_exec_t = tc_t.time[(tc_t.time.size() - 1)];
                vertex_tpn get_core = add_transition(time_petri_net, task_get, false,
                                                    TPetriNetTransition{0, 0, std::make_pair(0, 0)});
                vertex_tpn ready = add_place(time_petri_net, task_ready, 0);

                vertex_tpn exec = add_transition(time_petri_net, task_exec, false,
                                                TPetriNetTransition{0, 0, task_exec_t});

                add_edge(preempt_task_start, get_core, time_petri_net);
                add_edge(preempt_vertex, get_core, time_petri_net);
                add_edge(get_core, ready, time_petri_net);
                add_edge(exec, preempt_task_end, time_petri_net);
                add_edge(exec, preempt_vertex, time_petri_net);
                node.push_back(preempt_task_start);
                node.push_back(get_core);
                node.push_back(ready);

                if (tc_t.lock.empty()) {
                  add_edge(ready, exec, time_petri_net);
                  element_id += 1;
                } else {
                  for(int j = 0; j < tc_t.lock.size(); j++) {
                    auto lock_name = tc_t.lock[j];
                    vertex_tpn get_lock = add_transition(time_petri_net, task_lock + lock_name + std::to_string(element_id), false,
                                                        TPetriNetTransition{0, 0, {0, 0}});
                    vertex_tpn deal = add_place(time_petri_net, task_deal + lock_name + std::to_string(element_id), 0);
                    node.push_back(get_lock);
                    node.push_back(deal);
                    add_edge(get_lock, deal, time_petri_net);
                    if (j==0) {
                      add_edge(ready, get_lock, time_petri_net);
                    }
                  }
                  auto link = node.back();
                  for (int k = (tc_t.lock.size() - 1); k >= 0; k--) {
                    auto lock_name = tc_t.lock[k];
                    auto t = tc_t.time[k];
                    vertex_tpn drop_lock = add_transition(time_petri_net, task_drop + lock_name + std::to_string(element_id), false,
                                                         TPetriNetTransition{ 0, 0, t});
                    vertex_tpn unlocked = add_place(time_petri_net, task_unlock + lock_name + std::to_string(element_id), 0);
                    add_edge(drop_lock, unlocked, time_petri_net);
                    if (tc_t.lock.size() == 1) {
                      add_edge(link, drop_lock, time_petri_net);
                      add_edge(unlocked, exec, time_petri_net);
                    }else {
                      if (k == (tc_t.lock.size() - 1)) {
                        add_edge(link, drop_lock, time_petri_net);
                      } else if (k == 0) {
                        add_edge(unlocked, exec, time_petri_net);
                      }
                    }
                    node.push_back(drop_lock);
                    node.push_back(unlocked);
                  }
                  element_id += 1;
                }
                node.push_back(exec);
                node.push_back(preempt_task_end);

                multi_task_node.find(it2->name)->second.push_back(node);
              }
            }

          }

        }
      }
    }
  }

  BOOST_LOG_TRIVIAL(info) << "bind task preempt";
}
