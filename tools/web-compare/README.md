# web-compare

A browser page that puts this C++ engine and the Java original side by side
and says, candidate by candidate, whether they agree.

```bash
cmake --build build --target zcode_stdio
python tools/web-compare/compare.py        # http://127.0.0.1:8098/
```

The page finds `zcode_stdio` under `build/`, `build/Release/`,
`build-vs/Release/` or `build-asan/`; `ZCODE_STDIO` overrides that. The Java
service is optional — without it, `severe` still compares against the golden
files. Start it from `../ime-java-run` (see that directory's README).

## What is comparable, and what is not

All four of the Java service's endpoints have a counterpart here. One of
them is the splicer and nothing else; the other three go through the
dictionary:

| Java | against |
|---|---|
| `/ime/severe/{key}` | `SHAPE severe` — `decompose` + `LetterSplicer.severe`, no dictionary |
| `/ime/fuzzy/{key}` | `LOOKUP fuzzy` |
| `/ime/fuzzy/score/{key}` | `LOOKUP score` — the words *and* every metric behind the ranking |
| `/ime/{key}` | `LOOKUP make` — `severe` ∪ `fuzzy` |

All four compare against the live service. The splicer's own **fuzzy** list
is not exposed over HTTP at all -- `/ime/fuzzy` puts it through the BK tree
and returns dictionary words -- so that one is compared against
`testdata/golden_e2e.tsv`, dumped by `tools/golden-java` from the unmodified
Java sources. `decompose` likewise uses `testdata/golden_decompose.tsv`.

## Reading the verdict

Lists are compared **strictly**: same items, same order, same duplicates. A
candidate may legitimately repeat (different shapes, same components), so the
extras are a multiset difference, not a set one.

Four outcomes:

* **完全一致** — every available side matches C++ exactly.
* **只差在已知的原版 bug 上** (amber) — the *running* service differs but the
  golden file agrees. The service is the unmodified original; the golden data
  is that code with this project's two documented fixes applied (README,
  "Deviations from the Java original"). The page works this out from the two
  comparisons rather than from a list of affected inputs, so it stays correct
  if the patch set changes. At the time of writing the whole golden
  vocabulary produces exactly one such input: `eme`.
* **词和评分全同,只差同分次序** (amber) — the two lists hold the same words
  with the same metrics and the same sequence of scores, so only words that
  score equally have moved. That is the one declared difference: the
  original leaves ties in its HashMap's bucket order, this port sorts them
  by frequency and then by code point. The page proves it rather than
  assuming it, by asking both sides for the scored list and comparing the
  score sequences.
* **有差异** (red) — anything else. That is the case worth looking at.

Over 2000 candidates the dump keeps only a count and an FNV-1a digest instead
of the list; those inputs (`iiiiiiiiii`, `gggggg`, `hghghg`) are compared by
count and digest, which is still exact. The page says so.

## 批量对比

`跑一遍` runs the whole golden vocabulary (610 inputs) through both sides and
lists every mismatch. For `severe` that means 610 requests to the live Java
service, which takes a couple of seconds.

This is the part no unit test covers: the test suite checks C++ against the
golden *files*, and nothing checks the *running* Java service against C++.

Current result, full vocabulary:

| | 一致 | 不一致 | of which explained |
|---|---:|---:|---|
| severe, Java 服务 vs C++ | 604 | 1 | `eme`, the known fix |
| severe, golden vs C++ | 605 | 0 | |
| fuzzy 拼接, golden vs C++ | 605 | 0 | |
| decompose, golden vs C++ | 605 | 0 | |
| `/ime/fuzzy`, Java 服务 vs C++ | 532 | 73 | all 73 tie order |
| `/ime/fuzzy/score`, Java 服务 vs C++ | 532 | 73 | all 73 tie order |
| `/ime/{key}`, Java 服务 vs C++ | 535 | 70 | all 70 tie order |

Nothing is unexplained: every difference is either one of the two
documented fixes or the order of equally scored words.

Five inputs are skipped in each run — empty, one space, two spaces, a tab,
and `a<TAB>b` — because the stdio protocol rejects an empty argument and a
control character rather than guessing.

## 扩展词库

The **扩展词库** switch turns on the optional 461,182-word pack
(`python tools/extract_dictionary.py --extension`), which the engine
consults only when the compiled-in dictionary answers with nothing. Turning
it on restarts the engine process, because whether the pack is loaded is
fixed at startup.

With it off, the answers are the original's. With it on they are the
original's wherever the original had any, plus words it does not have
wherever it had none -- so it is the quick way to see what the pack buys
without losing the ability to show the port matches. Try `bicig`: nothing
from either side with the pack off, one word from the port with it on.

## The page

Candidates are drawn as vertical columns running left to right. The aligned
view puts the sides' columns next to each other, one cell per position, and
colours a cell when they differ; `分开显示` gives one pane per side instead.
Hovering a candidate shows its code points.

The font picker lists the installed Z52 faces plus two Unicode Mongolian
fonts. Keep those last two: ZCode borrows the assigned code points
`U+1865..U+18AA`, which are Sibe and Manchu letters in Unicode, so a Unicode
font renders them as *different letters that still look like plausible
Mongolian*. Switching font and watching the shapes change is the quick way to
confirm a form-code font is really in use and not a silent fallback.

## Why the Java side is proxied

The Java service sends no CORS headers, so a page on another origin can send
a request but cannot read the reply. `compare.py` serves the page and proxies
`/ime/*`, so the two share an origin. The alternative, adding a CORS config,
would mean editing `../ime-java`, which stays unmodified because the golden
data comes from it.

## Endpoints

Useful from a shell too; all return the Java service's envelope shape.

| | |
|---|---|
| `/api/info` | engine path and `HELLO`, whether the service is up, which golden files loaded |
| `/api/compare/{kind}/{key}` | both sides and the verdict |
| `/api/batch/{kind}?limit=N&offset=M` | the vocabulary, summary and mismatches |
| `/api/extension?on=0\|1` | load or unload the extension pack |

`{kind}` is `severe`, `fuzzy`, `decompose`, `dictfuzzy`, `score` or `make`.
| `/api/cpp/shape/{mode}/{key}`, `/api/cpp/decompose/{key}` | the C++ engine alone |
| `/api/golden/{mode,decompose}/{key}` | the golden files alone |
| `/api/words` | the golden vocabulary |
| `/ime/...` | proxied to the Java service unchanged |

For a candidate list on the command line without any of this, use
`tools/golden_lookup.py` (the Java original, offline) or pipe `SHAPE` into
`zcode_stdio` (see `docs/stdio-protocol.md`).
