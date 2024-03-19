//
// Created by Kevin on 2023/8/8.
//

#include "GConfig.h"
#include <regex>
#include <sstream>

std::vector<std::string> splitString(const std::string &input, char delimiter)
{
  std::vector<std::string> tokens;
  std::istringstream tokenStream(input);
  std::string token;
  while (std::getline(tokenStream, token, delimiter))
  {
    tokens.push_back(token);
  }
  return tokens;
}

std::vector<int> extractTimeValues(const std::string &timeString)
{
  std::vector<int> values;
  std::regex rgx(R"(\[(\d+),(\d+)\])");
  std::smatch matches;

  std::string::const_iterator start = timeString.begin();
  std::string::const_iterator end = timeString.end();
  while (std::regex_search(start, end, matches, rgx))
  {
    values.push_back(std::stoi(matches[1].str()));
    values.push_back(std::stoi(matches[2].str()));
    start = matches[0].second;
  }
  return values;
}

SubGraphConfig parseSubgraphLabel(const char *label)
{
  std::vector<std::string> parts = splitString(label, ';');
  if (parts.size() == 4)
  {
    SubGraphConfig config;
    config.name = parts[0];
    config.core = std::stoi(parts[1]);
    config.period = std::stoi(parts[3]);

    std::string boolPartStr = parts[2];
    std::transform(boolPartStr.begin(), boolPartStr.end(), boolPartStr.begin(),
                   ::tolower);
    config.is_period = (boolPartStr == "true" ? 1 : 0);
    return config;
  }
  else
  {
    SubGraphConfig config;
    config.name = parts[0];
    config.core = std::stoi(parts[1]);
    config.period = 0;
    config.is_period = false;
    return config;
  }
}

SubTaskConfig GConfig::prase_glb_task_label(const std::string &label)
{
  SubTaskConfig config;
  config.is_sub = false;
  std::regex rgx("\\{(.*?)\\}");
  std::smatch matches;
  if (std::regex_search(label, matches, rgx))
  {
    std::string content = matches[1].str();

    std::vector<std::string> parts;
    std::istringstream ss(content);
    std::string token;
    while (std::getline(ss, token, ';'))
    {
      parts.push_back(token);
    }
    if (parts.size() >= 4)
    {
      config.name = parts[0];
      config.core = std::stoi(parts[1]);
      config.priority = std::stoi(parts[2]);

      std::vector<int> timeValues = extractTimeValues(parts[3]);
      for (size_t i = 0; i < timeValues.size(); i += 2)
      {
        config.time.emplace_back(timeValues[i], timeValues[i + 1]);
      }

      if (parts.size() >= 5)
      {
        std::string locks = parts[4];
        std::istringstream locksStream(locks);
        while (std::getline(locksStream, token, ','))
        {
          config.lock.push_back(token);
          lock_set.insert(token);
        }
      }
    }
  }
  return config;
}

SubTaskConfig GConfig::prase_sub_task_label(const std::string &label)
{
  SubTaskConfig config;
  config.is_sub = true;
  std::regex rgx("\\{(.*?)\\}");
  std::smatch matches;
  if (std::regex_search(label, matches, rgx))
  {
    std::string content = matches[1].str();

    std::vector<std::string> parts;
    std::istringstream ss(content);
    std::string token;
    while (std::getline(ss, token, ';'))
    {
      parts.push_back(token);
    }
    if (parts.size() >= 3)
    {
      config.name = parts[0];
      config.priority = std::stoi(parts[1]);

      std::vector<int> timeValues = extractTimeValues(parts[2]);
      for (size_t i = 0; i < timeValues.size(); i += 2)
      {
        config.time.emplace_back(timeValues[i], timeValues[i + 1]);
      }

      if (parts.size() >= 4)
      {
        std::string locks = parts[3];
        std::istringstream locksStream(locks);
        while (std::getline(locksStream, token, ','))
        {
          config.lock.push_back(token);
          lock_set.insert(token);
        }
      }
    }
  }
  return config;
}

GConfig::GConfig(char *dag_file)
{
  this->dag_file = dag_file;
  init();
  logging::core::get()->set_filter(logging::trivial::severity >=
                                   logging::trivial::info);
}

void GConfig::init()
{
  // Initialize the Graphviz context
  gvc = gvContext();
  dotFile = fopen(dag_file, "r");
  if (!dotFile)
  {
    BOOST_LOG_TRIVIAL(error) << "Error opening DOT file";
    return;
  }
  graph = agread(dotFile, 0);
  fclose(dotFile);
}
void GConfig::prase_dag()
{
  if (graph)
  {
    // Traverse the subgraphs
    Agraph_t *subgraph;
    for (subgraph = agfstsubg(graph); subgraph;
         subgraph = agnxtsubg(subgraph))
    {
      prase_subgraph(subgraph);
      sub_task_collection.push_back(agnameof(subgraph));
    }

    int inSubgraph = 0;
    subgraph = nullptr;
    // Traverse the nodes in the main graph
    Agnode_t *node;
    for (node = agfstnode(graph); node; node = agnxtnode(graph, node))
    {
      inSubgraph = 0;
      for (subgraph = agfstsubg(graph); subgraph;
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
        SubTaskConfig stc = prase_glb_task_label(agget(node, (char *)"label"));
        std::string node_name = agnameof(node);
        if (node_name.find("Wait") != std::string::npos ||
            node_name.find("Distribute") != std::string::npos)
        {
          continue;
        }
        glb_task_collection.push_back(agnameof(node));
        all_stc.push_back(stc);
        tasks_label.insert(std::make_pair(agnameof(node), stc));
      }
    }
  }
}
void GConfig::prase_subgraph(Agraph_t *subgraph)
{
  Agnode_t *node;
  Agedge_t *edge;

  std::string sub_graph_name = std::string(agnameof(subgraph));
  SubGraphConfig sgc = parseSubgraphLabel(agget(subgraph, (char *)"label"));
  sub_graph_config.insert(std::make_pair(sub_graph_name, sgc));
  BOOST_LOG_TRIVIAL(info) << "Subgraph: " << agnameof(subgraph) << sgc.is_period
                          << sgc.period;
  std::vector<std::string> sub_graph_node_names;
  // Traverse the nodes in the subgraph
  for (node = agfstnode(subgraph); node; node = agnxtnode(subgraph, node))
  {
    std::string node_name = std::string(agnameof(node));

    task_where_sub.insert(std::make_pair(node_name, sub_graph_name));
    sub_graph_node_names.push_back(node_name);
    std::string node_label = std::string(agget(node, (char *)"label"));
    SubTaskConfig stc = prase_sub_task_label(node_label);
    stc.core = sgc.core;
    tasks_label.insert(std::make_pair(node_name, stc));
    if (node_name.find("Wait") != std::string::npos ||
        node_name.find("Distribute") != std::string::npos)
    {
      continue;
    }
    all_stc.push_back(stc);
  }
  sub_task_set.insert(std::make_pair(sub_graph_name, sub_graph_node_names));
  // Traverse the edges in the subgraph
  for (node = agfstnode(subgraph); node; node = agnxtnode(subgraph, node))
  {
    for (edge = agfstout(subgraph, node); edge;
         edge = agnxtout(subgraph, edge))
    {
      Agnode_t *targetNode = aghead(edge);
      std::string style = agget(edge, (char *)"style");
      if (style.find("dash") != std::string::npos)
      {
        sub_graph_start_end.insert(
            std::make_pair(sub_graph_name, std::make_pair(agnameof(targetNode),
                                                          agnameof(node))));
        BOOST_LOG_TRIVIAL(debug)
            << "start: " << agnameof(targetNode) << " end: " << agnameof(node);
      }
    }
  }
}
std::map<int, std::vector<SubTaskConfig>> GConfig::classify_priority()
{
  std::map<int, std::vector<SubTaskConfig>> tp;
  for (const auto &t : all_stc)
  {
    // std::cout << "------------core index error ------------" << std::endl;
    int core_index = t.core;
    tp[core_index].push_back(t);
  }
  for (auto &it : tp)
  {
    std::sort(it.second.begin(), it.second.end(),
              [](const SubTaskConfig &task1, const SubTaskConfig &task2)
              {
                return task1.priority < task2.priority;
              });
  }
  return tp;
}
