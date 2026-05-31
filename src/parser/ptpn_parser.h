#ifndef PARSER_PTPN_PARSER_H
#define PARSER_PTPN_PARSER_H

#include <string>
#include <unordered_map>
#include <vector>

#include "../petri/petri.h"

namespace parser {

enum class TokenType {
  IDENTIFIER,   // P0, T1, places, transitions, etc.
  NUMBER,       // 42, 100

  // Delimiters
  LBRACKET,     // [
  RBRACKET,     // ]
  COMMA,        // ,
  COLON,        // :
  AT,           // @
  ARROW,        // ->
  DOT,          // .

  // Keywords
  PLACES,      // places
  TRANSITIONS,  // transitions
  SUSPENDABLE,  // suspendable
  INIT,         // @init
  CAPACITY,     // @capacity

  // End/Error
  END,          // End of input
  ERROR         // Lexical error
};

struct Token {
  TokenType type;
  std::string value;
  int line;
  int column;

  Token(TokenType t, const std::string& v = "", int l = 1, int c = 1)
      : type(t), value(v), line(l), column(c) {}
};


struct PlaceNode {
  std::string id;
  std::string name;      // optional human-readable name
  int capacity = 1;     // default capacity

  PlaceNode() = default;
  PlaceNode(const std::string& i, const std::string& n = "", int cap = 1)
      : id(i), name(n), capacity(cap) {}
};

struct TransitionNode {
  std::string id;
  std::string name;                    // optional human-readable name
  int time_min = 0;                   // earliest firing time
  int time_max = 0;                    // latest firing time (0 = no upper bound)
  int priority = 0;                   // priority
  int core = -1;                       // core assignment (-1 for control)
  bool suspendable = false;           // can be suspended
  int capacity = 1;                   // capacity (for conflict resolution)

  TransitionNode() = default;
  TransitionNode(const std::string& i, int min, int max)
      : id(i), time_min(min), time_max(max) {}
};

struct ArcNode {
  std::string source;     // Place or Transition ID
  std::string target;     // Place or Transition ID
  int weight = 1;          // arc weight

  ArcNode(const std::string& s, const std::string& t, int w = 1)
      : source(s), target(t), weight(w) {}
};

struct InitNode {
  std::string place;      // Place ID
  int tokens = 1;         // number of tokens

  InitNode(const std::string& p, int t = 1) : place(p), tokens(t) {}
};

// Complete AST
struct PTPNAST {
  std::vector<PlaceNode> places;
  std::vector<TransitionNode> transitions;
  std::vector<ArcNode> arcs;
  std::vector<InitNode> initial_marking;

  // Lookups
  std::unordered_map<std::string, size_t> place_index;
  std::unordered_map<std::string, size_t> transition_index;

  bool has_place(const std::string& id) const {
    return place_index.find(id) != place_index.end();
  }
  bool has_transition(const std::string& id) const {
    return transition_index.find(id) != transition_index.end();
  }
  bool has_element(const std::string& id) const {
    return has_place(id) || has_transition(id);
  }

  void build_indexes() {
    for (size_t i = 0; i < places.size(); ++i) {
      place_index[places[i].id] = i;
    }
    for (size_t i = 0; i < transitions.size(); ++i) {
      transition_index[transitions[i].id] = i;
    }
  }
};

class Lexer {
 public:
  explicit Lexer(const std::string& input);

  Token current() const { return (tokens_.empty() || pos_ >= tokens_.size()) ? Token(TokenType::END) : tokens_[pos_]; }
  Token peek(int ahead = 1) const;
  void advance();
  bool is_at_end() const { return tokens_.empty() || pos_ >= tokens_.size(); }

  // Error handling
  std::string error_message() const { return error_msg_; }
  bool has_error() const { return !error_msg_.empty(); }

 private:
  void tokenize();
  void skip_whitespace();
  void skip_single_line_comment();
  void skip_multi_line_comment();
  char peek_char(int ahead = 0) const;
  char next_char();
  void add_token(TokenType type, const std::string& value = "");

  std::string read_identifier();
  int read_number();

  static TokenType keyword_lookup(const std::string& word);
  void report_error(const std::string& msg);

  static bool is_whitespace(char c);
  static bool is_alpha(char c);
  static bool is_digit(char c);
  static bool is_alnum(char c);

  std::string input_;
  size_t input_pos_ = 0;
  size_t pos_ = 0;
  size_t token_pos_ = 0;
  size_t start_pos_ = 0;
  int line_ = 1;
  int column_ = 1;
  int token_start_column_ = 1;
  std::vector<Token> tokens_;
  std::string error_msg_;
};

class Parser {
 public:
  explicit Parser(const std::string& input);
  explicit Parser(const Lexer& lexer);

  PTPNAST parse();

  // Error handling
  std::string error_message() const { return error_msg_; }
  bool has_error() const { return !error_msg_.empty(); }
  int error_line() const { return error_line_; }

 private:
  void init_lexer();

  // Parsing functions
  void parse_places();
  void parse_transitions();
  void parse_arcs();
  void parse_directives();

  PlaceNode parse_place();
  TransitionNode parse_transition();
  ArcNode parse_arc();
  InitNode parse_init_entry();
  std::pair<std::string, int> parse_capacity_entry();

  std::string parse_identifier();
  int parse_int();

  // Utilities
  void skip_newlines();
  bool match(TokenType type);
  bool match_keyword(const std::string& keyword);
  Token expect(TokenType type, const std::string& error_msg);
  std::string expect_identifier(const std::string& error_msg);

  void report_error(const std::string& msg, int line = -1);

  std::unique_ptr<Lexer> owned_lexer_;  // Must be declared BEFORE lexer_
  Lexer* lexer_ = nullptr; // Initialized AFTER owned_lexer_

  PTPNAST ast_;
  std::string error_msg_;
  int error_line_ = -1;
};

class PTPNBuilder {
 public:
  static petri::PTPN build(const PTPNAST& ast);
  static petri::PTPN parse(const std::string& source);

  static std::string error_message() { return error_msg_; }
  static bool has_error() { return !error_msg_.empty(); }

 private:
  static void add_places(const PTPNAST& ast, petri::PTPN& ptpn);
  static void add_transitions(const PTPNAST& ast, petri::PTPN& ptpn);
  static void add_arcs(const PTPNAST& ast, petri::PTPN& ptpn);
  static void set_initial_marking(const PTPNAST& ast, petri::PTPN& ptpn);

  static std::string error_msg_;
};

petri::PTPN parse_file(const std::string& filepath);
petri::PTPN parse_string(const std::string& source);

}  // namespace parser

#endif  // PARSER_PTPN_PARSER_H