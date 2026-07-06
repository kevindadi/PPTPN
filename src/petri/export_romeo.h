#ifndef PETRI_EXPORT_ROMEO_H
#define PETRI_EXPORT_ROMEO_H

#include <string>

#include "petri/export_ptpn.h"

namespace petri::exporting {

std::string render_romeo_cts(const PetriExportModel& model);
bool save_to_romeo_cts(const PetriExportModel& model, const std::string& file_path);

}  // namespace petri::exporting

#endif  // PETRI_EXPORT_ROMEO_H
