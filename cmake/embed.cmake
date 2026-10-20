# SPDX-License-Identifier: GPL-2.0-or-later
#
# cmake -DINPUT=<file> -DOUTPUT=<header> -DSYMBOL=<name> -P embed.cmake
# Writes <header> declaring `inline constexpr unsigned char SYMBOL[]` holding the
# bytes of <file> followed by one NUL.
file(READ "${INPUT}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){32})" "\\1\n" bytes "${bytes}")
get_filename_component(name "${INPUT}" NAME)
file(WRITE "${OUTPUT}.tmp"
    "// generated from data/${name} by cmake/embed.cmake; do not edit\n"
    "#pragma once\n"
    "namespace zcode {\n"
    "inline constexpr unsigned char ${SYMBOL}[] = {\n${bytes}0x00};\n"
    "}  // namespace zcode\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
