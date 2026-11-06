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

This library is the splicing engine only: no dictionary, no database. Of the
Java service's four endpoints exactly one is the splicer and nothing else:

| Java | comparable | against |
|---|---|---|
| `/ime/severe/{key}` | **yes** — `decompose` + `LetterSplicer.severe` | `SHAPE severe` |
| `/ime/fuzzy/{key}` | no — splicer output used as BK-tree keys, dictionary words returned | — |
| `/ime/fuzzy/score/{key}` | no — same, with the match metrics | — |
| `/ime/{key}` | no — `severe` ∪ `fuzzy` | — |

The splicer's **fuzzy** list is not exposed over HTTP at all, so the page
compares it against `testdata/golden_e2e.tsv`, which `tools/golden-java`
dumped from the unmodified Java sources. The three dictionary endpoints are
still in the picker, Java-only, for a quick look at the original service.

`decompose` compares `DECOMPOSE` against `testdata/golden_decompose.tsv`.

## Reading the verdict

Lists are compared **strictly**: same items, same order, same duplicates. A
candidate may legitimately repeat (different shapes, same components), so the
extras are a multiset difference, not a set one.

Three outcomes:

* **完全一致** — every available side matches C++ exactly.
* **只差在已知的原版 bug 上** (amber) — the *running* service differs but the
  golden file agrees. The service is the unmodified original; the golden data
  is that code with this project's two documented fixes applied (README,
  "Deviations from the Java original"). The page works this out from the two
  comparisons rather than from a list of affected inputs, so it stays correct
  if the patch set changes. At the time of writing the whole golden
  vocabulary produces exactly one such input: `eme`.
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

| | 一致 | 不一致 |
|---|---:|---:|
| severe, Java 服务 vs C++ | 604 | 1 (`eme`, the known fix) |
| severe, golden vs C++ | 605 | 0 |
| fuzzy, golden vs C++ | 605 | 0 |
| decompose, golden vs C++ | 605 | 0 |

Five inputs are skipped in each run — empty, one space, two spaces, a tab,
and `a<TAB>b` — because the stdio protocol rejects an empty argument and a
control character rather than guessing.

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
| `/api/compare/{severe,fuzzy,decompose}/{key}` | both sides and the verdict |
| `/api/batch/{kind}?limit=N&offset=M` | the vocabulary, summary and mismatches |
| `/api/cpp/shape/{mode}/{key}`, `/api/cpp/decompose/{key}` | the C++ engine alone |
| `/api/golden/{mode,decompose}/{key}` | the golden files alone |
| `/api/words` | the golden vocabulary |
| `/ime/...` | proxied to the Java service unchanged |

For a candidate list on the command line without any of this, use
`tools/golden_lookup.py` (the Java original, offline) or pipe `SHAPE` into
`zcode_stdio` (see `docs/stdio-protocol.md`).
