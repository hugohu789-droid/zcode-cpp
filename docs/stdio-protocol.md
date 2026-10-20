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

`<mode>` is `severe` or `fuzzy`:

* **severe** — full orthographic rules, the last letter in its final form. Use
  for a word the user has finished.
* **fuzzy** — relaxed rules, the last letter medial or final. Use while the
  user is still typing, since it is a prefix search.

`[max]` caps the number of candidates (`0` = no cap) and `[dedup]` is `0` or
`1`. Both default to the library's `Options` defaults.

## Responses

| Response | Meaning |
|---|---|
| `OK` | Success, no payload. |
| `OK<TAB>item<TAB>item...` | Success with a payload. |
| `ERR<TAB><message>` | The request was rejected. |

A rejected request leaves the session untouched, so a client can recover
without resynchronising. `DECOMPOSE` returns one item per split, the keys of a
split joined by spaces, because a split is itself a list.

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
  can produce very many candidates for a long word, and the cap is applied
  before the list is built.
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
