////
//// Created by Kevin on 2023/8/7.
////
// #include <boost/foreach.hpp>
// #include <boost/graph/adjacency_list.hpp>
// #include <boost/graph/graphviz.hpp>
// #include <boost/property_tree/json_parser.hpp>
// #include <boost/property_tree/ptree.hpp>
// #include <fstream>
// #include <iostream>
// #include <string>
//
// typedef boost::adjacency_list<
//     boost::vecS, boost::vecS, boost::directedS,
//     boost::property<boost::vertex_name_t, std::string>,
//     boost::property<boost::edge_name_t, std::string>>
//     Graph;
//
// typedef boost::graph_traits<Graph>::vertex_descriptor Vertex;
// typedef boost::graph_traits<Graph>::edge_descriptor Edge;
//
// int main() {
//   // Load the DOT file into a Boost Graph
//   Graph g;
//   std::ifstream dotFile("example.dot");
//   boost::dynamic_properties dp;
//   dp.property("node_id", boost::get(boost::vertex_name, g));
//   dp.property("label", boost::get(boost::edge_name, g));
//   boost::read_graphviz(dotFile, g, dp);
//
//   // Extract task and subtask information
//   std::map<std::string, std::map<std::string, std::string>> tasks;
//   BOOST_FOREACH (Vertex v, boost::vertices(g)) {
//     if (g[v].find("label") != g[v].end()) {
//       // This is a subgraph representing a task
//       std::string taskName = g[v][boost::vertex_name];
//       tasks[taskName]["label"] = g[v]["label"];
//       BOOST_FOREACH (Vertex u, boost::adjacent_vertices(v, g)) {
//         if (g[u].find("label") != g[u].end()) {
//           // This is a vertex representing a subtask
//           std::string subtaskName = g[u][boost::vertex_name];
//           tasks[taskName][subtaskName] = g[u]["label"];
//         }
//       }
//     }
//   }
//
//   // Print the extracted task and subtask information
//   boost::property_tree::ptree pt;
//   BOOST_FOREACH (auto &task, tasks) {
//     boost::property_tree::ptree taskNode;
//     taskNode.put("label", task.second["label"]);
//     BOOST_FOREACH (auto &subtask, task.second) {
//       if (subtask.first != "label") {
//         boost::property_tree::ptree subtaskNode;
//         subtaskNode.put("label", subtask.second);
//         taskNode.add_child(subtask.first, subtaskNode);
//       }
//     }
//     pt.add_child(task.first, taskNode);
//   }
//   std::stringstream ss;
//   boost::property_tree::write_json(ss, pt);
//   std::cout << ss.str() << std::endl;
//
//   return 0;
// }
