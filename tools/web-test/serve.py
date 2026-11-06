# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 hugohu789-droid
#
# Serves tools/web-test/index.html and proxies /ime/* to the Java service.
#
# The proxy exists so the page and the API share an origin. The Java service
# sends no CORS headers, so a page loaded from anywhere else cannot read its
# responses -- and the alternative, adding a CORS config to the service, would
# mean editing ime-java, which this project deliberately keeps unmodified.
#
#   python tools/web-test/serve.py            # http://127.0.0.1:8099
#   python tools/web-test/serve.py 9000 8088  # page port, java port

import os
import sys
import urllib.error
import urllib.request
from http.server import HTTPServer, SimpleHTTPRequestHandler

PAGE_PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
JAVA_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8088
JAVA = 'http://127.0.0.1:%d' % JAVA_PORT
HERE = os.path.dirname(os.path.abspath(__file__))


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=HERE, **kw)

    def do_GET(self):
        if not self.path.startswith('/ime/'):
            return super().do_GET()
        # The key is Mongolian or Latin and arrives percent-encoded; pass the
        # path through untouched so the service sees exactly what was typed.
        try:
            with urllib.request.urlopen(JAVA + self.path, timeout=60) as r:
                body, status = r.read(), r.status
        except urllib.error.HTTPError as e:
            body, status = e.read(), e.code
        except Exception as e:
            # Reported as JSON so the page can show it in place of results
            # rather than failing silently.
            body = ('{"ret":false,"error":%s}' % _json_str(str(e))).encode()
            status = 502
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        sys.stderr.write('%s %s\n' % (self.address_string(), fmt % args))


def _json_str(text):
    out = ['"']
    for ch in text:
        if ch in ('"', chr(92)):
            out.append(chr(92) + ch)
        elif ch < ' ':
            out.append('%s u%04x' % (chr(92), ord(ch)).replace(' ', ''))
        else:
            out.append(ch)
    out.append('"')
    return ''.join(out)


def main():
    server = HTTPServer(('127.0.0.1', PAGE_PORT), Handler)
    print('page   http://127.0.0.1:%d/' % PAGE_PORT)
    print('proxy  /ime/*  ->  %s' % JAVA)
    server.serve_forever()


if __name__ == '__main__':
    main()
