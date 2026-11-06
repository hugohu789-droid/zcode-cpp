# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 hugohu789-droid
#
# Serves index.html and puts the C++ engine and the Java original behind one
# origin, so the two can be compared candidate by candidate in a browser.
#
#   python tools/web-compare/compare.py                 # http://127.0.0.1:8098
#   python tools/web-compare/compare.py 9000 8088       # page port, java port
#   ZCODE_STDIO=...\zcode_stdio.exe python ...          # explicit engine path
#
# What is actually comparable
# ---------------------------
# This library is the splicing engine only: no dictionary, no database. Of the
# Java service's four endpoints, exactly one is the splicer and nothing else:
#
#   /ime/severe/{key}      decompose + LetterSplicer.severe       <- comparable
#   /ime/fuzzy/{key}       splicer output used as BK-tree keys,
#                          dictionary words returned              <- not
#   /ime/fuzzy/score/{key} same, with the match metrics            <- not
#   /ime/{key}             severe union fuzzy                      <- not
#
# So `severe` is compared against the live Java service, and `fuzzy` against
# testdata/golden_e2e.tsv, which is the Java splicer's own fuzzy output dumped
# by tools/golden-java. The HTTP API never exposes that list, and the only
# other way to get it would be to add an endpoint to ime-java, which this
# project keeps unmodified.
#
# The proxy exists because the Java service sends no CORS headers, so a page
# on another origin can send a request but cannot read the reply.

import json
import os
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, quote, unquote, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

PAGE_PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8098
JAVA_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8088
JAVA = 'http://127.0.0.1:%d' % JAVA_PORT

BACKSLASH = chr(92)
MODES = ('severe', 'fuzzy')


# ---------------------------------------------------------------- the engine

class EngineError(Exception):
    pass


def find_engine():
    named = os.environ.get('ZCODE_STDIO')
    if named:
        return named
    suffix = '.exe' if os.name == 'nt' else ''
    for rel in ('build', os.path.join('build', 'Release'),
                os.path.join('build-vs', 'Release'),
                os.path.join('build-vs', 'Debug'), 'build-asan'):
        path = os.path.join(REPO, rel, 'zcode_stdio' + suffix)
        if os.path.exists(path):
            return path
    return None


class Engine:
    """One long-lived zcode_stdio process, one request at a time.

    The service is a conversation on a pipe, so a lock is not an optimisation
    choice but the protocol: two interleaved requests would read each other's
    response line. A dead process is restarted on the next request, which is
    what makes the page survive rebuilding the engine underneath it.
    """

    def __init__(self, exe):
        self.exe = exe
        self.lock = threading.Lock()
        self.proc = None
        self.hello = None
        self.started = 0

    def _spawn(self):
        self.proc = subprocess.Popen(
            [self.exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, cwd=os.path.dirname(self.exe))
        self.started += 1
        self.hello = self._exchange('HELLO')

    def _exchange(self, line):
        for ch in line:
            # A control character is what would break the line framing, and
            # the engine rejects one rather than stripping it; catching it
            # here keeps a bad request out of the pipe entirely.
            if ord(ch) < 0x20:
                raise EngineError('control character in request')
        self.proc.stdin.write((line + chr(10)).encode('utf-8'))
        self.proc.stdin.flush()
        raw = self.proc.stdout.readline()
        if not raw:
            raise EngineError('engine closed the pipe')
        # The child writes in text mode on Windows, so a CR arrives too; the
        # protocol tolerates it in both directions.
        fields = raw.decode('utf-8').rstrip(chr(13) + chr(10)).split(chr(9))
        if fields[0] == 'ERR':
            raise EngineError(fields[1] if len(fields) > 1 else 'rejected')
        if fields[0] != 'OK':
            raise EngineError('unexpected response: ' + fields[0])
        return fields[1:]

    def request(self, line):
        with self.lock:
            if self.proc is None or self.proc.poll() is not None:
                self._spawn()
            try:
                return self._exchange(line)
            except EngineError:
                raise
            except OSError:
                # Broken pipe: the process went away between the poll and the
                # write. One retry on a fresh process, then give up.
                self._spawn()
                return self._exchange(line)

    def shape(self, mode, latin):
        return self.request('SHAPE %s %s' % (mode, latin))

    def decompose(self, latin):
        return [item.split(' ') for item in self.request('DECOMPOSE ' + latin)]

    def version(self):
        with self.lock:
            if self.proc is None or self.proc.poll() is not None:
                self._spawn()
            return list(self.hello or [])


# ------------------------------------------------------------- golden tables

def unescape(text):
    """Reverses GoldenDump.escape(): \\\\ \\t \\n \\r and \\uXXXX.

    Returns (text, had_lone_surrogate).
    """
    out = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch != BACKSLASH or i + 1 >= len(text):
            out.append(ch)
            i += 1
            continue
        nxt = text[i + 1]
        if nxt == 'u' and i + 6 <= len(text):
            out.append(chr(int(text[i + 2:i + 6], 16)))
            i += 6
        elif nxt == 't':
            out.append(chr(9))
            i += 2
        elif nxt == 'n':
            out.append(chr(10))
            i += 2
        elif nxt == 'r':
            out.append(chr(13))
            i += 2
        else:
            out.append(nxt)
            i += 2
    return _join_surrogates(out)


def _join_surrogates(chars):
    """GoldenDump escapes per UTF-16 char, so an astral input arrives as a
    surrogate pair. Python strings hold code points, and a lone surrogate
    cannot be encoded as UTF-8 at all, so the pairs have to be put back
    together here. An unpaired one is kept as U+FFFD and reported, which is
    what excludes that input from the batch run instead of silently comparing
    a different string."""
    out = []
    i = 0
    lost = False
    while i < len(chars):
        code = ord(chars[i])
        if 0xD800 <= code <= 0xDBFF and i + 1 < len(chars) \
                and 0xDC00 <= ord(chars[i + 1]) <= 0xDFFF:
            out.append(chr(0x10000 + ((code - 0xD800) << 10)
                           + (ord(chars[i + 1]) - 0xDC00)))
            i += 2
            continue
        if 0xD800 <= code <= 0xDFFF:
            out.append(chr(0xFFFD))
            lost = True
            i += 1
            continue
        out.append(chars[i])
        i += 1
    return ''.join(out), lost


def rows(path):
    with open(path, encoding='utf-8') as f:
        for line in f:
            if line.startswith('#'):
                continue
            fields = line.rstrip(chr(10)).rstrip(chr(13)).split(chr(9))
            if fields and fields[0]:
                yield fields


def hex_run(text):
    return '.'.join('%04X' % ord(ch) for ch in text)


def fnv1a64(items):
    """GoldenDump.fnv1a64(candidates(...)): the digest the dump stores in
    place of a list longer than 2000, so those three inputs can still be
    compared -- by count and digest rather than item by item."""
    ascii_form = ' '.join(hex_run(item) for item in items)
    h = 0xcbf29ce484222325
    for ch in ascii_form:
        h = ((h ^ (ord(ch) & 0xFF)) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return '%016X' % h


class Golden:
    """The Java original's own answers, dumped offline by tools/golden-java.

    This is the only access to the Java splicer's *fuzzy* list: the HTTP API
    puts that list through the dictionary before returning anything.
    """

    def __init__(self):
        self.shape = {}      # (mode, input) -> {items|digest, count}
        self.decomp = {}     # input -> [[key, ...], ...]
        self.words = []      # inputs the batch run may use, in file order
        self.lossy = []      # inputs this tool cannot reproduce exactly
        self.loaded = []
        self._load_shape()
        self._load_decompose()

    def _load_shape(self):
        path = os.path.join(REPO, 'testdata', 'golden_e2e.tsv')
        if not os.path.exists(path):
            return
        seen = set()
        for f in rows(path):
            if len(f) < 4 or f[0] not in MODES:
                continue
            word, lossy = unescape(f[1])
            count = int(f[2]) if f[2].isdigit() else None
            if f[3].startswith('H:'):
                # Over 2000 candidates: the dump keeps only the digest.
                entry = {'items': None, 'count': count, 'digest': f[3][2:]}
            else:
                items = []
                for item in f[3].split(' '):
                    if item:
                        items.append(''.join(chr(int(p, 16))
                                             for p in item.split('.')))
                entry = {'items': items, 'count': count, 'digest': None}
            self.shape[(f[0], word)] = entry
            if word not in seen:
                seen.add(word)
                if lossy:
                    self.lossy.append(f[1])
                else:
                    self.words.append(word)
        self.loaded.append('golden_e2e.tsv (%d inputs%s)' % (
            len(self.words),
            ',另有 %d 个无法还原' % len(self.lossy) if self.lossy else ''))

    def _load_decompose(self):
        path = os.path.join(REPO, 'testdata', 'golden_decompose.tsv')
        if not os.path.exists(path):
            return
        for f in rows(path):
            if len(f) < 3:
                continue
            # An input with no layout keys in it decomposes to *one* empty
            # split, not to nothing, and the dump writes that as an empty
            # field -- indistinguishable from a count of zero unless the
            # count column is read. Dropping the distinction made eight
            # inputs ("A", "-", "v", "1", ...) look like port bugs.
            count = int(f[1]) if f[1].isdigit() else None
            raw = f[2]
            if count == 0:
                splits = []
            elif raw == '':
                splits = [[] for _ in range(count or 0)]
            else:
                splits = [s.split(' ') if s else [] for s in raw.split('|')]
            self.decomp[unescape(f[0])[0]] = splits
        self.loaded.append('golden_decompose.tsv (%d inputs)' % len(self.decomp))


# ------------------------------------------------------------------ the java

def java_get(path, timeout=60):
    """Returns (items, run_ms, error)."""
    try:
        with urllib.request.urlopen(JAVA + path, timeout=timeout) as r:
            body = r.read()
    except urllib.error.HTTPError as e:
        return None, None, 'HTTP %d' % e.code
    except Exception as e:  # connection refused, timeout, ...
        return None, None, str(e)
    try:
        data = json.loads(body.decode('utf-8'))
    except ValueError:
        return None, None, 'response was not JSON'
    payload = data.get('data')
    if isinstance(payload, list):
        return payload, None, None
    if isinstance(payload, dict) and isinstance(payload.get('obj'), list):
        return payload['obj'], payload.get('runTime'), None
    return [], None, None


# ------------------------------------------------------------- the comparison

def diff(a, b):
    """Element-wise comparison of two candidate lists.

    `same` is the strict verdict: same items, same order, same duplicates.
    A list may legitimately repeat a candidate (different shapes with the
    same components), so the extras are a multiset difference, not a set one.
    """
    same = a == b
    first = None
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            first = i
            break
    if first is None and len(a) != len(b):
        first = min(len(a), len(b))

    rest_b = list(b)
    only_a = []
    for item in a:
        if item in rest_b:
            rest_b.remove(item)
        else:
            only_a.append(item)
    rest_a = list(a)
    only_b = []
    for item in b:
        if item in rest_a:
            rest_a.remove(item)
        else:
            only_b.append(item)
    return {
        'same': same,
        'sameAsMultiset': not only_a and not only_b,
        'countA': len(a),
        'countB': len(b),
        'firstDiff': first,
        'onlyA': only_a[:40],
        'onlyB': only_b[:40],
    }


def join_keys(splits):
    return [' '.join(k) for k in splits]


def sides_for(kind, latin, engine, golden, want_java=True):
    """The lists to compare, in display order, with `cpp` always present."""
    sides = []
    if kind == 'decompose':
        if want_java:
            got = golden.decomp.get(latin)
            sides.append({
                'id': 'golden', 'label': 'Java 原版 · Decomposer（golden 文件）',
                'ok': got is not None,
                'error': None if got is not None else 'golden 文件里没有这个输入',
                'obj': join_keys(got or []), 'runTime': None,
            })
        t0 = time.perf_counter()
        try:
            items = join_keys(engine.decompose(latin))
            err = None
        except EngineError as e:
            items, err = [], str(e)
        sides.append({
            'id': 'cpp', 'label': 'C++ zcode_stdio · DECOMPOSE',
            'ok': err is None, 'error': err, 'obj': items,
            'runTime': round((time.perf_counter() - t0) * 1000, 1),
        })
        return sides

    if want_java:
        if kind == 'severe':
            # The one endpoint that is the splicer and nothing else. The key
            # goes in a path segment, so it has to be percent-encoded: the
            # golden vocabulary includes inputs with spaces and non-ASCII
            # characters, and urllib refuses those outright.
            items, ms, err = java_get('/ime/severe/' + quote(latin, safe=''))
            sides.append({
                'id': 'java', 'label': 'Java 服务 · /ime/severe/{key}',
                'ok': err is None, 'error': err, 'obj': items or [],
                'runTime': ms,
            })
        got = golden.shape.get((kind, latin))
        side = {
            'id': 'golden',
            'label': 'Java 原版 · LetterSplicer.%s（golden 文件）' % kind,
            'ok': got is not None,
            'error': None if got is not None else 'golden 文件里没有这个输入',
            'obj': [], 'runTime': None, 'digestOnly': False,
        }
        if got is not None:
            if got['items'] is None:
                side['digestOnly'] = True
                side['count'] = got['count']
                side['digest'] = got['digest']
            else:
                side['obj'] = got['items']
        sides.append(side)

    t0 = time.perf_counter()
    try:
        items = engine.shape(kind, latin)
        err = None
    except EngineError as e:
        items, err = [], str(e)
    sides.append({
        'id': 'cpp', 'label': 'C++ zcode_stdio · SHAPE %s' % kind,
        'ok': err is None, 'error': err, 'obj': items,
        'runTime': round((time.perf_counter() - t0) * 1000, 1),
    })
    return sides


def verdict_for(sides):
    """Every other side against `cpp`: that is the thing under test."""
    cpp = next((s for s in sides if s['id'] == 'cpp'), None)
    pairs = []
    if cpp is None or not cpp['ok']:
        return {'same': None, 'pairs': pairs}
    for side in sides:
        if side['id'] == 'cpp' or not side['ok']:
            continue
        if side.get('digestOnly'):
            # Too many candidates to have been listed: count and digest is
            # the whole comparison, and it is still exact.
            got = fnv1a64(cpp['obj'])
            same = (side['count'] == len(cpp['obj']) and side['digest'] == got)
            pairs.append({
                'a': side['id'], 'b': 'cpp', 'how': 'digest', 'same': same,
                'sameAsMultiset': same, 'countA': side['count'],
                'countB': len(cpp['obj']), 'firstDiff': None,
                'onlyA': [], 'onlyB': [],
                'digestA': side['digest'], 'digestB': got,
            })
            continue
        d = diff(side['obj'], cpp['obj'])
        d['a'] = side['id']
        d['b'] = 'cpp'
        d['how'] = 'items'
        pairs.append(d)
    if not pairs:
        return {'same': None, 'pairs': pairs}

    # The running service is the unmodified original; the golden file is the
    # same code with this project's two documented fixes applied (see README,
    # "Deviations from the Java original"). So when the live service differs
    # from C++ at an input where the patched original does not, the difference
    # is one of those fixes -- not a porting error. Deciding that from the
    # two pairs, instead of listing the affected inputs, keeps the harness
    # from having to be updated whenever the patch set changes.
    golden_agrees = any(p['a'] == 'golden' and p['same'] for p in pairs)
    for p in pairs:
        p['knownPatch'] = bool(p['a'] == 'java' and not p['same']
                               and golden_agrees)
    return {
        'same': all(p['same'] for p in pairs),
        'sameExceptPatches': all(p['same'] or p['knownPatch'] for p in pairs),
        'pairs': pairs,
    }


# ---------------------------------------------------------------- the server

ENGINE = None
GOLDEN = None


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=HERE, **kw)

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)
        try:
            if path.startswith('/ime/'):
                return self.proxy_java()
            if path == '/api/info':
                return self.info()
            if path.startswith('/api/cpp/shape/'):
                return self.cpp_shape(path)
            if path.startswith('/api/cpp/decompose/'):
                return self.cpp_decompose(path)
            if path.startswith('/api/golden/'):
                return self.golden_one(path)
            if path.startswith('/api/compare/'):
                return self.compare(path)
            if path.startswith('/api/batch/'):
                return self.batch(path, query)
            if path == '/api/words':
                return self.send_json({'obj': GOLDEN.words})
        except EngineError as e:
            return self.send_json({'error': '引擎: ' + str(e)}, ok=False,
                                  status=502)
        except Exception as e:  # noqa: BLE001 - reported, not swallowed
            return self.send_json({'error': '%s: %s' % (type(e).__name__, e)},
                                  ok=False, status=500)
        return super().do_GET()

    # -- plumbing

    def send_json(self, data, ok=True, status=200):
        body = json.dumps({'ret': ok, 'retCode': 30200 if ok else 50000,
                           'data': data}, ensure_ascii=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def proxy_java(self):
        # Pass the path through untouched so the service sees exactly what was
        # typed, percent-encoding included.
        try:
            with urllib.request.urlopen(JAVA + self.path, timeout=60) as r:
                body, status = r.read(), r.status
        except urllib.error.HTTPError as e:
            body, status = e.read(), e.code
        except Exception as e:
            return self.send_json({'error': str(e)}, ok=False, status=502)
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def tail(self, path, prefix):
        return unquote(path[len(prefix):], encoding='utf-8')

    # -- endpoints

    def info(self):
        java_items, _, java_err = java_get('/ime/severe/sain', timeout=3)
        try:
            hello = ENGINE.version()
            engine_err = None
        except EngineError as e:
            hello, engine_err = [], str(e)
        self.send_json({
            'engine': {
                'path': ENGINE.exe, 'hello': hello, 'error': engine_err,
                'restarts': max(0, ENGINE.started - 1),
            },
            'java': {'url': JAVA, 'up': java_err is None, 'error': java_err,
                     'probe': java_items or []},
            'golden': GOLDEN.loaded,
            'words': len(GOLDEN.words),
            'lossy': GOLDEN.lossy,
        })

    def cpp_shape(self, path):
        rest = self.tail(path, '/api/cpp/shape/')
        mode, _, latin = rest.partition('/')
        if mode not in MODES:
            return self.send_json({'error': 'mode must be severe or fuzzy'},
                                  ok=False, status=400)
        t0 = time.perf_counter()
        items = ENGINE.shape(mode, latin)
        self.send_json({'obj': items,
                        'runTime': round((time.perf_counter() - t0) * 1000, 1)})

    def cpp_decompose(self, path):
        latin = self.tail(path, '/api/cpp/decompose/')
        t0 = time.perf_counter()
        items = join_keys(ENGINE.decompose(latin))
        self.send_json({'obj': items,
                        'runTime': round((time.perf_counter() - t0) * 1000, 1)})

    def golden_one(self, path):
        rest = self.tail(path, '/api/golden/')
        kind, _, latin = rest.partition('/')
        if kind == 'decompose':
            got = GOLDEN.decomp.get(latin)
            return self.send_json({'obj': join_keys(got or []),
                                   'found': got is not None})
        if kind not in MODES:
            return self.send_json({'error': 'unknown kind'}, ok=False,
                                  status=400)
        got = GOLDEN.shape.get((kind, latin))
        if got is None:
            return self.send_json({'obj': [], 'found': False})
        self.send_json({'obj': got['items'] or [], 'found': True,
                        'count': got['count'], 'digest': got['digest'],
                        'digestOnly': got['items'] is None})

    def compare(self, path):
        rest = self.tail(path, '/api/compare/')
        kind, _, latin = rest.partition('/')
        if kind not in ('severe', 'fuzzy', 'decompose'):
            return self.send_json({'error': 'unknown kind'}, ok=False,
                                  status=400)
        if not latin.strip():
            return self.send_json({'error': 'empty input'}, ok=False,
                                  status=400)
        sides = sides_for(kind, latin, ENGINE, GOLDEN)
        self.send_json({'kind': kind, 'input': latin, 'sides': sides,
                        'verdict': verdict_for(sides)})

    def batch(self, path, query):
        """The whole golden vocabulary through both sides, mismatches listed.

        This is the part no unit test covers: the test suite checks C++
        against the golden *file*, and nothing checks the running Java service
        against C++ over a vocabulary.
        """
        kind = self.tail(path, '/api/batch/')
        if kind not in ('severe', 'fuzzy', 'decompose'):
            return self.send_json({'error': 'unknown kind'}, ok=False,
                                  status=400)
        limit = int((query.get('limit') or ['0'])[0] or 0)
        offset = int((query.get('offset') or ['0'])[0] or 0)
        words = GOLDEN.words[offset:]
        if limit > 0:
            words = words[:limit]

        counts = {}
        mismatches = []
        skipped = []
        t0 = time.perf_counter()
        for word in words:
            sides = sides_for(kind, word, ENGINE, GOLDEN)
            for pair in verdict_for(sides)['pairs']:
                key = pair['a'] + ' vs ' + pair['b']
                slot = counts.setdefault(key, {'same': 0, 'differ': 0,
                                                'knownPatch': 0})
                if pair['same']:
                    slot['same'] += 1
                else:
                    slot['differ'] += 1
                    if pair.get('knownPatch'):
                        slot['knownPatch'] += 1
                    if len(mismatches) < 200:
                        mismatches.append({
                            'input': word, 'pair': key,
                            'countA': pair['countA'], 'countB': pair['countB'],
                            'firstDiff': pair['firstDiff'],
                            'sameAsMultiset': pair['sameAsMultiset'],
                            'knownPatch': bool(pair.get('knownPatch')),
                            'onlyA': pair['onlyA'][:6],
                            'onlyB': pair['onlyB'][:6],
                        })
            for side in sides:
                if not side['ok'] and len(skipped) < 20:
                    skipped.append({'input': word, 'side': side['id'],
                                    'error': side['error']})
        self.send_json({
            'kind': kind, 'tested': len(words), 'offset': offset,
            'counts': counts, 'mismatches': mismatches, 'skipped': skipped,
            'elapsedMs': round((time.perf_counter() - t0) * 1000),
        })

    def log_message(self, fmt, *args):
        sys.stderr.write('%s %s\n' % (self.address_string(), fmt % args))


def main():
    global ENGINE, GOLDEN
    exe = find_engine()
    if not exe:
        sys.stderr.write(
            'zcode_stdio not found. Build it:\n'
            '  cmake --build build --target zcode_stdio\n'
            'or set ZCODE_STDIO to its path.\n')
        return 1
    ENGINE = Engine(exe)
    GOLDEN = Golden()
    print('page    http://127.0.0.1:%d/' % PAGE_PORT)
    print('engine  %s' % exe)
    print('java    %s  (proxied at /ime/*)' % JAVA)
    for line in GOLDEN.loaded:
        print('golden  %s' % line)
    if not GOLDEN.loaded:
        print('golden  none found under testdata/ -- fuzzy has nothing to '
              'compare against')
    ThreadingHTTPServer(('127.0.0.1', PAGE_PORT), Handler).serve_forever()
    return 0


if __name__ == '__main__':
    sys.exit(main())
