// SPDX-License-Identifier: GPL-2.0-or-later
//
// Access to data/dictionary.tsv, compiled into the library.
#pragma once

#include <string_view>

namespace zcode::dict {

// The word list as it appears in data/dictionary.tsv: comment lines starting
// with '#', then one `word<TAB>frequency` line each, sorted by word.
std::string_view embedded_word_list();

}  // namespace zcode::dict
