//
// Created by 张凯文 on 2024/6/22.
//

#ifndef PPTPN_INCLUDE_OWNER_ERROR_H
#define PPTPN_INCLUDE_OWNER_ERROR_H
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <iostream>
#include <string>

enum class ParseErrorCodes {
  FileNotFound = 1,
  SyntaxError,
  UnexpectedToken,
  MissingValue,
  UnknownError
};

enum class GenPNErrorCodes { DependencyMissing = 1, LinkerError, UnknownError };

enum class GenStateErrorCodes {
  StateInvalid = 1,
  TransitionError,
  OutputFailed,
  UnknownError
};

class ParseErrorCategory : public boost::system::error_category {
public:
  const char *name() const noexcept override { return "ParseErrorCategory"; }

  std::string message(int ev) const override {
    switch (static_cast<ParseErrorCodes>(ev)) {
    case ParseErrorCodes::FileNotFound:
      return "File not found.";
    case ParseErrorCodes::SyntaxError:
      return "Syntax error.";
    case ParseErrorCodes::UnexpectedToken:
      return "Unexpected token.";
    case ParseErrorCodes::MissingValue:
      return "Missing value.";
    case ParseErrorCodes::UnknownError:
      return "Unknown parse error.";
    default:
      return "Unknown error.";
    }
  }
};

class GenPNErrorCategory : public boost::system::error_category {
public:
  const char *name() const noexcept override { return "BuildErrorCategory"; }

  std::string message(int ev) const override {
    switch (static_cast<GenPNErrorCodes>(ev)) {
    case GenPNErrorCodes::DependencyMissing:
      return "Dependency missing.";
    case GenPNErrorCodes::LinkerError:
      return "Linker error.";
    case GenPNErrorCodes::UnknownError:
      return "Unknown build error.";
    default:
      return "Unknown error.";
    }
  }
};

const boost::system::error_category &parse_error_category() {
  static ParseErrorCategory instance;
  return instance;
}

const boost::system::error_category &gen_pn_error_category() {
  static GenPNErrorCategory instance;
  return instance;
}

// const boost::system::error_category& generation_error_category() {
//   static GenerationErrorCategory instance;
//   return instance;
// }

boost::system::error_code make_error_code(ParseErrorCodes e) {
  return {static_cast<int>(e), parse_error_category()};
}

boost::system::error_code make_error_code(GenPNErrorCodes e) {
  return {static_cast<int>(e), gen_pn_error_category()};
}

// boost::system::error_code make_error_code(GenerationErrorCodes e) {
//   return {static_cast<int>(e), generation_error_category()};
// }

namespace boost::system {
template <> struct is_error_code_enum<ParseErrorCodes> : std::true_type {};

template <> struct is_error_code_enum<GenPNErrorCodes> : std::true_type {};

// template <>
// struct is_error_code_enum<GenerationErrorCodes> : std::true_type {};
} // namespace boost::system

#endif // PPTPN_INCLUDE_OWNER_ERROR_H
