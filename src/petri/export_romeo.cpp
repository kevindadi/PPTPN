#include "petri/export_romeo.h"

#include <boost/filesystem.hpp>
#include <fstream>
#include <limits>
#include <sstream>
#include <spdlog/spdlog.h>

namespace petri::exporting {

namespace {

std::string escape_xml_string(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char ch : value) {
    switch (ch) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&apos;";
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

std::string quote_xml_attr(const std::string& value) {
  return "\"" + escape_xml_string(value) + "\"";
}

std::string format_int_or_infinity(int value) {
  return value == std::numeric_limits<int>::max() ? "inf" : std::to_string(value);
}

std::string node_id(const PetriExportModel& model, const ExportNodeRef& ref) {
  if (ref.kind == NodeKind::PLACE) {
    return model.places.at(ref.index).id;
  }
  return model.transitions.at(ref.index).id;
}

}  // namespace

std::string render_romeo_cts(const PetriExportModel& model) {
  std::ostringstream out;
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  out << "<romeo-cts>\n";
  out << "  <net>\n";

  for (const auto& place : model.places) {
    out << "    <place id=" << quote_xml_attr(place.id)
        << " name=" << quote_xml_attr(place.name)
        << " initial=" << quote_xml_attr(std::to_string(place.initial_tokens))
        << " capacity=" << quote_xml_attr(format_int_or_infinity(place.capacity))
        << "/>\n";
  }

  for (const auto& transition : model.transitions) {
    out << "    <transition id=" << quote_xml_attr(transition.id)
        << " name=" << quote_xml_attr(transition.name)
        << " priority=" << quote_xml_attr(std::to_string(transition.priority))
        << " earliest=" << quote_xml_attr(format_int_or_infinity(transition.earliest))
        << " latest=" << quote_xml_attr(format_int_or_infinity(transition.latest))
        << " suspendable=" << quote_xml_attr(transition.suspendable ? "true" : "false")
        << "/>\n";
  }

  for (const auto& arc : model.arcs) {
    out << "    <arc source=" << quote_xml_attr(node_id(model, arc.source))
        << " target=" << quote_xml_attr(node_id(model, arc.target))
        << " weight=" << quote_xml_attr(std::to_string(arc.weight))
        << "/>\n";
  }

  out << "  </net>\n";
  out << "</romeo-cts>\n";
  return out.str();
}

bool save_to_romeo_cts(const PetriExportModel& model, const std::string& file_path) {
  try {
    boost::filesystem::path cts_filename(file_path);
    if (!cts_filename.parent_path().empty() &&
        !boost::filesystem::exists(cts_filename.parent_path())) {
      boost::filesystem::create_directories(cts_filename.parent_path());
    }

    std::ofstream ofs(cts_filename.string());
    if (!ofs) {
      spdlog::error("[PETRI_EXPORT] Cannot open Romeo CTS file: {}", cts_filename.string());
      return false;
    }

    ofs << render_romeo_cts(model);
    ofs.close();

    spdlog::info("[PETRI_EXPORT] Romeo CTS file saved to: {}",
                 boost::filesystem::absolute(cts_filename).string());
    return true;
  } catch (const std::exception& e) {
    spdlog::error("[PETRI_EXPORT] Error saving Romeo CTS file: {}", e.what());
    return false;
  }
}

}  // namespace petri::exporting
