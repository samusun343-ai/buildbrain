#!/usr/bin/env python3
"""HTTP server for disc-cache tests: serves the test site with
explicit cache headers (python -m http.server sends none, which
makes llcache refuse to persist anything)."""
import http.server
import os
import sys

SITE = os.path.expanduser(
    "~/workspace/buildbrain-work/rebuild/browser-engine/test-site")


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=SITE, **kw)

    def end_headers(self):
        self.send_header("Cache-Control", "max-age=3600")
        super().end_headers()

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    http.server.HTTPServer(("127.0.0.1", 8932), Handler).serve_forever()
