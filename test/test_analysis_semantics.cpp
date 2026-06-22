#include <gtest/gtest.h>

#include <boost/graph/adjacency_list.hpp>

#include "analysis/ptpn_analysis.h"
#include "petri/petri.h"

namespace {

petri::PTPN make_time_first_net() {
  petri::PTPN ptpn;

  const size_t input = ptpn.add_place("p0", 1);
  const size_t low_done = ptpn.add_place("p1", 1);
  const size_t high_done = ptpn.add_place("p2", 1);
  ptpn.set_initial_marking(input, 1);

  const size_t low_priority =
      ptpn.add_transition("low_priority", petri::TimeInterval(0, 0), 1, 0, false);
  const size_t high_priority =
      ptpn.add_transition("high_priority", petri::TimeInterval(3, 3), 99, 0, false);

  ptpn.set_pre_arc(input, low_priority, 1);
  ptpn.set_post_arc(low_priority, low_done, 1);

  ptpn.set_pre_arc(input, high_priority, 1);
  ptpn.set_post_arc(high_priority, high_done, 1);

  return ptpn;
}

}  // namespace

TEST(PtpnAnalysisSemanticsTest, PicksEarliestFiringTimeBeforePriority) {
  const petri::PTPN ptpn = make_time_first_net();
  state_class::PTPNAnalyzer analyzer(ptpn);

  ASSERT_EQ(analyzer.build(8), 2u);

  const auto& graph = analyzer.get_graph();
  const auto initial = analyzer.get_initial_vertex();

  std::vector<size_t> fired_transitions;
  for (auto [edge_it, edge_end] = boost::out_edges(initial, graph); edge_it != edge_end;
       ++edge_it) {
    const auto& edge = boost::get(boost::edge_name, graph, *edge_it);
    fired_transitions.push_back(static_cast<size_t>(edge.transition_id));
  }

  ASSERT_EQ(fired_transitions.size(), 1u);
  EXPECT_EQ(fired_transitions[0], 0u);
}

TEST(PtpnAnalysisSemanticsTest, OpenLowerBoundDelaysFiringByOneTick) {
  petri::PTPN ptpn;
  const size_t input = ptpn.add_place("p0", 1);
  const size_t done = ptpn.add_place("p1", 1);
  ptpn.set_initial_marking(input, 1);

  const size_t task = ptpn.add_transition(
      "task", petri::TimeInterval(1, 3, true, false), 1, 0, false);
  ptpn.set_pre_arc(input, task, 1);
  ptpn.set_post_arc(task, done, 1);

  state_class::PTPNAnalyzer analyzer(ptpn);
  auto state = analyzer.create_initial_state();
  EXPECT_DOUBLE_EQ(2.0, analyzer.advance_time(state));
}
