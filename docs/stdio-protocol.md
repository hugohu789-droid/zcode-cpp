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

## Requests

| Request | Meaning |
|---|---|
| `PUSH <key>` | Append one layout key to the session. The key is taken verbatim (a key may be several characters). |
| `POP` | Drop the last key. No-op on an empty session. |
| `CLEAR` | Empty the session. |
| `KEYS` | The session's keys. |
| `CAND <mode> [max] [dedup]` | Candidates for the session's keys. |
| `SHAPE <mode> <latin>` | Stateless: shape a whole Latin string. |
| `DECOMPOSE <latin>` | Every way of splitting the input into layout keys. |
| `QUIT` | End the conversation. The response is sent before the service exits. |

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

Incremental, the way an IME uses it — one key per keystroke, three candidates
each time:

```console
$ printf 'PUSH s\nCAND fuzzy 3\nPUSH a\nCAND fuzzy 3\nPUSH i\nCAND fuzzy 3\nQUIT\n' | zcode_stdio
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
$ printf 'PUSH s\nPUSH a\nBOGUS\nPOP\nKEYS\nQUIT\n' | zcode_stdio
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

* Use a `Session` (`PUSH`/`POP`/`CAND`) rather than re-sending the whole string
  per keystroke: the session caches every key but the last, so a keystroke
  costs one layer of splicing.
* `CAND` and `SHAPE` agree for the same keys; a test pins that, so a host may
  use `SHAPE` to re-check a finished word.
* Ask for `max` roughly equal to what the candidate window shows. The engine
  can produce very many candidates for a long word, and the cap is applied
  before the list is built.
* The service holds exactly one session. A host that needs several
  (one per input context) should run one process per session, or add a session
  id to the protocol.
