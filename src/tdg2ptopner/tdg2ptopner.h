#ifndef TDG2PTOPNER_H
#define TDG2PTOPNER_H

#include <string>

#include "../tdg/tdg.h"
#include "validate.h"

namespace ptopner_export {

struct Tdg2PtopnerResult {
  bool success = false;
  PtopnerValidationResult validation;
  std::string error_message;
};

Tdg2PtopnerResult transform_to_ppn_file(const tdg::TDG& tdg, const std::string& output_path);

}  // namespace ptopner_export

#endif  // TDG2PTOPNER_H
