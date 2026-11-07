# NOTICE

## Original work

zcode-cpp is a C++20 port of the Mongolian letter-shape and letter-splicing
engine of **east-mod/ime** by **Zorigt** (east-mod):

- Repository: <https://github.com/east-mod/ime>
- Ported from commit `cad6a24c8f705aae4e7a11c8ab54f141ec0a7fc1` (2022-06-09)
- Original license: GNU General Public License version 2 (`LICENSE` in that repository)
- Ported with the author's permission.

This port is a derivative work and is distributed under
**GPL-2.0-or-later** (see `LICENSE`). Every ported source file names the Java
class it came from.

Ported: the shaping engine (`letter/`, `letter/base/`, `letter/splice/`,
`keyboard/`, `filter/`, parts of `common/Strings`), and the dictionary half
— `approximate/matching/` and the three methods of
`input/method/service/InputMethodService` behind the HTTP endpoints. Not
ported: the Spring Boot service, MyBatis/MySQL access, the HTTP controllers
and the thread pool.

The data tables in `data/` are transcriptions of the Java classes named in
their headers. `data/dictionary.tsv` is the `word` table of that project's
own `ime.sql`, restricted to the rows the service loads
(`frequency > 44`); `tools/extract_dictionary.py` regenerates it, and with
`--extension` also writes the rows the service never loads. Both are the
original work's data and carry its licence.

`testdata/golden_dict.tsv` is what the original *service* answered, captured
over HTTP by `tools/golden_dict.py`, because its dictionary lives in MySQL
and cannot be dumped offline like the rest. The golden files in `testdata/`
were produced by running the
original Java code (see `tools/golden-java/`) with two bug fixes applied:
`tools/golden-java/src/patches/` contains modified copies of the original
`LetterSplicer` and `FrontLikeTailOrhichE` (GPL, by Zorigt), each header
describing its change. The same fixes are made in the C++ port and listed in
`README.md` under "Deviations from the Java original".

## ZCode font

ZCode output is meant to be rendered with a ZCode ("Z52") form-code font. The
reference font used for this project is:

| | |
|---|---|
| Name | **Z52 Hara Tig** (`z52haratig.ttf`), "Version 1.02; December 31, 2021" |
| Copyright string in the font | "Z Code" |
| License | **No licence text accompanies the font** — its `name` table has no license or license-URL entry, and `OS/2.fsType` = 8 (editable embedding). Redistribution here rests on the author's permission, recorded below, not on a published licence. |
| Obtained from | <https://github.com/zmongol/mongol_editor_package-main>, commit `618edf0237c6ec18d38b4fe07ab244f2f98a3d7f`, `assets/fonts/z52haratig.ttf` |
| SHA-256 | `2bc510505c6f52935506a3d342758ed44b271164c7b4841415ff7a94172db2ef` |
| Coverage | all 52 ZCode code points (U+1865..U+18AA); no Private Use Area; GSUB for script `mong` |

That repository's `LICENSE` is MIT ("Copyright (c) 2025 Zmongol Editor
Contributors") and covers its software; the fonts carry an older, different
copyright ("Z Code", 2021–2023) and no license of their own, so the MIT grant
cannot be assumed to cover them.

The font is included unmodified as `third_party/fonts/z52haratig.ttf`. The
project owner confirmed with the original author (Zorigt, east-mod) that the
font is free to use and may be redistributed with this port. That permission
was given in correspondence; it is not a published licence, and the font still
carries no licence text of its own, so it stays outside this project's GPL and
is not relicensed by it. `tools/fetch_font.py` re-downloads the font from the
pinned commit and verifies the hash. The library itself never needs the font.

**One point is still worth settling in writing.** The permission came from the
author of the engine this port derives from. The font's own copyright string
is "Z Code" and it was obtained from a different repository (`zmongol`), whose
`LICENSE` covers only its software. If that is the same rights holder, nothing
further is needed. If it is not, the font needs permission from whoever holds
"Z Code". Either way, a named licence -- OFL-1.1 is the usual choice for a
font -- in a `LICENSE` file beside the font or in its `name` table is what a
downstream redistributor can rely on: a private assurance given to this
project does not travel with the file.

## Data not included

- `ime.sql` (inside the original's `ime.tar.gz`): a 499,925-row word-frequency
  table in ZCode. Its corpus provenance is not documented in the original
  repository; it is not part of this port.

## Build-time dependencies (not redistributed)

- GoogleTest 1.15.2 (BSD-3-Clause), fetched by CMake for the test suite.
- The golden-data harness compiles against the original Java sources and uses
  lombok, Guava, Apache Commons Collections and SLF4J from Maven Central.
