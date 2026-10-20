// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from com.zorigt.ime.letter.LetterLocation and com.zorigt.ime.letter.Nature.
#pragma once

#include <cstdint>

#include "zcode/zcode.h"

namespace zcode {

// LetterLocation: HEAD = 0, MIDDLE = 1, TAIL = 2.
enum class Location : std::uint8_t { head = 0, middle = 1, tail = 2 };

// Nature: CHAGH (masculine), HUNDII (feminine), SAARMAG (neutral).
enum class Nature : std::uint8_t { chagh, hundii, saarmag };

// Dense indices into the tables (see tables.h).
using ShapeId = std::uint16_t;
using RuleId = std::uint16_t;
inline constexpr RuleId no_rule = 0xFFFF;  // Java: a null rule reference

}  // namespace zcode
