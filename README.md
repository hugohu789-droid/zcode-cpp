# zcode-cpp

A standalone C++20 port of the Mongolian input method of
[east-mod/ime](https://github.com/east-mod/ime) (Java, by Zorigt): Latin key
sequence in, candidate ZCode words out, following traditional Mongolian
orthography (positional forms, vowel harmony, letter-joining rules).

Two halves. The **shaping engine** (`zcode/zcode.h`) turns keys into
well-formed letter sequences; most of them are not words. The **dictionary**
(`zcode/dictionary.h`) picks out the ones that are, from the original's own
38,743-word list, compiled in. No database, no network, no files.

GPL-2.0-or-later. See `NOTICE.md` for attribution and third-party data.

## ZCode

ZCode is a form-code (形码) encoding. Each code point is one glyph component
(tooth, belly, tail, …); a written letter is a run of one to three components.
It borrows the 52 code points U+1865..U+18AA of the Unicode Mongolian block,
and this library outputs them exactly as the Java original does. Render with
a ZCode (Z52) font, e.g. Z52 Hara Tig, bundled as
`third_party/fonts/z52haratig.ttf` (redistributed with the author's
permission; no published licence of its own — see `NOTICE.md`).

Internally a ZCode string is `std::u32string` (`zcode::CodePoint` =
`char32_t`); `zcode::to_utf8()` converts at the boundary. Latin input is a
UTF-8 byte string; only the layout keys `a e i o u n b p h g m l s x t d q j y
r w f k c z H R L Z C` (case-sensitive) are meaningful.

## API

```cpp
#include <zcode/zcode.h>

auto engine = zcode::Engine::create();          // embedded data tables, no I/O
for (const auto& word : engine->shape_utf8("mongol", zcode::Mode::severe)) {
    // ...
}
```

- `Mode::severe` — full orthographic rules; the last letter takes a final form.
- `Mode::fuzzy` — relaxed rules; the last letter may also be medial (for
  prefix search). Can produce many candidates.
- `splice()` and `decompose()` expose the two stages separately;
  `key_filter_accepts()` is the original's `b-tail` dictionary filter.
- `count(keys, mode)` returns the exact number of candidates without
  building them.

Per keystroke, use a `Session`: it caches everything but the last key, so
each key costs one layer of splicing. `candidates()` always equals
`splice(keys(), mode)`.

```cpp
zcode::Session s(*engine);
s.push("m"); s.push("o"); s.push("n");
auto now = s.candidates(zcode::Mode::fuzzy);
s.pop();  // backspace
```

`zcode::Options` (all off by default; the defaults reproduce the Java
original, apart from the two bug fixes below): `max_candidates` keeps only the first N candidates in the
original order, `deduplicate` drops repeated ZCode strings keeping the first.

No globals, no singletons, no I/O inside the algorithm. Candidates are
returned in the same order as the Java original (see the deviations below).

### The dictionary

```cpp
#include <zcode/dictionary.h>

auto dictionary = zcode::Dictionary::create();   // 38,743 words, embedded
zcode::Ime ime(*engine, *dictionary);

ime.severe_make_word("bagsi");   // no dictionary; same as shape(severe)
ime.fuzzy_make_word("bagsi");    // dictionary words, best score first
ime.make_word("bagsi");          // the two merged, the original's way round
```

These are the three methods behind the original's HTTP endpoints, and they
answer what it answers. `fuzzy_make_word` returns `SuggestWord`, which
carries every number behind the ranking, as `/ime/fuzzy/score/{key}` does.

The search looks like approximate string matching but is not. Every word in
a length-K bucket has length exactly K, so a hit at tolerance K-L has
distance exactly K-L, which can only be K-L insertions:

> a word matches ⟺ the query is a **subsequence** of it, and `L ≤ |word| < 2L`

and the JaroWinkler filter that follows reduces to "the first code point
must match". Both were checked against the running original over its whole
vocabulary. So the index here is a trie walked greedily, not the original's
Burkhard-Keller tree: identical results, measured 819× faster (0.29 ms
against 237.57 ms for the same 60 lookups). A faithful BK tree is kept as
the test oracle. Loading and indexing the dictionary takes 36 ms.

An optional pack of the 461,182 words the original never loads can be
generated with `tools/extract_dictionary.py --extension` and attached with
`Ime::set_extension`. It is consulted only when the main dictionary answers
with nothing -- which is also when the original answers with nothing -- so
it fills gaps without contradicting it.

### The user's own words

```cpp
zcode::UserWords mine;                 // the host fills this from its store
mine.restore({word, /*count=*/4, /*tick=*/730});
ime.set_user_words(&mine);
ime.set_now(931);                      // the host's clock, for the decay
...
mine.learn(chosen, 931);               // the user committed one
for (const auto& e : mine.entries()) { /* the host persists these */ }
```

**This engine ranks; the host stores.** The host owns the file, the
per-profile scoping, the privacy policy and any synchronisation; `UserWords`
touches no disk and no clock, which is also what makes it testable.

The habit model is Chewing's -- how often, damped, decayed by how long ago --
added to the score the dictionary gave, which is normalised to at most 1:

```
bonus = weight * count/(count + saturation) * 0.5^((now - tick)/half_life)
```

with `weight` 1.0 (the ceiling, against a dictionary score of at most 1),
`half_life` 10 ticks and `saturation` 2 (uses at which the strength is half).

So a settled habit outranks anything the dictionary alone can offer, a single
old use only nudges, and a word left alone for long enough fades back to
where the dictionary had it. A word **only** the user has is offered too,
above the dictionary's own, because it matched what was typed by the same
rule. An empty store changes nothing -- which is what the comparison against
the Java service depends on, that service having no such feature.

## Deviations from the Java original

Two bugs of the original are fixed (everything else, including its other
quirks, is reproduced exactly):

1. After a key at which every candidate was rejected, the original restarted
   splicing from the empty sequence and returned fragments of the word's tail.
   Here the result stays empty. Keys without shapes at their position are
   still skipped, as in the original.
2. `FRONT_LIKE_TAIL_ORHICH_E` listed `mMiddleHurbelge` instead of
   `mMiddleAchvg`, so "...m e" could never end in m-achvg + e-orhich.

The golden data is generated from the original with the same two fixes
applied (`tools/golden-java/src/patches/`); `-Poriginal` regenerates it from
the unmodified original.

## Build and test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

The tests compare the data tables, every rule's truth table and the full
golden outputs against files generated by running the Java original.

## Golden data

`testdata/*.tsv` is produced by `tools/golden-java`, which compiles the
original Java sources (expected next to this repository as `../ime-java`;
override with `-Dime.java.src=...`) without Spring or a database:

```bash
mvn -q -f tools/golden-java/pom.xml compile exec:java -Dexec.args=testdata
```

Add `-Poriginal` (and another output directory) for the unpatched original.

## Layout

| Path | Contents |
|---|---|
| `include/zcode/zcode.h` | public API |
| `src/` | the port, one file per Java class or group (each names its origin) |
| `include/zcode/dictionary.h` | public API of the dictionary half |
| `data/` | `zcode.tsv`, `shapes.tsv`, `rules.tsv`, `keyboard.tsv`, `dictionary.tsv` — embedded at build time |
| `service/` | `zcode_stdio`, the engine as a process a host can drive |
| `tools/web-compare/` | a page that diffs this against the running Java service |
| `testdata/` | golden files from the Java original |
| `tools/golden-java/` | the Java harness that generates them |
| `docs/` | analysis reports |
