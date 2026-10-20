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
class it came from. Only the shaping engine was ported (`letter/`,
`letter/base/`, `letter/splice/`, `keyboard/`, `filter/`, parts of
`common/Strings`); the Spring Boot service, MyBatis/MySQL access, HTTP
controllers and the approximate-matching code were not.

The data tables in `data/` are transcriptions of the Java classes named in
their headers. The golden files in `testdata/` were produced by running the
original Java code (see `tools/golden-java/`).

## ZCode font

ZCode output is meant to be rendered with a ZCode ("Z52") form-code font. The
reference font used for this project is:

| | |
|---|---|
| Name | **Z52 Hara Tig** (`z52haratig.ttf`), "Version 1.02; December 31, 2021" |
| Copyright string in the font | "Z Code" |
| License | **none stated** — the font's `name` table has no license or license-URL entry; `OS/2.fsType` = 8 (editable embedding) |
| Obtained from | <https://github.com/zmongol/mongol_editor_package-main>, commit `618edf0237c6ec18d38b4fe07ab244f2f98a3d7f`, `assets/fonts/z52haratig.ttf` |
| SHA-256 | `2bc510505c6f52935506a3d342758ed44b271164c7b4841415ff7a94172db2ef` |
| Coverage | all 52 ZCode code points (U+1865..U+18AA); no Private Use Area; GSUB for script `mong` |

That repository's `LICENSE` is MIT ("Copyright (c) 2025 Zmongol Editor
Contributors") and covers its software; the fonts carry an older, different
copyright ("Z Code", 2021–2023) and no license of their own, so the MIT grant
cannot be assumed to cover them. **The font is therefore not committed to this
repository** until its license is confirmed. `tools/fetch_font.py` downloads
it from the pinned commit and verifies the hash, into the git-ignored
`third_party/fonts/`. The library itself never needs the font.

## Data not included

- `ime.sql` (inside the original's `ime.tar.gz`): a 499,925-row word-frequency
  table in ZCode. Its corpus provenance is not documented in the original
  repository; it is not part of this port.

## Build-time dependencies (not redistributed)

- GoogleTest 1.15.2 (BSD-3-Clause), fetched by CMake for the test suite.
- The golden-data harness compiles against the original Java sources and uses
  lombok, Guava, Apache Commons Collections and SLF4J from Maven Central.
