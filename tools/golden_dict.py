# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 hugohu789-droid
#
# Captures the Java original's dictionary answers into testdata/golden_dict.tsv.
#
#   python tools/golden_dict.py                 # the usual vocabulary
#   python tools/golden_dict.py bagsi mongol    # just these
#
# Unlike the other golden files this one cannot be produced by tools/golden-java:
# the dictionary lives in MySQL and only the running service can answer for it.
# So this talks to the service over HTTP -- see ../ime-java-run/README.md for
# how to start it -- and records exactly what it said.
#
# Rows
#   severe <TAB> input <TAB> count <TAB> items
#       /ime/severe/{key}. No dictionary; the port must match it exactly.
#   make   <TAB> input <TAB> count <TAB> items
#       /ime/{key}.
#   score  <TAB> input <TAB> count <TAB> items
#       /ime/fuzzy/score/{key}: the word *and* every metric behind its
#       ranking, which is what makes this the row that pins the port. The
#       plain /ime/fuzzy/{key} list is these words without the metrics, so it
#       is not stored separately.
#
# An item is the word as dot-separated upper-case hex code points. A `score`
# item then carries the metrics after a '|', in the order SuggestWord.toString()
# prints them:
#
#   1873.186A|frequency|frequencyScore|levenshteinD|levenshteinScore|jaroWinklerScore|score
#
# The metric values are copied verbatim, as the service printed them, so the
# test compares Java's own float formatting rather than a reparse of it.
#
# The input column is escaped the way tools/golden-java escapes it: a
# backslash, \t, \n, \r, and \uXXXX for anything outside printable ASCII.

import io
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request

SERVICE = 'http://127.0.0.1:8088'
TAB = chr(9)
NL = chr(10)
BACKSLASH = chr(92)


def escape(text):
    out = []
    for ch in text:
        if ch == BACKSLASH:
            out.append(BACKSLASH * 2)
        elif ch == TAB:
            out.append(BACKSLASH + 't')
        elif ch == NL:
            out.append(BACKSLASH + 'n')
        elif ch == chr(13):
            out.append(BACKSLASH + 'r')
        elif ' ' <= ch <= '~':
            out.append(ch)
        else:
            for unit in ch.encode('utf-16-be'):
                pass
            # Java escapes per UTF-16 unit, so an astral character becomes two.
            data = ch.encode('utf-16-be')
            for i in range(0, len(data), 2):
                out.append('%su%04X' % (BACKSLASH, (data[i] << 8) | data[i + 1]))
    return ''.join(out)


def hexed(word):
    return '.'.join('%04X' % ord(c) for c in word)


def get(path, key):
    url = SERVICE + path + urllib.parse.quote(key, safe='')
    with urllib.request.urlopen(url, timeout=300) as r:
        return json.loads(r.read().decode('utf-8'))['data']['obj']


def score_item(raw):
    """'word,frequency:N,...' -> 'HEX|N|...' keeping the printed values."""
    parts = raw.split(',')
    values = [p.split(':', 1)[1] for p in parts[1:] if ':' in p]
    return '|'.join([hexed(parts[0])] + values)


def vocabulary():
    """The golden inputs, plus every prefix of the word-like ones.

    The prefixes matter more than the whole words: an input method asks
    after every keystroke, so most of the queries it ever makes are
    prefixes, and they are where the dictionary comes back empty.
    """
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    path = os.path.join(here, 'testdata', 'golden_e2e.tsv')
    inputs = []
    seen = set()
    with io.open(path, encoding='utf-8') as f:
        for line in f:
            if line.startswith('#'):
                continue
            fields = line.rstrip(NL).rstrip(chr(13)).split(TAB)
            if len(fields) < 2 or fields[0] != 'severe':
                continue
            word = unescape(fields[1])
            for candidate in [word] + prefixes(word):
                if candidate not in seen:
                    seen.add(candidate)
                    inputs.append(candidate)
    return inputs


def prefixes(word):
    if not word.isascii() or not word.isalpha() or len(word) < 3:
        return []
    return [word[:n] for n in range(1, len(word))]


def unescape(text):
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
        elif nxt in 'tnr':
            out.append({'t': TAB, 'n': NL, 'r': chr(13)}[nxt])
            i += 2
        else:
            out.append(nxt)
            i += 2
    # Put surrogate pairs back together: Java escaped them per UTF-16 unit.
    text = ''.join(out)
    try:
        return text.encode('utf-16', 'surrogatepass').decode('utf-16')
    except UnicodeDecodeError:
        return text


def main():
    words = sys.argv[1:] or vocabulary()
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_path = os.path.join(here, 'testdata', 'golden_dict.tsv')

    try:
        urllib.request.urlopen(SERVICE + '/ime/severe/sain', timeout=5).read()
    except Exception as e:
        sys.stderr.write('the Java service is not answering on %s: %s\n'
                         'start it with ../ime-java-run/start-ime.cmd\n'
                         % (SERVICE, e))
        return 1

    rows = []
    skipped = []
    for word in words:
        if not word.strip() or any(ord(c) < 0x20 for c in word):
            # The service takes the key in a path segment; a control
            # character cannot go there, and the port rejects it too.
            skipped.append(word)
            continue
        try:
            severe = get('/ime/severe/', word)
            make = get('/ime/', word)
            score = get('/ime/fuzzy/score/', word)
        except urllib.error.HTTPError as e:
            skipped.append(word)
            sys.stderr.write('%s: HTTP %d\n' % (word, e.code))
            continue
        rows.append(('severe', word, [hexed(w) for w in severe]))
        rows.append(('make', word, [hexed(w) for w in make]))
        rows.append(('score', word, [score_item(s) for s in score]))

    with io.open(out_path, 'w', encoding='utf-8', newline=NL) as f:
        f.write('# SPDX-License-Identifier: GPL-2.0-or-later' + NL)
        f.write('# generated by tools/golden_dict.py from the running '
                'east-mod/ime service; do not edit' + NL)
        f.write('# what the original answers for the dictionary endpoints; '
                'see that script for the format' + NL)
        f.write('# %d inputs, %d rows' % (len(rows) // 3, len(rows)) + NL)
        f.write('# kind' + TAB + 'input(escaped)' + TAB + 'count' + TAB +
                'items' + NL)
        for kind, word, items in rows:
            f.write(TAB.join([kind, escape(word), str(len(items)),
                              ' '.join(items)]) + NL)

    size = os.path.getsize(out_path)
    print('%s: %d inputs, %d rows, %.1f MB%s'
          % (out_path, len(rows) // 3, len(rows), size / 1048576.0,
             ', %d inputs skipped' % len(skipped) if skipped else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
