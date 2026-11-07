# The stdio service protocol

`zcode_stdio` exposes the shaping engine as a line-oriented service on
stdin/stdout, so a host can use it from a separate process.

Running it out of process is deliberate. This library is `GPL-2.0-or-later`;
keeping it in its own process keeps it out of a host's linkage, so a host under
a different licence can drive it. It also makes the engine usable from a host
written in any language.

The protocol logic lives in `service/protocol.{h,cpp}`, separate from `main()`,
so it is unit-tested directly (`tests/test_protocol.cpp`) rather than by
spawning a process.

## Framing

One request per line, one response per line, UTF-8, LF-terminated. A trailing
CR is tolerated, so a Windows client may write in text mode. Fields are
TAB-separated.

TAB is safe as a separator because ZCode output can never contain it: ZCode
code points are U+1865..U+18AA. A test pins this (`CandidatesNeverContainTabOrNewline`).

## Sessions

A host with several input contexts, a window each or an input client each,
needs a shaping session for each, so the session commands carry an id. The
server allocates ids; `OPEN` returns one.

**Session 0 always exists and cannot be closed.** A single-context host, and a
shell one-liner, can therefore ignore session management and just use `0`.

## Requests

| Request | Meaning |
|---|---|
| `HELLO` | Engine name and protocol version. |
| `OPEN` | Allocate a session; the response carries its id. |
| `CLOSE <sid>` | Destroy it. Session 0 is rejected. |
| `PUSH <sid> <key>` | Append one layout key. The key is taken verbatim (a key may be several characters). |
| `POP <sid>` | Drop the last key. No-op on an empty session. |
| `CLEAR <sid>` | Empty the session. |
| `KEYS <sid>` | The session's keys. |
| `CAND <sid> <mode> [max] [dedup]` | Candidates for the session's keys. |
| `TYPE <sid> <key> <mode> [max] [dedup]` | Push a key **and** return the candidates, in one exchange. |
| `UNTYPE <sid> <mode> [max] [dedup]` | Pop a key **and** return the candidates. |
| `SHAPE <mode> <latin>` | Stateless: shape a whole Latin string. No id. |
| `DECOMPOSE <latin>` | Every way of splitting the input into layout keys. No id. |
| `LOOKUP <what> <latin>` | Stateless dictionary lookup, named after the original's HTTP endpoints. No id, no cap. |
| `USER <what> ...` | The user's own words and habits. |
| `DICT` | What dictionary is loaded. |
| `QUIT` | End the conversation. The response is sent before the service exits. |

An unknown or missing `<sid>` is an error and changes nothing. That holds for
`TYPE` and `UNTYPE` too: the key is pushed or popped only after every field
has validated, so a rejected request never half-applies.

A field carrying a control character is **rejected, not stripped**. Stripping
would turn an injection attempt into a valid-looking request, and a control
character is exactly what would break the line framing.

### Version handshake

```console
$ printf 'HELLO
QUIT
' | zcode_stdio
OK	paiza-engine	1
OK
```

A host should check this before trusting an engine binary, so a mismatch fails
at startup rather than on a real request.

### Modes

`<mode>` selects what a query answers with. The first two are the splicer's
own and answer with **letter shapes**; the other three go through the
dictionary and answer with **words**, so they are rejected on a build that has
none (`DICT` says which this is).

| `<mode>` | Answers with | Equivalent stateless form |
|---|---|---|
| `severe` | letter shapes, the last letter in its final form — for a word the user has finished | `SHAPE severe <latin>` |
| `fuzzy` | letter shapes, the last letter medial or final — while the user is still typing, since it is a prefix search | `SHAPE fuzzy <latin>` |
| `word` | dictionary words, best score first | `LOOKUP fuzzy <latin>` |
| `score` | the same words, each with every metric behind its ranking | `LOOKUP score <latin>` |
| `make` | `severe` shapes and dictionary words merged | `LOOKUP make <latin>` |
| `full` | `score`, then the `severe` shapes it does not already contain, written as scored items with the metrics zeroed | none |

A host picks one mode and stays with it; which one is the host's policy, not
the engine's. Paiza's input profile carries it as `engine_mode`.

**An interactive host wants `full`.** The dictionary holds 38,743 words, so
`word` and `score` answer with nothing whenever someone types a word that is
not among them -- a blank candidate window and nothing to commit, which is
how an input method must never behave. `full` appends the splicer's own
reading of what was typed for exactly that case, with the metrics zeroed so
the shapes sort below every real word on the host's one scale. `score` stays
as it is because it is what `/ime/fuzzy/score/{key}` answers, which is what
the comparison harness checks against the original.

An item in `score` mode is the word, then its metrics, exactly as the
original's `SuggestWord.toString()` writes them:

```
<word>,frequency:110487,frequencyScore:1.0,levenshteinD:0,levenshteinScore:0.8181818,jaroWinklerScore:3.0,score:1.0
```

The word ends at the first comma — unambiguous, because ZCode is
`U+1865..U+18AA` and so can contain neither a comma nor a colon. The floats
are formatted the way Java's `Float.toString()` formats them, so a host
diffing this against the original service sees no spurious differences.

A dictionary mode takes the session's keys joined back into the Latin string
and decomposes that itself, so a host may push keys in any grouping.

`[max]` caps the number of candidates (`0` = no cap) and `[dedup]` is `0` or
`1`. Both default to the library's `Options` defaults and both apply in every
mode. **`max` matters most in `score`**: a few hundred words each carrying six
metrics is a response of well over 100 KB, and a host is right to treat a
response that size as a protocol error. The shaping modes apply the cap before
building the list; a dictionary mode cannot, because the ranking needs every
match before it knows which are best, so there the cap truncates the ranked
list — the words kept are the best ones, in order.

### LOOKUP

`LOOKUP <what> <latin>` is the stateless form of the three dictionary modes,
with `<what>` named after the original service's endpoints so the two can be
compared answer for answer:

| `<what>` | Original endpoint |
|---|---|
| `make` | `/ime/{key}` |
| `fuzzy` | `/ime/fuzzy/{key}` |
| `score` | `/ime/fuzzy/score/{key}` |
| `severe` | `/ime/severe/{key}` (no dictionary; same as `SHAPE severe`) |

It takes no cap, so a host should prefer `TYPE`/`CAND`.

## Responses

| Response | Meaning |
|---|---|
| `OK` | Success, no payload. |
| `OK<TAB>item<TAB>item...` | Success with a payload. |
| `ERR<TAB><message>` | The request was rejected. |

A rejected request leaves the session untouched, so a client can recover
without resynchronising. `DECOMPOSE` returns one item per split, the keys of a
split joined by spaces, because a split is itself a list.

## The user's own words

The division of labour is deliberate: **this engine ranks, the host stores.**
The host owns the file or database, the per-profile scoping, the privacy
policy and any synchronisation. It replays what it kept into the engine at
startup, tells the engine when the user commits a word, and reads the entries
back to persist them. Nothing here touches a disk or a clock.

| Request | Meaning |
|---|---|
| `USER now <tick>` | The host's clock now reads this. Habits decay against it. |
| `USER learn <tick> <word>` | The user committed `<word>` at `<tick>`: one more use, as of then. Also sets "now". |
| `USER restore <count> <tick> <word>` | Replay one stored entry. Replaying the same store twice is harmless -- the higher count and the later tick win -- so a restart does not look like use. |
| `USER forget <word>` | Drop one entry. |
| `USER clear` | Drop them all. |
| `USER list` | Every entry, one item each, as `<word>,count:<n>,tick:<n>` -- the same "text, then fields" shape a scored candidate uses. |

`<tick>` is the host's own clock and the engine assumes only that it does not
go backwards. Days since an epoch is the natural unit, and then the half-life
below is in days; a counter of commits works too.

A word the user has committed keeps its place in the dictionary's ranking and
is lifted by how established the habit is:

```
strength = count / (count + 2)
decay    = 0.5 ^ ((now - tick) / 30)
bonus    = 1.0 * strength * decay
```

The dictionary's own score is normalised to at most 1, so a settled habit
outranks anything the dictionary alone can offer and a single old use only
nudges. A word **only** the user has is offered as well, above the
dictionary's own, scored `1 + bonus`: it matched what was typed by the same
rule the dictionary uses, and the user put it there. Its metric fields stay
zero, because there is no frequency to compare it against and no maximum to
normalise it by, and a number that looked like the dictionary's but meant
something else would be worse than none.

**An empty store changes nothing**, which is what the comparison against the
Java service depends on -- that service has no such feature, so every
difference would otherwise look like a port bug.

What a host does, in order: replay the store, keep the clock current, ask as
usual, and record what the user picked.

```
USER restore 4 730 <word>        # at startup, once per stored entry
USER now 931                     # today, so the decay is against today
TYPE 0 b full 27                 # ... one per keystroke
USER learn 931 <word>            # the user pressed space on this one
USER list                        # at shutdown, to persist
```

## Driving it from a shell

Stateless:

```console
$ printf 'SHAPE fuzzy sain\nQUIT\n' | zcode_stdio
OK	ᢔᡪᡬᡱ	ᢔᡪᡬᡬᡱ	ᢔᡪᡬᡰ	ᢔᡪᡬᡬᡰ	ᢔᡪᡬᡪ	ᢔᡪᡬᡬᡪ	ᢔᡪᡬᡧ	ᢔᡪᡬᡬᡧ
OK
```

Incremental with `TYPE`, the way an IME should use it — one round trip per
keystroke:

```console
$ printf 'TYPE 0 s fuzzy 3\nTYPE 0 a fuzzy 3\nTYPE 0 i fuzzy 3\nUNTYPE 0 fuzzy 3\nQUIT\n' | zcode_stdio
OK	ᢔ
OK	ᢔᡪ	ᢔᡧ	ᢔᡨ
OK	ᢔᡪᡬ	ᢔᡪᡬᡬ	ᢔᡪᡫ
OK	ᢔᡪ	ᢔᡧ	ᢔᡨ
OK
```

The same thing with the separate commands — one key per keystroke, three candidates
each time:

```console
$ printf 'PUSH 0 s\nCAND 0 fuzzy 3\nPUSH 0 a\nCAND 0 fuzzy 3\nPUSH 0 i\nCAND 0 fuzzy 3\nQUIT\n' | zcode_stdio
OK
OK	ᢔ
OK
OK	ᢔᡪ	ᢔᡧ	ᢔᡨ
OK
OK	ᢔᡪᡬ	ᢔᡪᡬᡬ	ᢔᡪᡫ
OK
```

Backspace, and recovery from a bad request:

```console
$ printf 'PUSH 0 s\nPUSH 0 a\nBOGUS\nPOP 0\nKEYS 0\nQUIT\n' | zcode_stdio
OK
OK
ERR	unknown command
OK
OK	s
OK
```

> The candidates render above as Sibe and Manchu letters because ZCode borrows
> those assigned code points; they are only correct Mongolian when shown in a
> ZCode (Z52) form-code font. See `NOTICE.md`.

## Notes for a host

* Use `TYPE` and `UNTYPE` per keystroke. They combine the mutation and the
  query, so a keystroke costs **one** round trip, and the session caches every
  key but the last, so it costs one layer of splicing. `PUSH`+`CAND` gives the
  same answer in two round trips; with a pipe between the processes the extra
  trip costs far more than the splicing saved.
* `CAND` and `SHAPE` agree for the same keys; a test pins that, so a host may
  use `SHAPE` to re-check a finished word.
* Ask for `max` roughly equal to what the candidate window shows. The engine
  can produce very many candidates for a long word. In the shaping modes the
  cap is applied before the list is built; in the dictionary modes it
  truncates the ranked list, and in `score` mode it is what keeps the response
  inside a sane size.
* Prefer `score` over `word` if the host ranks engine candidates against
  candidates of its own: it is the same list, and the `score:` field is what
  the ranking actually used, so the host need not infer it from the order.
* One process serves any number of sessions. Open one per input context and
  close it when the context goes away, or just use session 0 if there is only
  ever one.

Two sessions, independent:

```console
$ printf 'OPEN\nOPEN\nPUSH 1 s\nPUSH 2 m\nKEYS 1\nKEYS 2\nCLOSE 1\nKEYS 1\nQUIT\n' | zcode_stdio
OK	1
OK	2
OK
OK
OK	s
OK	m
OK
ERR	unknown session
OK
```
