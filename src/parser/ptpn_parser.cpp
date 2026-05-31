#include "parser/ptpn_parser.h"

#include <fstream>
#include <sstream>

namespace parser {

Lexer::Lexer(const std::string& input) : input_(input) {
  tokenize();
}

void Lexer::tokenize() {
  while (input_pos_ < input_.size()) {
    skip_whitespace();
    if (input_pos_ >= input_.size()) break;

    start_pos_ = input_pos_;
    token_start_column_ = column_;

    char c = peek_char();

    // Single-line comment
    if (c == '/' && peek_char(1) == '/') {
      skip_single_line_comment();
      continue;
    }

    // Multi-line comment
    if (c == '/' && peek_char(1) == '*') {
      skip_multi_line_comment();
      continue;
    }

    // Number
    if (is_digit(c)) {
      int num = read_number();
      add_token(TokenType::NUMBER, std::to_string(num));
      continue;
    }

    // Identifier or keyword
    if (is_alpha(c) || c == '_') {
      std::string id = read_identifier();
      TokenType type = keyword_lookup(id);
      if (type != TokenType::IDENTIFIER) {
        add_token(type, id);
      } else {
        add_token(TokenType::IDENTIFIER, id);
      }
      continue;
    }

    // Single-character tokens
    switch (c) {
      case '[': next_char(); add_token(TokenType::LBRACKET); break;
      case ']': next_char(); add_token(TokenType::RBRACKET); break;
      case ',': next_char(); add_token(TokenType::COMMA); break;
      case ':': next_char(); add_token(TokenType::COLON); break;
      case '@': next_char(); add_token(TokenType::AT); break;
      case '.': next_char(); add_token(TokenType::DOT); break;
      case '-':
        if (peek_char(1) == '>') {
          next_char(); next_char();
          add_token(TokenType::ARROW);
        } else {
          report_error("Unexpected character '-'");
        }
        break;
      default:
        report_error(std::string("Unexpected character: '") + c + "'");
        next_char();
        add_token(TokenType::ERROR);
    }
  }

  add_token(TokenType::END);
}

Token Lexer::peek(int ahead) const {
  size_t idx = token_pos_ + ahead - 1;
  if (idx < tokens_.size()) {
    return tokens_[idx];
  }
  return Token(TokenType::END);
}

void Lexer::advance() {
  if (token_pos_ < tokens_.size()) {
    token_pos_++;
  }
}

void Lexer::skip_whitespace() {
  while (input_pos_ < input_.size() && is_whitespace(peek_char())) {
    if (peek_char() == '\n') {
      line_++;
      column_ = 1;
    } else {
      column_++;
    }
    input_pos_++;
  }
}

void Lexer::skip_single_line_comment() {
  while (input_pos_ < input_.size() && peek_char() != '\n') {
    input_pos_++;
    column_++;
  }
}

void Lexer::skip_multi_line_comment() {
  // Skip /*
  input_pos_ += 2;
  column_ += 2;

  while (input_pos_ < input_.size()) {
    if (peek_char() == '*' && peek_char(1) == '/') {
      input_pos_ += 2;
      column_ += 2;
      break;
    }
    if (peek_char() == '\n') {
      line_++;
      column_ = 1;
    }
    input_pos_++;
    column_++;
  }
}

char Lexer::peek_char(int ahead) const {
  size_t idx = input_pos_ + ahead;
  if (idx < input_.size()) {
    return input_[idx];
  }
  return '\0';
}

char Lexer::next_char() {
  char c = input_[input_pos_];
  input_pos_++;
  column_++;
  return c;
}

void Lexer::add_token(TokenType type, const std::string& value) {
  tokens_.emplace_back(type, value, line_, token_start_column_);
}

void Lexer::report_error(const std::string& msg) {
  if (error_msg_.empty()) {
    error_msg_ = "Lexer error at line " + std::to_string(line_) +
                 ", column " + std::to_string(column_) + ": " + msg;
  }
}

std::string Lexer::read_identifier() {
  std::string result;
  while (input_pos_ < input_.size() && is_alnum(peek_char())) {
    result += next_char();
  }
  return result;
}

int Lexer::read_number() {
  std::string result;
  while (input_pos_ < input_.size() && is_digit(peek_char())) {
    result += next_char();
  }
  return std::stoi(result);
}

bool Lexer::is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool Lexer::is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool Lexer::is_digit(char c) {
  return c >= '0' && c <= '9';
}

bool Lexer::is_alnum(char c) {
  return is_alpha(c) || is_digit(c) || c == '_';
}

TokenType Lexer::keyword_lookup(const std::string& word) {
  if (word == "places") return TokenType::PLACES;
  if (word == "transitions") return TokenType::TRANSITIONS;
  if (word == "suspendable") return TokenType::SUSPENDABLE;
  if (word == "init") return TokenType::INIT;
  if (word == "capacity") return TokenType::CAPACITY;
  return TokenType::IDENTIFIER;
}

// ========================================
// Parser Implementation
// ========================================

Parser::Parser(const std::string& input)
    : owned_lexer_(new Lexer(input)), lexer_(owned_lexer_.get()) {}

Parser::Parser(const Lexer& lexer) : lexer_(const_cast<Lexer*>(&lexer)) {}

void Parser::init_lexer() {
  // Already initialized in constructor
}

PTPNAST Parser::parse() {
  ast_ = PTPNAST();

  while (!lexer_->is_at_end()) {
    Token curr = lexer_->current();

    if (curr.type == TokenType::PLACES) {
      lexer_->advance();
      parse_places();
    } else if (curr.type == TokenType::TRANSITIONS) {
      lexer_->advance();
      parse_transitions();
    } else if (curr.type == TokenType::AT) {
      lexer_->advance();
      parse_directives();
    } else if (curr.type == TokenType::IDENTIFIER) {
      // Could be an arc: P0 -> T0
      parse_arcs();
    } else {
      report_error("Unexpected token: " + curr.value);
      lexer_->advance();
    }
  }

  ast_.build_indexes();
  return ast_;
}

void Parser::parse_places() {
  while (!lexer_->is_at_end()) {
    Token curr = lexer_->current();

    // End of places section (new keyword)
    if (curr.type == TokenType::TRANSITIONS ||
        curr.type == TokenType::AT) {
      break;
    }

    if (curr.type == TokenType::IDENTIFIER) {
      PlaceNode place = parse_place();
      ast_.places.push_back(place);
    } else if (curr.type == TokenType::COMMA) {
      lexer_->advance();
    } else if (curr.type == TokenType::NUMBER) {
      report_error("Expected identifier for place, got number");
      break;
    } else {
      // Skip unknown tokens (newlines, etc.)
      lexer_->advance();
    }
  }
}

PlaceNode Parser::parse_place() {
  std::string id = parse_identifier();

  PlaceNode place(id);

  // Check for optional name and capacity
  if (lexer_->current().type == TokenType::COLON) {
    lexer_->advance();  // consume ':'

    Token next = lexer_->current();
    if (next.type == TokenType::NUMBER) {
      // Capacity only: P0:1
      place.capacity = parse_int();
    } else if (next.type == TokenType::IDENTIFIER) {
      std::string name_or_cap = parse_identifier();

      if (lexer_->current().type == TokenType::COLON) {
        lexer_->advance();
        place.name = name_or_cap;
        place.capacity = parse_int();
      } else {
        // Could be name or capacity - try as capacity first if it's a number-like identifier
        place.name = name_or_cap;
      }
    }
  }

  return place;
}

void Parser::parse_transitions() {
  while (!lexer_->is_at_end()) {
    Token curr = lexer_->current();

    // End of transitions section (new keyword)
    if (curr.type == TokenType::PLACES ||
        curr.type == TokenType::AT) {
      break;
    }

    if (curr.type == TokenType::IDENTIFIER || curr.type == TokenType::NUMBER) {
      TransitionNode trans = parse_transition();
      ast_.transitions.push_back(trans);
    } else {
      // Skip unknown tokens
      lexer_->advance();
    }
  }
}

TransitionNode Parser::parse_transition() {
  std::string id = parse_identifier();

  TransitionNode trans(id, 0, 0);

  // Check for name
  if (lexer_->current().type == TokenType::COLON) {
    lexer_->advance();
    trans.name = parse_identifier();
  }

  // Parse time range: [min, max]
  if (lexer_->current().type == TokenType::LBRACKET) {
    lexer_->advance();

    trans.time_min = parse_int();
    expect(TokenType::COMMA, "Expected ',' in time range");
    lexer_->advance();  // consume comma

    trans.time_max = parse_int();

    expect(TokenType::RBRACKET, "Expected ']' after time range");
    // Note: no advance here, let the main loop handle it
  }

  // Parse attributes: @priority=N, @core=N, suspendable
  while (lexer_->current().type == TokenType::AT ||
         lexer_->current().type == TokenType::IDENTIFIER) {

    if (lexer_->current().type == TokenType::IDENTIFIER &&
        lexer_->current().value == "suspendable") {
      lexer_->advance();
      trans.suspendable = true;
      continue;
    }

    if (lexer_->current().type == TokenType::AT) {
      lexer_->advance();

      Token attr = lexer_->current();
      if (attr.type == TokenType::IDENTIFIER) {
        std::string attr_name = attr.value;
        lexer_->advance();

        if (lexer_->current().type == TokenType::COLON) {
          lexer_->advance();
          int val = parse_int();

          if (attr_name == "priority") {
            trans.priority = val;
          } else if (attr_name == "core") {
            trans.core = val;
          } else if (attr_name == "capacity") {
            trans.capacity = val;
          }
        } else if (attr_name == "priority") {
          // @priority shorthand: @N
          trans.priority = std::stoi(attr.value);
        } else if (attr_name == "core") {
          trans.core = std::stoi(attr.value);
        }
      }
    } else if (lexer_->current().type == TokenType::IDENTIFIER) {
      // Could be shorthand like @97
      std::string word = lexer_->current().value;
      if (word == "suspendable") {
        lexer_->advance();
        trans.suspendable = true;
      } else {
        break;
      }
    } else {
      break;
    }
  }

  // Handle comma separator
  if (lexer_->current().type == TokenType::COMMA) {
    lexer_->advance();
  }

  return trans;
}

void Parser::parse_arcs() {
  while (!lexer_->is_at_end()) {
    Token curr = lexer_->current();

    // Check if this looks like an arc (identifier -> ...)
    if (curr.type != TokenType::IDENTIFIER) {
      break;
    }

    // Look ahead to check for arrow
    bool has_arrow = false;
    for (size_t i = 1; i < 10 && !lexer_->is_at_end(); i++) {
      Token peeked = lexer_->peek(i);
      if (peeked.type == TokenType::ARROW) {
        has_arrow = true;
        break;
      }
      if (peeked.type != TokenType::COLON &&
          peeked.type != TokenType::NUMBER &&
          peeked.type != TokenType::COMMA) {
        break;
      }
    }

    if (!has_arrow) {
      break;
    }

    ArcNode arc = parse_arc();
    ast_.arcs.push_back(arc);
  }
}

ArcNode Parser::parse_arc() {
  std::string source = parse_identifier();

  expect(TokenType::ARROW, "Expected '->' in arc");
  lexer_->advance();

  std::string target = parse_identifier();

  ArcNode arc(source, target, 1);

  // Optional weight
  if (lexer_->current().type == TokenType::COLON) {
    lexer_->advance();
    arc.weight = parse_int();
  }

  return arc;
}

void Parser::parse_directives() {
  Token directive = lexer_->current();

  if (directive.type == TokenType::INIT) {
    lexer_->advance();

    // Parse: P0:1, P1:2
    while (!lexer_->is_at_end()) {
      skip_newlines();

      Token curr = lexer_->current();
      if (curr.type == TokenType::END ||
          curr.type == TokenType::PLACES ||
          curr.type == TokenType::TRANSITIONS) {
        break;
      }

      InitNode init = parse_init_entry();
      ast_.initial_marking.push_back(init);

      if (lexer_->current().type == TokenType::COMMA) {
        lexer_->advance();
      } else {
        break;
      }
    }
  } else if (directive.type == TokenType::CAPACITY) {
    lexer_->advance();

    // Parse: P0:1, P1:2
    while (!lexer_->is_at_end()) {
      skip_newlines();

      Token curr = lexer_->current();
      if (curr.type == TokenType::END ||
          curr.type == TokenType::PLACES ||
          curr.type == TokenType::TRANSITIONS) {
        break;
      }

      auto entry = parse_capacity_entry();

      // Update place capacity
      for (auto& place : ast_.places) {
        if (place.id == entry.first) {
          place.capacity = entry.second;
          break;
        }
      }

      if (lexer_->current().type == TokenType::COMMA) {
        lexer_->advance();
      } else {
        break;
      }
    }
  } else {
    report_error("Unknown directive: " + directive.value);
    lexer_->advance();
  }
}

InitNode Parser::parse_init_entry() {
  std::string place = parse_identifier();

  expect(TokenType::COLON, "Expected ':' in init entry");
  lexer_->advance();

  int tokens = parse_int();

  return InitNode(place, tokens);
}

std::pair<std::string, int> Parser::parse_capacity_entry() {
  std::string place = parse_identifier();

  expect(TokenType::COLON, "Expected ':' in capacity entry");
  lexer_->advance();

  int cap = parse_int();

  return {place, cap};
}

std::string Parser::parse_identifier() {
  Token curr = lexer_->current();
  if (curr.type != TokenType::IDENTIFIER) {
    report_error("Expected identifier, got: " + curr.value);
    lexer_->advance();
    return "";
  }
  std::string id = curr.value;
  lexer_->advance();
  return id;
}

int Parser::parse_int() {
  Token curr = lexer_->current();
  if (curr.type != TokenType::NUMBER) {
    report_error("Expected number, got: " + curr.value);
    lexer_->advance();
    return 0;
  }
  int val = std::stoi(curr.value);
  lexer_->advance();
  return val;
}

void Parser::skip_newlines() {
  // Lexer already skips whitespace, so this is a no-op
  // but we use it as a synchronization point
}

bool Parser::match(TokenType type) {
  if (lexer_->current().type == type) {
    lexer_->advance();
    return true;
  }
  return false;
}

bool Parser::match_keyword(const std::string& keyword) {
  Token curr = lexer_->current();
  if (curr.type == TokenType::IDENTIFIER && curr.value == keyword) {
    lexer_->advance();
    return true;
  }
  return false;
}

Token Parser::expect(TokenType type, const std::string& error_msg) {
  Token curr = lexer_->current();
  if (curr.type != type) {
    report_error(error_msg + " (got " + curr.value + ")");
  }
  return curr;
}

std::string Parser::expect_identifier(const std::string& error_msg) {
  return parse_identifier();
}

void Parser::report_error(const std::string& msg, int line) {
  if (error_msg_.empty()) {
    error_msg_ = "Parser error";
    if (line > 0 || error_line_ > 0) {
      error_msg_ += " at line " + std::to_string(line > 0 ? line : error_line_);
    }
    error_msg_ += ": " + msg;
  }
}

// ========================================
// PTPNBuilder Implementation
// ========================================

std::string PTPNBuilder::error_msg_ = "";

petri::PTPN PTPNBuilder::build(const PTPNAST& ast) {
  petri::PTPN ptpn;

  add_places(ast, ptpn);
  add_transitions(ast, ptpn);
  add_arcs(ast, ptpn);
  set_initial_marking(ast, ptpn);

  return ptpn;
}

petri::PTPN PTPNBuilder::parse(const std::string& source) {
  Parser parser(source);
  PTPNAST ast = parser.parse();

  if (parser.has_error()) {
    error_msg_ = parser.error_message();
    return petri::PTPN();
  }

  return build(ast);
}

void PTPNBuilder::add_places(const PTPNAST& ast, petri::PTPN& ptpn) {
  for (const auto& place : ast.places) {
    ptpn.add_place(place.name.empty() ? place.id : place.name, place.capacity);
  }
}

void PTPNBuilder::add_transitions(const PTPNAST& ast, petri::PTPN& ptpn) {
  for (const auto& trans : ast.transitions) {
    petri::TimeInterval interval(trans.time_min,
                                  trans.time_max == 0 ? petri::INF : trans.time_max);
    size_t idx = ptpn.add_transition(
        trans.name.empty() ? trans.id : trans.name,
        interval,
        trans.priority,
        trans.core,
        trans.suspendable);
    (void)idx;  // suppress unused warning
  }
}

void PTPNBuilder::add_arcs(const PTPNAST& ast, petri::PTPN& ptpn) {
  for (const auto& arc : ast.arcs) {
    // Determine source and target types
    bool source_is_place = arc.source[0] == 'P';
    bool target_is_place = arc.target[0] == 'P';

    size_t place_idx, trans_idx;

    if (source_is_place) {
      place_idx = ast.place_index.at(arc.source);
    } else {
      trans_idx = ast.transition_index.at(arc.source);
    }

    if (target_is_place) {
      place_idx = ast.place_index.at(arc.target);
    } else {
      trans_idx = ast.transition_index.at(arc.target);
    }

    // Place -> Transition: Pre arc
    if (source_is_place && !target_is_place) {
      size_t p = ast.place_index.at(arc.source);
      size_t t = ast.transition_index.at(arc.target);
      ptpn.set_pre_arc(p, t, arc.weight);
    }
    // Transition -> Place: Post arc
    else if (!source_is_place && target_is_place) {
      size_t t = ast.transition_index.at(arc.source);
      size_t p = ast.place_index.at(arc.target);
      ptpn.set_post_arc(t, p, arc.weight);
    }
    // Place -> Place (shouldn't happen in standard PTPN)
    // Transition -> Transition (shouldn't happen)
    else {
      error_msg_ = "Invalid arc: " + arc.source + " -> " + arc.target;
    }
  }
}

void PTPNBuilder::set_initial_marking(const PTPNAST& ast, petri::PTPN& ptpn) {
  petri::Marking marking(ptpn.num_places(), 0);

  for (const auto& init : ast.initial_marking) {
    if (ast.place_index.find(init.place) != ast.place_index.end()) {
      size_t idx = ast.place_index.at(init.place);
      marking[idx] = init.tokens;
    }
  }

  ptpn.set_initial_marking(marking);
}

// ========================================
// Convenience Functions
// ========================================

petri::PTPN parse_file(const std::string& filepath) {
  std::ifstream file(filepath);
  if (!file.is_open()) {
    return petri::PTPN();
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();

  return parse_string(content);
}

petri::PTPN parse_string(const std::string& source) {
  return PTPNBuilder::parse(source);
}

}  // namespace parser