//
// Created by 张凯文 on 2023/6/26.
//

#include "timepetrinet.h"
#include <queue>
#include <utility>
#include <csignal>

#define BOOST_CHRONO_EXITENSIONS

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
  auto pt_a = boost::chrono::steady_clock::now();
//  PetriNet::construct_petri_net();
  std::map<std::string, vertex_tpn> wait_v;
  for (const auto &id : task_name) {
    if (id.second.find("Wait") != std::string::npos) {
      std::string wait0 = "wait" + std::to_string(element_id);
      element_id += 1;
//      std::string wait1 = "wait" + std::to_string(element_id);
//      element_id += 1;
//      std::string wait2 = "wait" + std::to_string(element_id);
//      element_id += 1;
      TPetriNetTransition pt = {0, 256, std::make_pair(0, 0), 256};
      vertex_tpn wait_v_s = add_transition(time_petri_net, wait0, false, pt);
//      vertex_tpn wait_v_m = add_place(time_petri_net, wait1, 0);
//      vertex_tpn wait_v_l = add_transition(time_petri_net, wait2, false, pt);
     // add_edge(wait_v_s, wait_v_m, time_petri_net);
      // add_edge(wait_v_m, wait_v_l, time_petri_net);
      id_start_end.insert(std::pair<std::size_t, boost::tuple<vertex_tpn, vertex_tpn>>(id.first,
                                                                                       boost::tie(wait_v_s,
                                                                                                  wait_v_s)));

      id_str_start_end.insert(std::make_pair(id.second, std::make_pair(wait_v_s, wait_v_s)));
      BOOST_LOG_TRIVIAL(debug) << "find wait vertex!";
      continue;
    }
    if (id.second.find("Distribute") != std::string::npos) {
      std::string dist = "distribute" + std::to_string(element_id);
      element_id += 1;
      TPetriNetTransition pt = {0, 256, std::make_pair(0, 0), 256};
      vertex_tpn dist_v_s = add_transition(time_petri_net, dist, false, pt);
      id_start_end.insert(std::pair<std::size_t, boost::tuple<vertex_tpn, vertex_tpn>>(id.first,
                                                                                       boost::tie(dist_v_s,
                                                                                                  dist_v_s)));

      id_str_start_end.insert(std::make_pair(id.second, std::make_pair(dist_v_s, dist_v_s)));
      BOOST_LOG_TRIVIAL(debug) << "find distribute vertex!";
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
    int task_pror = tc_t.priority;
    int task_core = tc_t.core;
    auto task_exec_t = tc_t.time[(tc_t.time.size() - 1)];
    vertex_tpn entry = add_place(time_petri_net, task_entry, 0);
    vertex_tpn get_core = add_transition(time_petri_net, task_get, false,
                                         TPetriNetTransition{0, task_pror, std::make_pair(0, 0), task_core});
    vertex_tpn ready = add_place(time_petri_net, task_ready, 0);

    vertex_tpn exec = add_transition(time_petri_net, task_exec, false,
                                     TPetriNetTransition{0, task_pror, task_exec_t, task_core});
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
                                            TPetriNetTransition{0, task_pror, {0, 0}, task_core});
        vertex_tpn deal = add_place(time_petri_net, task_deal + lock_name, 0);
        add_edge(node.back(), get_lock, time_petri_net);
        node.push_back(get_lock);
        node.push_back(deal);
        add_edge(get_lock, deal, time_petri_net);
      }
      auto link = node.back();

      for (int j = (lock_n - 1); j >= 0; j--) {
        auto lock_name = tc_t.lock[j];
        auto t = tc_t.time[j];
        vertex_tpn drop_lock = add_transition(time_petri_net, task_drop + lock_name, false,
                                             TPetriNetTransition{0, task_pror, t, task_core});
        vertex_tpn unlocked = add_place(time_petri_net, task_unlock + lock_name, 0);
        add_edge(node.back(), drop_lock, time_petri_net);
        add_edge(drop_lock, unlocked, time_petri_net);
        if (lock_n == 1) {
          //add_edge(link, drop_lock, time_petri_net);
          add_edge(unlocked, exec, time_petri_net);
        } else if (j == 0) {
          add_edge(unlocked, exec, time_petri_net);
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
  // 除自环外，任务链接关系
  std::map<std::size_t, std::vector<std::size_t>> s_t_map;
  for (boost::tie(ei, ei_end) = edges(config.dag); ei != ei_end; ++ei) {
    auto source_vertex = index[source(*ei, config.dag)];
    auto target_vertex = index[target(*ei, config.dag)];
    // 自环
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
                                    TPetriNetTransition{0, 256, std::make_pair(spin_time, spin_time), 256});
      // TODO: 初始化marking需要Refactor
      // initial_state.insert(v1);
      add_edge(v1, v2, time_petri_net);
      add_edge(v2, v1, time_petri_net);
      auto v_start = id_start_end[source_vertex];
      vertex_tpn v_s = v_start.head;
      add_edge(v2, v_s, time_petri_net);
    } else {
//      if (s_t_map.count(source_vertex) != 0) {
//        s_t_map.at(source_vertex).push_back(target_vertex);
//      }else {
//        s_t_map.insert(std::make_pair(source_vertex, target_vertex));
//      }
      //s_t_map.insert();
      std::string source_vertex_name = (*(id_name_map_me.find(source_vertex))).second;
      std::string target_vertex_name = (*(id_name_map_me.find(target_vertex))).second;
      if ((source_vertex_name.find("Wait") == 0) || (source_vertex_name.find("Distribute") == 0)){
        vertex_tpn wait_vertex = id_start_end[source_vertex].get<1>();
        vertex_tpn next_task = id_start_end[target_vertex].get<0>();
        add_edge(wait_vertex, next_task, time_petri_net);
      } else if (target_vertex_name.find("Wait") == 0 || (target_vertex_name.find("Distribute") == 0)) {
        vertex_tpn wait_vertex = id_start_end[target_vertex].get<0>();
        vertex_tpn last_task = id_start_end[source_vertex].get<1>();
        add_edge(last_task, wait_vertex, time_petri_net);
        task_succ.insert(std::make_pair(target_vertex, wait_vertex));
        // std::cout << "target:" << target_vertex_name << std::endl;
      } else {
        std::string link_name = "link" + std::to_string(element_id);
        element_id += 1;
        vertex_tpn link = add_transition(time_petri_net, link_name, false,
                                        TPetriNetTransition{0, 256, std::make_pair(0, 0), 256});

        auto v_s = id_start_end[source_vertex];
        vertex_tpn v_s_end = v_s.get<1>();
        add_edge(v_s_end, link, time_petri_net);
        auto v_t = id_start_end[target_vertex];
        vertex_tpn v_t_start = v_t.get<0>();
        add_edge(link, v_t_start, time_petri_net);
      }
    }
  }
//  for (auto & iter : s_t_map) {
//    auto s_v = iter.first;
//    std::string source_vertex_name = (*(id_name_map_me.find(s_v))).second;
//    // 判断后续节点是否包含 Wait
//    for (auto node : iter.second) {
//
//    }
//  }
  BOOST_LOG_TRIVIAL(info) << "Base Petri Net Construction Completed!";
  create_core_vertex(6);
  bind_task_core(config);
  create_lock_vertex(config);
  bind_task_priority(const_cast<Config &>(config));
  task_bind_lock(config);
  boost::chrono::duration<double> sec = boost::chrono::steady_clock::now() - pt_a;
  BOOST_LOG_TRIVIAL(info) << "transform petri net: " << sec.count();
  BOOST_LOG_TRIVIAL(info) << "petri net num: " << boost::num_vertices(time_petri_net);
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
            auto handle_vertex = it[2];
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
            int task_core = tc_it2.core;
            auto task_exec_t = tc_it2.time[(tc_it2.time.size() - 1)];

            vertex_tpn get_core = add_transition(time_petri_net, task_get, false,
                                                TPetriNetTransition{0, task_pror, std::make_pair(0, 0), task_core});
            vertex_tpn ready = add_place(time_petri_net, task_ready, 0);

            vertex_tpn exec = add_transition(time_petri_net, task_exec, false,
                                            TPetriNetTransition{0, task_pror, task_exec_t, task_core});

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
                                                    TPetriNetTransition{0, 256, {0, 0}, task_core});
                vertex_tpn deal = add_place(time_petri_net, task_deal + lock_name + std::to_string(element_id), 0);
                node.push_back(get_lock);
                node.push_back(deal);
                add_edge(node.back(), get_lock, time_petri_net);
                add_edge(get_lock, deal, time_petri_net);
//                if (j==0) {
//                  add_edge(ready, get_lock, time_petri_net);
//                }
              }
              auto link = node.back();
              for (int k = (tc_it2.lock.size() - 1); k >= 0; k--) {
                auto lock_name = tc_it2.lock[k];
                auto l_t = tc_it2.time[k];
                vertex_tpn drop_lock = add_transition(time_petri_net, task_drop + lock_name + std::to_string(element_id), false,
                                                     TPetriNetTransition{ 0, 256, l_t, task_core});
                vertex_tpn unlocked = add_place(time_petri_net, task_unlock + lock_name + std::to_string(element_id), 0);
                add_edge(node.back(), drop_lock, time_petri_net);
                add_edge(drop_lock, unlocked, time_petri_net);
                if (tc_it2.lock.size() == 1) {
                  add_edge(unlocked, exec, time_petri_net);
                }else if (k == 0) {
                    add_edge(unlocked, exec, time_petri_net);
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
            BOOST_LOG_TRIVIAL(info) << "preempt task has locks";
            int task_v_num = it.size();
            int lock_n = config.task.find(it1->name)->second.lock.size();
            // 记录自旋锁在锁集中出现的位置
            int record = 0;
            for (int l = 0; l < lock_n; l++) {
              if (config.task.find(it1->name)->second.lock[l].find("spin") !=std::string::npos ){
                record = l + 1;
                BOOST_LOG_TRIVIAL(info) << it1->name << "->" << "spin locks index: " << record;
              }
            }
            // 如果没有自旋锁，不需要看锁的数量，直接遍历任务结构，第二个place 到倒数第二个 place 都可以抢占
            //
            if (record <= 0 ) {
              for (int p = 2, q = (task_v_num - 3), l = 0; l <= lock_n; p = p+2, q = q - 2, l++) {
                auto preempt_vertex_front = it[p];
                auto preempt_vertex_back = it[q];
//              auto lock_type = time_petri_net[preempt_vertex].name;
                create_priority_task(config,
                                     it2->name,
                                     preempt_vertex_front,
                                     p,
                                     (it2_vertex->second)[0][0],
                                     (it2_vertex->second)[0].back()
                );
                create_priority_task(config,
                                     it2->name,
                                     preempt_vertex_back,
                                     q,
                                     (it2_vertex->second)[0][0],
                                     (it2_vertex->second)[0].back()
                );
              }
            }
            else {
            // 如果有自旋锁，记录位置
              for (int p = 2, q = (task_v_num - 3), l = 0; l < record; p = p+2, q = q - 2, l++) {
                auto preempt_vertex_front = it[p];
                auto preempt_vertex_back = it[q];
  //              auto lock_type = time_petri_net[preempt_vertex].name;
                create_priority_task(config,
                                     it2->name,
                                     preempt_vertex_front,
                                     p,
                                     (it2_vertex->second)[0][0],
                                     (it2_vertex->second)[0].back()
                );
                create_priority_task(config,
                                     it2->name,
                                     preempt_vertex_back,
                                     q,
                                     (it2_vertex->second)[0][0],
                                     (it2_vertex->second)[0].back()
                );
              }
            }

          }

        }
      }
    }
  }

  BOOST_LOG_TRIVIAL(info) << "bind task preempt";
}

void TimePetriNet::create_priority_task(const Config& config,
                                        const std::string& name,
                                        vertex_tpn preempt_vertex,
                                        int handle_t,
                                        vertex_tpn start,
                                        vertex_tpn end) {
  time_petri_net[(handle_t+1)].pnt.is_handle = true;

  std::string task_get = name + "get_core" + std::to_string(element_id);
  std::string task_ready = name + "ready" + std::to_string(element_id);
  std::string task_lock = name + "get_lock" + std::to_string(element_id);
  std::string task_deal = name + "deal" + std::to_string(element_id);
  std::string task_drop = name + "drop_lock" + std::to_string(element_id);
  std::string task_unlock = name + "unlocked" + std::to_string(element_id);
  std::string task_exec = name + "exec" + std::to_string(element_id);

  // get task prority and execute time
  std::vector<std::vector<vertex_tpn>> task_node_tmp;
  std::vector<vertex_tpn> node;
  TaskConfig tc_t = config.task.find(name)->second;
  int task_pror = tc_t.priority;
  int taks_core = tc_t.core;
  auto task_exec_t = tc_t.time[(tc_t.time.size() - 1)];
  vertex_tpn get_core = add_transition(time_petri_net, task_get, false,
                                       TPetriNetTransition{0, task_pror, std::make_pair(0, 0), taks_core});
  vertex_tpn ready = add_place(time_petri_net, task_ready, 0);

  vertex_tpn exec = add_transition(time_petri_net, task_exec, false,
                                   TPetriNetTransition{0, task_pror, task_exec_t, taks_core});

  add_edge(start, get_core, time_petri_net);
  add_edge(preempt_vertex, get_core, time_petri_net);
  add_edge(get_core, ready, time_petri_net);
  add_edge(exec, end, time_petri_net);
  add_edge(exec, preempt_vertex, time_petri_net);
  node.push_back(start);
  node.push_back(get_core);
  node.push_back(ready);

  if (tc_t.lock.empty()) {
    add_edge(ready, exec, time_petri_net);
    element_id += 1;
  } else {
    for(int j = 0; j < tc_t.lock.size(); j++) {
      auto lock_name = tc_t.lock[j];
      vertex_tpn get_lock = add_transition(time_petri_net, task_lock + lock_name, false,
                                           TPetriNetTransition{0, 256, {0, 0}, taks_core});
      vertex_tpn deal = add_place(time_petri_net, task_deal + lock_name, 0);
      add_edge(node.back(), get_lock, time_petri_net);
      node.push_back(get_lock);
      node.push_back(deal);
      add_edge(get_lock, deal, time_petri_net);
    }
    //auto link = node.back();
    for (int k = (tc_t.lock.size() - 1); k >= 0; k--) {
      auto lock_name = tc_t.lock[k];
      auto t = tc_t.time[k];
      vertex_tpn drop_lock = add_transition(time_petri_net, task_drop + lock_name, false,
                                            TPetriNetTransition{ 0, 256, t, taks_core});
      vertex_tpn unlocked = add_place(time_petri_net, task_unlock + lock_name, 0);
      add_edge(node.back(), drop_lock, time_petri_net);
      add_edge(drop_lock, unlocked, time_petri_net);
      if (tc_t.lock.size() == 1) {
        add_edge(unlocked, exec, time_petri_net);
      }else if (k == 0) {
        add_edge(unlocked, exec, time_petri_net);
      }
      node.push_back(drop_lock);
      node.push_back(unlocked);
    }
    element_id += 1;
  }
  node.push_back(exec);
  node.push_back(end);

  multi_task_node.find(name)->second.push_back(node);
}

StateClass TimePetriNet::get_initial_state_class() {
  BOOST_LOG_TRIVIAL(info) << "get_initial_state_class";
  Marking initial_markings;
//  std::set<std::size_t> h_t,H_t;
//  std::unordered_map<size_t, int> enabled_t_time;
  std::set<T_wait> all_t;
  boost::graph_traits<TPN>::vertex_iterator vi, vi_end;
  boost::graph_traits<TPN>::in_edge_iterator in_i, in_end;
  for (boost::tie(vi, vi_end) = boost::vertices(time_petri_net); vi != vi_end; ++vi) {
    if (time_petri_net[*vi].shape == "circle") {
      if (time_petri_net[*vi].token == 1) {
        initial_markings.indexes.insert(index(*vi));
        initial_markings.labels.insert(time_petri_net[*vi].label);
      }
    }else {
      bool flag = true;
      for (boost::tie(in_i, in_end) = boost::in_edges(*vi, time_petri_net);
           in_i != in_end; ++in_i) {
        auto source_vertex = source(*in_i, time_petri_net);
        if (time_petri_net[source_vertex].token == 0) {
          flag = false;
          break;
        }
      }
      if (flag) {
//        if (time_petri_net[*vi].pnt.is_handle) {
//          H_t.insert(index(*vi));
//          enabled_t_time.insert(std::make_pair(index(*vi), 0));
//        } else {
//          h_t.insert(index(*vi));
//          enabled_t_time.insert(std::make_pair(index(*vi), 0));
//        }

        all_t.insert({*vi, 0});
      }
    }
  }
  return StateClass{initial_markings, all_t};
}

void TimePetriNet::generate_state_class() {
  BOOST_LOG_TRIVIAL(info) << "Generating state class";
  auto pt_a = boost::chrono::steady_clock::now();
  std::queue<StateClass> q;
  q.push(initial_state_class);
  scg.insert(initial_state_class);

  while (!q.empty()) {
    StateClass s = q.front();
    q.pop();

    std::vector<SchedT> sched_t = get_sched_t(s);

    for (auto& t : sched_t) {
      StateClass new_state = fire_transition(s, t);
      //BOOST_LOG_TRIVIAL(info) << "new state: ";
      //new_state.print_current_mark();
      auto result = scg.insert(new_state);
      if (result.second) {
        q.push(new_state);
      }

    }
//    if (sec.count() >= timeout) {
//        break;
//    }

  }
  boost::chrono::duration<double> sec = boost::chrono::steady_clock::now() - pt_a;
  BOOST_LOG_TRIVIAL(info) << "generate state clas time(s): " << sec.count();
  BOOST_LOG_TRIVIAL(info) << "scg num: " << scg.size();
}

std::vector<SchedT> TimePetriNet::get_sched_t(StateClass &state) {
  BOOST_LOG_TRIVIAL(debug) << "get sched t";
  set_state_class(state);
  // 获得使能的变迁
  std::vector<vertex_tpn> enabled_t_s;
  std::vector<SchedT> sched_T;
  boost::graph_traits<TPN>::vertex_iterator vi, vi_end;
  boost::graph_traits<TPN>::in_edge_iterator in_i, in_end;
  for (boost::tie(vi, vi_end) = vertices(time_petri_net); vi != vi_end; ++vi) {
    bool enable_t = true;
    if (time_petri_net[*vi].shape == "circle") {
      continue;
    } else {
      for (boost::tie(in_i, in_end) = in_edges(*vi, time_petri_net); in_i != in_end; ++in_i) {
        auto source_vertex = source(*in_i, time_petri_net);
        if (time_petri_net[source_vertex].token == 0) {
          enable_t = false;
          break;
        }
      }
      if (enable_t)
        enabled_t_s.push_back(vertex(*vi, time_petri_net));
    }
  }

  if (enabled_t_s.empty()) {
      return {};
  }

  // 遍历这些变迁,找到符合发生区间的变迁集合
  std::pair<int, int> fire_time = {INT_MAX, INT_MAX};
  for(auto t : enabled_t_s) {
    fire_time.first = ((time_petri_net[t].pnt.const_time.first - time_petri_net[t].pnt.runtime) < fire_time.first)?
      (time_petri_net[t].pnt.const_time.first - time_petri_net[t].pnt.runtime):fire_time.first;
    fire_time.second = ((time_petri_net[t].pnt.const_time.second - time_petri_net[t].pnt.runtime) < fire_time.second)?
      (time_petri_net[t].pnt.const_time.second - time_petri_net[t].pnt.runtime):fire_time.second;
  }
  if ((fire_time.first < 0) && (fire_time.second < 0)) {
    BOOST_LOG_TRIVIAL(error) << "fire time not leq zero";
  }
  // find transition which satifies the time domain
  std::pair<int, int> sched_time = {0, 0};
  int s_h, s_l;
  for (auto t : enabled_t_s) {
    int t_min = time_petri_net[t].pnt.const_time.first - time_petri_net[t].pnt.runtime;
    int t_max = time_petri_net[t].pnt.const_time.second - time_petri_net[t].pnt.runtime;
    if(t_min > fire_time.second ) {

      sched_time = fire_time;
    } else if (t_max > fire_time.second){
      // transition's max go beyond time_d
      if (t_min >= fire_time.first) {
        sched_time = {t_min, fire_time.second};
      } else {
        BOOST_LOG_TRIVIAL(error) << "the time_d min value is error";
      }
    } else {
      if (t_min >= fire_time.first) {
        sched_time = {t_min, t_max};
      } else {
        BOOST_LOG_TRIVIAL(error) << "the time_d min value is error";
      }
    }

    sched_T.push_back(SchedT{t, sched_time});
  }

  // 删除优先级
  std::vector<int> erase_index;
  for (int i = 0; i < sched_T.size() - 1; i++) {
    for (int j = 1; j < sched_T.size(); j++) {
      if ((time_petri_net[sched_T[i].t].pnt.priority < time_petri_net[sched_T[j].t].pnt.priority) &&
            (time_petri_net[sched_T[i].t].pnt.c == time_petri_net[sched_T[j].t].pnt.c)) {
        erase_index.push_back(i);
        }
    }
  }
  // 遍历要删除的下标集合
  for (auto it = erase_index.rbegin(); it != erase_index.rend(); ++it) {
    auto index_i = *it;
    auto pos = sched_T.begin() + index_i;  // 获取对应元素的迭代器
    // 如果该变迁为可挂起变迁,记录已等待时间
//    if (time_petri_net[sched_T[*pos].t].pnt.is_handle) {
//    }
    sched_T.erase(pos);  // 移除该元素
  }
  for(auto d : sched_T) {
    BOOST_LOG_TRIVIAL(debug) << time_petri_net[d.t].label;
  }

  return sched_T;
}

StateClass TimePetriNet::fire_transition(StateClass sc, SchedT transition) {
  BOOST_LOG_TRIVIAL(debug) << "fire_transition: " << transition.t;
  // reset!
  set_state_class(sc);
  // fire transition
  // 1. 首先获取所有可调度变迁
  std::vector<std::size_t> enabled_t, old_enabled_t;
  boost::graph_traits<TPN>::vertex_iterator vi, vi_end;
  boost::graph_traits<TPN>::in_edge_iterator in_i, in_end;
  for (boost::tie(vi, vi_end) = vertices(time_petri_net); vi != vi_end; ++vi) {
    bool enable_t = true;
    if (time_petri_net[*vi].shape == "circle") {
        continue;
    } else {
        for (boost::tie(in_i, in_end) = in_edges(*vi, time_petri_net); in_i != in_end; ++in_i) {
        auto source_vertex = source(*in_i, time_petri_net);
        if (time_petri_net[source_vertex].token == 0) {
          enable_t = false;
          break;
        }
        }
        if (enable_t)
        enabled_t.push_back(vertex(*vi, time_petri_net));
        old_enabled_t.push_back(vertex(*vi, time_petri_net));
    }
  }

  // 删除优先级
  std::vector<int> erase_index;
  for (int i = 0; i < enabled_t.size() - 1; i++) {
    for (int j = 1; j < enabled_t.size(); j++) {
        if ((time_petri_net[enabled_t[i]].pnt.priority < time_petri_net[enabled_t[j]].pnt.priority) &&
            (time_petri_net[enabled_t[i]].pnt.c == time_petri_net[enabled_t[j]].pnt.c)) {
        erase_index.push_back(i);
        }
    }
  }
  // 遍历要删除的下标集合
  for (auto it = erase_index.rbegin(); it != erase_index.rend(); ++it) {
    auto index_i = *it;
    auto pos = enabled_t.begin() + index_i;  // 获取对应元素的迭代器
    // 如果该变迁为可挂起变迁,记录已等待时间
    //    if (time_petri_net[sched_T[*pos].t].pnt.is_handle) {
    //    }
    enabled_t.erase(pos);  // 移除该元素
  }
  // 2. 将除发生变迁外的使能时间+状态转移时间
  for (auto t : enabled_t) {
    if (transition.time.second == transition.time.first){
      time_petri_net[t].pnt.runtime += transition.time.first;
    }else {
      time_petri_net[t].pnt.runtime += (transition.time.second - transition.time.first);
    }
  }
  time_petri_net[transition.t].pnt.runtime = 0;
  // 3. 发生该变迁
  Marking new_mark;
  std::set<std::size_t> h_t, H_t;
  std::unordered_map<std::size_t, int> time;
  std::set<T_wait> all_t;
  // 发生变迁的前置集为 0
  for (boost::tie(in_i, in_end) = in_edges(transition.t, time_petri_net); in_i != in_end; ++in_i) {
    vertex_tpn place = source(*in_i, time_petri_net);
    if (time_petri_net[place].token < 0) {
      BOOST_LOG_TRIVIAL(error) << "place token <= 0, the transition not enabled";
    } else {
      time_petri_net[place].token = 0;
    }
  }
  // 发生变迁的后继集为 1
  typename boost::graph_traits<TPN>::out_edge_iterator out_i, out_end;
  for (boost::tie(out_i, out_end) = out_edges(transition.t, time_petri_net); out_i != out_end; ++out_i) {
    vertex_tpn place = target(*out_i, time_petri_net);
    if (time_petri_net[place].token > 1) {
      BOOST_LOG_TRIVIAL(error) << "safe petri net, the place not increment >= 1";
    } else {
      time_petri_net[place].token = 1;
    }
  }
  // 4.获取新的标识
  std::vector<std::size_t> new_enabled_t;
  for (boost::tie(vi, vi_end) = vertices(time_petri_net); vi != vi_end; ++vi) {
    bool enable_t = true;
    if (time_petri_net[*vi].shape == "circle") {
      if (time_petri_net[*vi].token == 1) {
        new_mark.indexes.insert(vertex(*vi, time_petri_net));
        new_mark.labels.insert(time_petri_net[*vi].label);
      }
    } else {
      for (boost::tie(in_i, in_end) = in_edges(*vi, time_petri_net); in_i != in_end; ++in_i) {
        auto source_vertex = source(*in_i, time_petri_net);
        if (time_petri_net[source_vertex].token == 0) {
          enable_t = false;
          break;
        }
      }
      if (enable_t)
        new_enabled_t.push_back(vertex(*vi, time_petri_net));
    }
  }

  // 5. 比较前后使能部分,前一种状态使能,而新状态不使能,则为可挂起变迁
  // 将所有变迁等待时间置为0

  // 5.1 公共部分
  std::set<std::size_t> common;
  std::set_intersection(old_enabled_t.begin(), old_enabled_t.end(), new_enabled_t.begin(), new_enabled_t.end(),
                        std::inserter(common, common.begin()));
  for (unsigned long it : common) {
    all_t.insert({it, time_petri_net[it].pnt.runtime});
//    h_t.insert(it);
//    time.insert(std::make_pair(it, time_petri_net[it].pnt.runtime));
  }
  // 5.2 前状态使能而现状态不使能
  std::set<std::size_t> old_minus_new;
  std::set_difference(old_enabled_t.begin(), old_enabled_t.end(), new_enabled_t.begin(), new_enabled_t.end(),
                      std::inserter(old_minus_new, old_minus_new.begin()));
  for (unsigned long it : old_minus_new) {
    // 若为可挂起变迁,则保存已运行时间
    if (time_petri_net[it].pnt.is_handle) {
      all_t.insert({it, time_petri_net[it].pnt.runtime});
//      H_t.insert(it);
//      time.insert(std::make_pair(it, time_petri_net[it].pnt.runtime));
    }  else {
      time_petri_net[it].pnt.runtime = 0;
    }
  }
  // 5.3 现状态使能而前状态不使能
  std::set<std::size_t> new_minus_old;
  std::set_difference(new_enabled_t.begin(), new_enabled_t.end(),
                      old_enabled_t.begin(), old_enabled_t.end(),
                      std::inserter(new_minus_old, new_minus_old.begin()));
  for (unsigned long it : new_minus_old) {
    if (time_petri_net[it].pnt.is_handle) {
      all_t.insert({it, time_petri_net[it].pnt.runtime});
//      h_t.insert(it);
//      time.insert(std::make_pair(it, time_petri_net[it].pnt.runtime));
    }else {
//      h_t.insert(it);
//      time.insert(std::make_pair(it, 0));
      all_t.insert({it, 0});
    }
  }
  for (auto it = all_t.begin(); it != all_t.end(); ++it) {
    for (auto it2 = std::next(it); it2 != all_t.end(); ) {
      if ((time_petri_net[(*it).t].pnt.priority < time_petri_net[(*it2).t].pnt.priority) &&
          (time_petri_net[(*it).t].pnt.c == time_petri_net[(*it2).t].pnt.c)) {
        if (time_petri_net[(*it2).t].pnt.is_handle) {
          ++it2;
        } else {
          it2 = all_t.erase(it);
        }
      } else {
        // 继续迭代
        ++it2;
      }
    }
  }
//  StateClass new_sc;
//  new_sc.mark = new_mark;
//  new_sc.t_sched = h_t;
//  new_sc.handle_t_sched = H_t;
//  new_sc.t_time = time;
  return {new_mark, all_t};
}

void TimePetriNet::set_state_class(const StateClass& state_class) {
  // 首先重置原有状态
  boost::graph_traits<TPN>::vertex_iterator vi, vi_end;
  for (boost::tie(vi, vi_end) = vertices(time_petri_net); vi != vi_end; ++vi) {
    if (time_petri_net[*vi].shape == "circle") {
      time_petri_net[*vi].token = 0;
    } else {
      time_petri_net[*vi].enabled = false;
      time_petri_net[*vi].pnt.runtime = 0;
    }
  }
  // 2. 设置标识
  for (auto m : state_class.mark.indexes) {
    time_petri_net[m].token = 1;
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
  for (auto t : state_class.all_t) {
    time_petri_net[t.t].pnt.runtime = t.time;
  }
}
