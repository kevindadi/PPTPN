#include "petri/export_romeo.h"

#include <boost/filesystem.hpp>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <vector>

namespace petri::exporting {

namespace {

struct TransitionArcs {
  std::map<size_t, int> inputs;
  std::map<size_t, int> outputs;
};

std::string sanitize_identifier(const std::string& raw, const std::string& fallback) {
  const std::string& source = raw.empty() ? fallback : raw;
  std::string result;
  result.reserve(source.size());

  for (char ch : source) {
    const auto uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) || ch == '_') {
      result += ch;
    } else {
      result += '_';
    }
  }

  if (result.empty()) {
    result = fallback;
  }
  if (!std::isalpha(static_cast<unsigned char>(result.front())) && result.front() != '_') {
    result = fallback + "_" + result;
  }
  return result;
}

std::string unique_identifier(const std::string& candidate, std::set<std::string>& used) {
  std::string result = candidate;
  int suffix = 1;
  while (used.count(result) > 0) {
    result = candidate + "_" + std::to_string(suffix++);
  }
  used.insert(result);
  return result;
}

std::vector<std::string> place_identifiers(const PetriExportModel& model) {
  std::vector<std::string> ids;
  ids.reserve(model.places.size());
  std::set<std::string> used;
  for (size_t i = 0; i < model.places.size(); ++i) {
    ids.push_back(unique_identifier(
        sanitize_identifier(model.places[i].id, "P" + std::to_string(i + 1)), used));
  }
  return ids;
}

std::vector<std::string> transition_identifiers(const PetriExportModel& model) {
  std::vector<std::string> ids;
  ids.reserve(model.transitions.size());
  std::set<std::string> used;
  for (size_t i = 0; i < model.transitions.size(); ++i) {
    ids.push_back(unique_identifier(
        sanitize_identifier(model.transitions[i].id, "T" + std::to_string(i + 1)), used));
  }
  return ids;
}

std::vector<TransitionArcs> collect_transition_arcs(const PetriExportModel& model) {
  std::vector<TransitionArcs> transition_arcs(model.transitions.size());
  for (const auto& arc : model.arcs) {
    if (arc.source.kind == NodeKind::PLACE && arc.target.kind == NodeKind::TRANSITION) {
      transition_arcs.at(arc.target.index).inputs[arc.source.index] += arc.weight;
    } else if (arc.source.kind == NodeKind::TRANSITION && arc.target.kind == NodeKind::PLACE) {
      transition_arcs.at(arc.source.index).outputs[arc.target.index] += arc.weight;
    }
  }
  return transition_arcs;
}

std::string format_int_or_infinity(int value) {
  return value == std::numeric_limits<int>::max() ? "inf" : std::to_string(value);
}

std::string format_interval(const ExportTransition& transition) {
  return std::string(transition.left_open ? "(" : "[") +
         format_int_or_infinity(transition.earliest) + "," +
         format_int_or_infinity(transition.latest) + std::string(transition.right_open ? ")" : "]");
}

bool has_explicit_priority(const ExportTransition& transition) {
  return transition.priority != std::numeric_limits<int>::max();
}

std::string format_transition_options(const ExportTransition& transition,
                                      const TransitionArcs& arcs,
                                      const std::vector<std::string>& places) {
  std::vector<std::string> options;
  if (has_explicit_priority(transition)) {
    options.push_back("priority=" + std::to_string(transition.priority));
  }

  if (!arcs.inputs.empty()) {
    std::ostringstream intermediate;
    intermediate << "intermediate { ";
    bool first = true;
    for (const auto& [place_index, weight] : arcs.inputs) {
      if (!first) {
        intermediate << " , ";
      }
      const auto& place = places.at(place_index);
      intermediate << place << " = " << place << " - " << weight;
      first = false;
    }
    intermediate << "; }";
    options.push_back(intermediate.str());
  }

  if (options.empty()) {
    return "";
  }

  std::ostringstream out;
  out << " [";
  for (size_t i = 0; i < options.size(); ++i) {
    if (i > 0) {
      out << ", ";
    }
    out << options[i];
  }
  out << "]";
  return out.str();
}

std::string format_guard(const TransitionArcs& arcs, const std::vector<std::string>& places) {
  if (arcs.inputs.empty()) {
    return "true";
  }

  std::ostringstream guard;
  bool first = true;
  for (const auto& [place_index, weight] : arcs.inputs) {
    if (!first) {
      guard << " and ";
    }
    guard << places.at(place_index) << " >= " << weight;
    first = false;
  }
  return guard.str();
}

std::string format_update(const TransitionArcs& arcs, const std::vector<std::string>& places) {
  std::set<size_t> touched_places;
  for (const auto& [place_index, weight] : arcs.inputs) {
    (void)weight;
    touched_places.insert(place_index);
  }
  for (const auto& [place_index, weight] : arcs.outputs) {
    (void)weight;
    touched_places.insert(place_index);
  }

  if (touched_places.empty()) {
    return "";
  }

  std::ostringstream update;
  bool first = true;
  for (size_t place_index : touched_places) {
    if (!first) {
      update << " , ";
    }

    const auto& place = places.at(place_index);
    const int input_weight = arcs.inputs.count(place_index) > 0 ? arcs.inputs.at(place_index) : 0;
    const int output_weight =
        arcs.outputs.count(place_index) > 0 ? arcs.outputs.at(place_index) : 0;

    update << place << " = " << place;
    if (input_weight > 0) {
      update << " - " << input_weight;
    }
    if (output_weight > 0) {
      update << " + " << output_weight;
    }
    first = false;
  }
  return update.str();
}

}  // namespace

std::string render_romeo_cts(const PetriExportModel& model) {
  const auto places = place_identifiers(model);
  const auto transitions = transition_identifiers(model);
  const auto transition_arcs = collect_transition_arcs(model);

  std::ostringstream out;
  out << "// TPN name=PTPN\n\n";
  out << "typedef int place; \n\n";
  out << "initially { \n";
  out << "place ";
  for (size_t i = 0; i < model.places.size(); ++i) {
    if (i > 0) {
      out << ", ";
    }
    out << places[i] << "=" << model.places[i].initial_tokens;
  }
  out << "; }\n\n";

  for (size_t i = 0; i < model.transitions.size(); ++i) {
    const auto& transition = model.transitions[i];
    const auto& arcs = transition_arcs[i];
    out << " transition" << format_transition_options(transition, arcs, places) << "  "
        << transitions[i] << " " << format_interval(transition) << "\n";
    out << "      when (" << format_guard(arcs, places) << ")\n";
    out << "      { ";
    const auto update = format_update(arcs, places);
    if (!update.empty()) {
      out << update << "; ";
    }
    out << " }\n";
  }

  out << "\ngraph [passed=eq]\n";
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
