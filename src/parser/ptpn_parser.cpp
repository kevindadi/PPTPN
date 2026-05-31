#include "parser/ptpn_parser.h"

#include <fstream>
#include <sstream>

namespace parser {

// ========================================
// Skipper for comments and whitespace
// ========================================

static bool is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void skip_whitespace_and_comments(std::string::const_iterator& it,
                                         const std::string::const_iterator& end) {
  while (it != end) {
    // Skip whitespace
    while (it != end && is_whitespace(*it)) {
      ++it;
    }

    // Check for single-line comment
    if (it != end && *it == '/' && it + 1 != end && *(it + 1) == '/') {
      while (it != end && *it != '\n') {
        ++it;
      }
      continue;
    }

    // Check for block comment
    if (it != end && *it == '/' && it + 1 != end && *(it + 1) == '*') {
      it += 2;
      while (it != end && !(*it == '*' && it + 1 != end && *(it + 1) == '/')) {
        ++it;
      }
      if (it != end) it += 2;
      continue;
    }

    break;
  }
}

// ========================================
// Helper functions
// ========================================

static std::string read_identifier(std::string::const_iterator& it,
                                   std::string::const_iterator end) {
  std::string result;
  while (it != end && (std::isalnum(*it) || *it == '_')) {
    result += *it++;
  }
  return result;
}

static int read_number(std::string::const_iterator& it,
                       std::string::const_iterator end) {
  int result = 0;
  while (it != end && std::isdigit(*it)) {
    result = result * 10 + (*it++ - '0');
  }
  return result;
}

// ========================================
// Parser Implementation
// ========================================

bool PTPNParser::parse(const std::string& input, PTPNAST& ast, std::string& error) {
  ast = PTPNAST();

  auto it = input.begin();
  auto end = input.end();

  try {
    skip_whitespace_and_comments(it, end);

    while (it != end) {
      skip_whitespace_and_comments(it, end);
      if (it == end) break;

      // Check for @ directives first
      if (*it == '@') {
        ++it;
        std::string directive = read_identifier(it, end);

        if (directive == "init") {
          skip_whitespace_and_comments(it, end);

          while (it != end && std::isalpha(*it)) {
            InitNode init;
            init.place = read_identifier(it, end);
            if (init.place.empty()) break;

            init.tokens = 1;
            skip_whitespace_and_comments(it, end);

            if (it != end && *it == ':') {
              ++it;
              init.tokens = read_number(it, end);
            }

            ast.initial_marking.push_back(init);
            skip_whitespace_and_comments(it, end);

            if (it != end && *it == ',') {
              ++it;
              skip_whitespace_and_comments(it, end);
            } else {
              break;
            }
          }
        }
        else if (directive == "capacity") {
          skip_whitespace_and_comments(it, end);

          while (it != end && std::isalpha(*it)) {
            std::string place_id = read_identifier(it, end);
            skip_whitespace_and_comments(it, end);

            if (it != end && *it == ':') {
              ++it;
              int cap = read_number(it, end);

              for (auto& p : ast.places) {
                if (p.id == place_id) {
                  p.capacity = cap;
                  break;
                }
              }
            }

            skip_whitespace_and_comments(it, end);
            if (it != end && *it == ',') {
              ++it;
              skip_whitespace_and_comments(it, end);
            } else {
              break;
            }
          }
        }
        continue;
      }

      // Regular keywords and identifiers
      std::string token = read_identifier(it, end);

      if (token == "places") {
        skip_whitespace_and_comments(it, end);

        // Keep parsing places until we hit a section header
        while (it != end && *it != '@') {
          // Stop if we hit transitions keyword
          if (*it == 't' || *it == 'T') {
            auto lookahead = it;
            std::string kw = read_identifier(lookahead, end);
            if (kw == "transitions") break;
          }

          // Skip non-alphabetic characters
          if (!std::isalpha(*it) && *it != '_') {
            ++it;
            skip_whitespace_and_comments(it, end);
            continue;
          }

          PlaceNode place;
          place.capacity = 1;

          place.id = read_identifier(it, end);
          if (place.id.empty()) {
            ++it;
            continue;
          }

          skip_whitespace_and_comments(it, end);

          if (it != end && *it == ':') {
            ++it;
            skip_whitespace_and_comments(it, end);

            if (it != end && std::isdigit(*it)) {
              place.capacity = read_number(it, end);
            } else {
              place.name = read_identifier(it, end);

              skip_whitespace_and_comments(it, end);

              if (it != end && *it == ':') {
                ++it;
                skip_whitespace_and_comments(it, end);
                place.capacity = read_number(it, end);
              }
            }
          }

          ast.places.push_back(place);
          skip_whitespace_and_comments(it, end);

          if (it != end && *it == ',') {
            ++it;
            skip_whitespace_and_comments(it, end);
          }
        }
      }
      else if (token == "transitions") {
        skip_whitespace_and_comments(it, end);

        // Keep parsing transitions
        while (it != end && *it != '@') {
          skip_whitespace_and_comments(it, end);

          // End conditions
          if (it == end) break;
          if (*it == '[') break;  // End of transitions section

          // Stop if we hit an arc (identifier -> identifier)
          if (std::isalpha(*it) || *it == '_') {
            auto lookahead_it = it;
            std::string src = read_identifier(lookahead_it, end);
            skip_whitespace_and_comments(lookahead_it, end);
            if (lookahead_it != end && *lookahead_it == '-' &&
                lookahead_it + 1 != end && *(lookahead_it + 1) == '>') {
              break;
            }
          }

          // Skip non-alphabetic characters except for '[' and '@'
          if (!std::isalpha(*it) && *it != '_' && *it != '[' && *it != '@') {
            ++it;
            continue;
          }

          TransitionNode trans;
          trans.time_min = 0;
          trans.time_max = 0;
          trans.priority = 0;
          trans.core = -1;
          trans.suspendable = false;

          trans.id = read_identifier(it, end);
          if (trans.id.empty()) {
            ++it;
            continue;
          }

          skip_whitespace_and_comments(it, end);

          // Optional :name
          if (it != end && *it == ':') {
            ++it;
            skip_whitespace_and_comments(it, end);
            trans.name = read_identifier(it, end);
            skip_whitespace_and_comments(it, end);
          }

          // Time range [min, max]
          if (it != end && *it == '[') {
            ++it;
            skip_whitespace_and_comments(it, end);
            trans.time_min = read_number(it, end);
            skip_whitespace_and_comments(it, end);

            if (it != end && *it == ',') {
              ++it;
              skip_whitespace_and_comments(it, end);
              trans.time_max = read_number(it, end);
            }

            if (it != end && *it == ']') {
              ++it;
            }
          }

          skip_whitespace_and_comments(it, end);

          // Attributes @priority, @core, and suspendable
          bool done_with_transition = false;
          while (!done_with_transition && it != end) {
            skip_whitespace_and_comments(it, end);
            if (it == end) break;

            if (*it == '@') {
              ++it;
              std::string attr = read_identifier(it, end);

              if (attr == "priority" || attr == "core") {
                if (it != end && *it == '=') ++it;
                int val = read_number(it, end);
                if (attr == "priority") trans.priority = val;
                else trans.core = val;
              }

              // After attribute, check if more attributes follow
              skip_whitespace_and_comments(it, end);
              if (it != end && *it == ',') {
                ++it;  // More attributes, continue loop
              }
              // Else: either @ for next attribute, or done
            }
            else if (std::isalpha(*it)) {
              // Could be "suspendable" or start of next transition
              std::string word;
              auto word_end = it;
              while (word_end != end && std::isalpha(*word_end)) {
                word += *word_end++;
              }

              if (word == "suspendable") {
                trans.suspendable = true;
                it = word_end;
                skip_whitespace_and_comments(it, end);
                break;  // Done with this transition
              } else if (word == "core" || word == "priority") {
                // This is an attribute without @ prefix (e.g., after comma)
                std::string attr = word;
                it = word_end;
                skip_whitespace_and_comments(it, end);
                if (it != end && *it == '=') ++it;
                int val = read_number(it, end);
                if (attr == "priority") trans.priority = val;
                else trans.core = val;
                skip_whitespace_and_comments(it, end);
                if (it != end && *it == ',') {
                  ++it;
                  // Continue to parse next attribute
                }
                // Else: done with this transition
              } else {
                // This is the start of next transition
                done_with_transition = true;
              }
            }
            else {
              // Non-alphanumeric, skip and continue
              ++it;
            }
          }

          ast.transitions.push_back(trans);
        }
      }
      else if (!token.empty()) {
        // Check if this token starts an arc (source -> target)
        std::string src = token;

        skip_whitespace_and_comments(it, end);

        if (it != end && *it == '-' && it + 1 != end && *(it + 1) == '>') {
          it += 2;
          skip_whitespace_and_comments(it, end);

          std::string tgt = read_identifier(it, end);
          ArcNode arc;
          arc.source = src;
          arc.target = tgt;
          arc.weight = 1;

          skip_whitespace_and_comments(it, end);
          if (it != end && *it == ':') {
            ++it;
            arc.weight = read_number(it, end);
          }

          ast.arcs.push_back(arc);
        }
      }
      else {
        // Skip unrecognized character
        ++it;
      }
    }

    return true;
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
}

bool PTPNParser::parse_file(const std::string& filepath, PTPNAST& ast, std::string& error) {
  std::ifstream file(filepath);
  if (!file.is_open()) {
    error = "Cannot open file: " + filepath;
    return false;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return parse(buffer.str(), ast, error);
}

// ========================================
// PTPNBuilder Implementation
// ========================================

std::string PTPNBuilder::error_msg_ = "";

petri::PTPN PTPNBuilder::build(const PTPNAST& ast) {
  petri::PTPN ptpn;

  for (const auto& p : ast.places) {
    std::string name = p.name.empty() ? p.id : p.name;
    ptpn.add_place(name, p.capacity);
  }

  for (const auto& t : ast.transitions) {
    std::string name = t.name.empty() ? t.id : t.name;
    petri::TimeInterval interval(t.time_min, t.time_max);
    ptpn.add_transition(name, interval, t.priority, t.core, t.suspendable);
  }

  for (const auto& arc : ast.arcs) {
    bool src_place = arc.source[0] == 'P';
    bool tgt_place = arc.target[0] == 'P';

    size_t src_idx = std::string::npos;
    size_t tgt_idx = std::string::npos;

    for (size_t i = 0; i < ast.places.size(); i++) {
      if (ast.places[i].id == arc.source) {
        src_idx = i;
        break;
      }
    }
    for (size_t i = 0; i < ast.places.size(); i++) {
      if (ast.places[i].id == arc.target) {
        tgt_idx = i;
        break;
      }
    }
    for (size_t i = 0; i < ast.transitions.size(); i++) {
      if (ast.transitions[i].id == arc.source) {
        src_idx = i;
        break;
      }
    }
    for (size_t i = 0; i < ast.transitions.size(); i++) {
      if (ast.transitions[i].id == arc.target) {
        tgt_idx = i;
        break;
      }
    }

    if (src_place && !tgt_place && src_idx != std::string::npos && tgt_idx != std::string::npos) {
      ptpn.set_pre_arc(src_idx, tgt_idx, arc.weight);
    } else if (!src_place && tgt_place && src_idx != std::string::npos && tgt_idx != std::string::npos) {
      ptpn.set_post_arc(src_idx, tgt_idx, arc.weight);
    }
  }

  petri::Marking marking(ptpn.num_places(), 0);
  for (const auto& init : ast.initial_marking) {
    for (size_t i = 0; i < ast.places.size(); i++) {
      if (ast.places[i].id == init.place) {
        marking[i] = init.tokens;
        break;
      }
    }
  }
  ptpn.set_initial_marking(marking);

  return ptpn;
}

petri::PTPN PTPNBuilder::parse(const std::string& source) {
  PTPNAST ast;
  std::string error;
  if (!PTPNParser::parse(source, ast, error)) {
    error_msg_ = error;
    return petri::PTPN();
  }
  return build(ast);
}

petri::PTPN PTPNBuilder::parse_file(const std::string& filepath) {
  PTPNAST ast;
  std::string error;
  if (!PTPNParser::parse_file(filepath, ast, error)) {
    error_msg_ = error;
    return petri::PTPN();
  }
  return build(ast);
}

// Backward compatibility
petri::PTPN parse_file(const std::string& filepath) {
  return PTPNBuilder::parse_file(filepath);
}

petri::PTPN parse_string(const std::string& source) {
  return PTPNBuilder::parse(source);
}

}  // namespace parser