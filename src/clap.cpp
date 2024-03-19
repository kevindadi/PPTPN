
#include "include/clap.h"

TDG::TDG(string tdg_file)
{
    this->tdg_file = std::move(tdg_file);
    logging::core::get()->set_filter(logging::trivial::severity >=
                                     logging::trivial::info);
}

void TDG::prase_tdg() {}