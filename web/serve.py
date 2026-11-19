#!/usr/bin/env python3
"""Local server for the Sonic Generations wasm build.

The runtime is compiled with -sUSE_PTHREADS, which requires SharedArrayBuffer;
browsers only expose it when the page is served with the COOP/COEP headers.
Python's http.server does not send them, so run THIS server, not
`python -m http.server`:

    python3 serve.py [port]     # default 8080, then open http://localhost:8080
"""
import http.server
import os
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # Required for SharedArrayBuffer / pthreads
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # WASM + cache friendlyness
        self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
        super().end_headers()

    def guess_type(self, path):
        if path.endswith(".wasm"):
            return "application/wasm"
        return super().guess_type(path)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    server = http.server.ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"serving on http://localhost:{port}  (COOP/COEP enabled)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
