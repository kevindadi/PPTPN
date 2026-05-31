#include <gtest/gtest.h>
#include "parser/ptpn_parser.h"

namespace parser {

// Test fixture for PTPN parser tests
class PTPNParserTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

TEST_F(PTPNParserTest, ParsePlaces) {
  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse("places P0, P1, P2", ast, error);

  EXPECT_TRUE(result);
  EXPECT_EQ(3, ast.places.size());
  EXPECT_EQ("P0", ast.places[0].id);
  EXPECT_EQ("P1", ast.places[1].id);
  EXPECT_EQ("P2", ast.places[2].id);
}

TEST_F(PTPNParserTest, ParsePlacesWithName) {
  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse("places P0:Start, P1:Buffer", ast, error);

  EXPECT_TRUE(result);
  EXPECT_EQ(2, ast.places.size());
  EXPECT_EQ("P0", ast.places[0].id);
  EXPECT_EQ("Start", ast.places[0].name);
  EXPECT_EQ("P1", ast.places[1].id);
  EXPECT_EQ("Buffer", ast.places[1].name);
}

TEST_F(PTPNParserTest, ParsePlacesWithCapacity) {
  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse("places P0:1, P1:5, P2:10", ast, error);

  EXPECT_TRUE(result);
  EXPECT_EQ(3, ast.places.size());
  EXPECT_EQ(1, ast.places[0].capacity);
  EXPECT_EQ(5, ast.places[1].capacity);
  EXPECT_EQ(10, ast.places[2].capacity);
}

TEST_F(PTPNParserTest, ParsePlacesWithNameAndCapacity) {
  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse("places P0:Start:1, P1:Buffer:5", ast, error);

  EXPECT_TRUE(result);
  EXPECT_EQ(2, ast.places.size());
  EXPECT_EQ("P0", ast.places[0].id);
  EXPECT_EQ("Start", ast.places[0].name);
  EXPECT_EQ(1, ast.places[0].capacity);
  EXPECT_EQ("P1", ast.places[1].id);
  EXPECT_EQ("Buffer", ast.places[1].name);
  EXPECT_EQ(5, ast.places[1].capacity);
}

TEST_F(PTPNParserTest, ParseTransitions) {
  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse("transitions T0 [0, 0]", ast, error);

  EXPECT_TRUE(result);
  EXPECT_EQ(1, ast.transitions.size());
  EXPECT_EQ("T0", ast.transitions[0].id);
  EXPECT_EQ(0, ast.transitions[0].time_min);
  EXPECT_EQ(0, ast.transitions[0].time_max);
}

TEST_F(PTPNParserTest, ParseTransitionsWithAttributes) {
  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse("transitions T0 [1, 5] @priority=10 @core=2", ast, error);

  EXPECT_TRUE(result);
  EXPECT_EQ(1, ast.transitions.size());
  EXPECT_EQ("T0", ast.transitions[0].id);
  EXPECT_EQ(1, ast.transitions[0].time_min);
  EXPECT_EQ(5, ast.transitions[0].time_max);
  EXPECT_EQ(10, ast.transitions[0].priority);
  EXPECT_EQ(2, ast.transitions[0].core);
}

TEST_F(PTPNParserTest, ParseTransitionsWithSuspendable) {
  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse("transitions T0 [3, 8] suspendable", ast, error);

  EXPECT_TRUE(result);
  EXPECT_EQ(1, ast.transitions.size());
  EXPECT_EQ("T0", ast.transitions[0].id);
  EXPECT_EQ(3, ast.transitions[0].time_min);
  EXPECT_EQ(8, ast.transitions[0].time_max);
  EXPECT_TRUE(ast.transitions[0].suspendable);
}

TEST_F(PTPNParserTest, ParseArcs) {
  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse("P0 -> T0\nT0 -> P1", ast, error);

  EXPECT_TRUE(result);
  EXPECT_EQ(2, ast.arcs.size());
  EXPECT_EQ("P0", ast.arcs[0].source);
  EXPECT_EQ("T0", ast.arcs[0].target);
  EXPECT_EQ("T0", ast.arcs[1].source);
  EXPECT_EQ("P1", ast.arcs[1].target);
}

TEST_F(PTPNParserTest, ParseArcsWithWeight) {
  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse("P0 -> T0:2", ast, error);

  EXPECT_TRUE(result);
  EXPECT_EQ(1, ast.arcs.size());
  EXPECT_EQ("P0", ast.arcs[0].source);
  EXPECT_EQ("T0", ast.arcs[0].target);
  EXPECT_EQ(2, ast.arcs[0].weight);
}

TEST_F(PTPNParserTest, ParseInit) {
  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse("@init P0:1", ast, error);

  EXPECT_TRUE(result);
  EXPECT_EQ(1, ast.initial_marking.size());
  EXPECT_EQ("P0", ast.initial_marking[0].place);
  EXPECT_EQ(1, ast.initial_marking[0].tokens);
}

TEST_F(PTPNParserTest, ParseInitMultiple) {
  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse("@init P0:1, P1:2", ast, error);

  EXPECT_TRUE(result);
  EXPECT_EQ(2, ast.initial_marking.size());
  EXPECT_EQ("P0", ast.initial_marking[0].place);
  EXPECT_EQ(1, ast.initial_marking[0].tokens);
  EXPECT_EQ("P1", ast.initial_marking[1].place);
  EXPECT_EQ(2, ast.initial_marking[1].tokens);
}

TEST_F(PTPNParserTest, ParseCompletePTPN) {
  std::string content = R"(
places
    P0: Start:1
    P1: Running:1

transitions
    T0 [0, 0] @priority=0, core=-1
    T1 [3, 8] @priority=97, core=0, suspendable

P0 -> T0
T0 -> P1
P1 -> T1

@init P0:1
)";

  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse(content, ast, error);

  EXPECT_TRUE(result) << "Parse failed: " << error;
  EXPECT_EQ(2, ast.places.size());
  EXPECT_EQ(2, ast.transitions.size());
  EXPECT_EQ(3, ast.arcs.size());
  EXPECT_EQ(1, ast.initial_marking.size());
}

TEST_F(PTPNParserTest, ParseWithComments) {
  std::string content = R"(
// This is a comment
places P0, P1 // inline comment
/* block comment */
transitions T0 [0, 0]
P0 -> T0
T0 -> P1
)";

  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse(content, ast, error);

  EXPECT_TRUE(result) << "Parse failed: " << error;
  EXPECT_EQ(2, ast.places.size());
  EXPECT_EQ(1, ast.transitions.size());
  EXPECT_EQ(2, ast.arcs.size());
}

TEST_F(PTPNParserTest, BuildPTPNFromAST) {
  std::string content = R"(
places P0, P1
transitions T0 [0, 0]
P0 -> T0
T0 -> P1
@init P0:1
)";

  petri::PTPN ptpn = PTPNBuilder::parse(content);

  EXPECT_EQ(2, ptpn.num_places());
  EXPECT_EQ(1, ptpn.num_transitions());
}

TEST_F(PTPNParserTest, ParseEmptyInput) {
  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse("", ast, error);

  EXPECT_TRUE(result);
}

TEST_F(PTPNParserTest, ParseMultipleTransitions) {
  std::string content = R"(
transitions
    T0 [0, 0] @priority=0, core=-1
    T1 [1, 5] @priority=50, core=0
    T2 [10, 100] suspendable
)";

  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse(content, ast, error);

  EXPECT_TRUE(result) << "Parse failed: " << error;
  EXPECT_EQ(3, ast.transitions.size());
  EXPECT_EQ("T0", ast.transitions[0].id);
  EXPECT_EQ("T1", ast.transitions[1].id);
  EXPECT_EQ("T2", ast.transitions[2].id);
  EXPECT_TRUE(ast.transitions[2].suspendable);
}

TEST_F(PTPNParserTest, ParseTransitionWithName) {
  PTPNAST ast;
  std::string error;
  bool result = PTPNParser::parse("transitions T0:Task1 [1, 10]", ast, error);

  EXPECT_TRUE(result);
  EXPECT_EQ(1, ast.transitions.size());
  EXPECT_EQ("T0", ast.transitions[0].id);
  EXPECT_EQ("Task1", ast.transitions[0].name);
}

}  // namespace parser