# zcode-cpp — orientation for a new session

Read this first; it is loaded automatically. `README.md` has the full
picture — this is the part that is easy to get wrong without being told.

## What this is

A C++20 port of the Mongolian ZCode input engine of
[east-mod/ime](https://github.com/east-mod/ime) (Java, by Zorigt), ported
with the author's permission. Two halves: the **shaping engine**
(`zcode/zcode.h`) turns Latin keys into well-formed letter sequences, and
the **dictionary** (`zcode/dictionary.h`) picks out the ones that are
words. No database, no network, no files. GPL-2.0-or-later — see
`NOTICE.md`.

`service/zcode_stdio` exposes it as a line-protocol process
(`docs/stdio-protocol.md`). That is how
[paiza](https://github.com/hugohu789-droid/paiza) uses it: out of process,
so this licence never reaches paiza's linkage.

## The rule that governs every change

**The output must stay identical to the Java original's**, to the last
float, wherever the original has an opinion. Two things enforce it:

1. `testdata/*.tsv` — golden files produced by running the original Java
   (`tools/golden-java`). 83 tests compare against them.
2. `tools/web-compare` — a page that puts this engine and the **running**
   Java service side by side over the whole 610-input vocabulary. Nothing
   a test covers. Needs MySQL + `ime.jar` from `../ime-java-run`.

Current state: every difference is accounted for — one documented bug fix
(`eme`) and the order of equally scored words. If a change moves those
numbers, it is a regression, not an improvement.

Features the original does not have (`Session`, the trie index, the stdio
service, the extension pack, `UserWords`) are listed in
`docs/test-report.md` §2.2 with, for each, whether it can change an
answer. Most cannot. `UserWords` can — but only once a host puts something
in it, which is why "an empty store changes nothing" is pinned by a test.

## Building

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Builds and passes on Windows (MSVC) and Linux (GCC): 83/83 on both. macOS
is not verified — the port itself is platform-independent and has no
`#ifdef` outside `service/`, so it is expected to work, but nobody has run
it.

Do not rename `src/common_strings.h` back to `strings.h`: the test target
puts `src` on the include path, and a file by that name shadows POSIX
`<strings.h>`, which stops the suite compiling on Linux. The file says so
too.

## Measuring

Use `bench/dict_profile`, which splits one keystroke into splicing,
searching and scoring. Do **not** time the engine through
`subprocess(bufsize=0)`: `readline()` then reads a byte at a time, and a
22 KB answer looks like 110 ms of engine time. That produced a round of
wrong conclusions once, including nearly buying an optimisation that was
not needed.

For reference, with the whole vocabulary typed key by key: median worst
keystroke 0.3 ms, worst real word 16.2 ms, and everything above 20 ms is a
one- or two-letter repetition (`gggggg`, 174 ms). The host's budget is
200 ms per exchange.
