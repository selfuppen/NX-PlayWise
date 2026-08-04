#!/usr/bin/env python3
from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys
from urllib.parse import urlsplit


STATIC_ROOT = Path(__file__).with_name("ptc_frontend")
STATIC_ASSETS = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/index.html": ("index.html", "text/html; charset=utf-8"),
    "/styles.css": ("styles.css", "text/css; charset=utf-8"),
    "/app.js": ("app.js", "text/javascript; charset=utf-8"),
    "/token.js": ("token.js", "text/javascript; charset=utf-8"),
    "/storage.js": ("storage.js", "text/javascript; charset=utf-8"),
    "/sw.js": ("sw.js", "text/javascript; charset=utf-8"),
    "/manifest.webmanifest": ("manifest.webmanifest", "application/manifest+json; charset=utf-8"),
    "/icon.svg": ("icon.svg", "image/svg+xml"),
    "/icon-maskable.svg": ("icon-maskable.svg", "image/svg+xml"),
}


class Handler(BaseHTTPRequestHandler):
    def _send_headers(self, status: int, content_type: str, length: int) -> None:
        self.send_response(status)
        self.send_header("content-type", content_type)
        self.send_header("content-length", str(length))
        self.send_header("cache-control", "no-cache")
        self.send_header("x-content-type-options", "nosniff")
        self.send_header("referrer-policy", "no-referrer")
        self.send_header("cross-origin-opener-policy", "same-origin")
        self.send_header(
            "content-security-policy",
            "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; "
            "connect-src 'self'; manifest-src 'self'; worker-src 'self'; base-uri 'none'; "
            "form-action 'self'; frame-ancestors 'none'",
        )
        self.end_headers()

    def _serve_asset(self, *, include_body: bool) -> None:
        path = urlsplit(self.path).path
        asset = STATIC_ASSETS.get(path)
        if asset is None:
            body = b"not_found\n"
            self._send_headers(404, "text/plain; charset=utf-8", len(body))
            if include_body:
                self.wfile.write(body)
            return

        filename, content_type = asset
        try:
            body = STATIC_ROOT.joinpath(filename).read_bytes()
        except OSError as exc:
            message = f"static asset unavailable: {filename}\n".encode("utf-8")
            self._send_headers(500, "text/plain; charset=utf-8", len(message))
            if include_body:
                self.wfile.write(message)
            sys.stderr.write(f"ptc-front: {exc}\n")
            return
        self._send_headers(200, content_type, len(body))
        if include_body:
            self.wfile.write(body)

    def do_GET(self) -> None:
        self._serve_asset(include_body=True)

    def do_HEAD(self) -> None:
        self._serve_asset(include_body=False)

    def do_POST(self) -> None:
        body = b"not_found\n"
        self._send_headers(404, "text/plain; charset=utf-8", len(body))
        self.wfile.write(body)

    def log_message(self, format: str, *args: object) -> None:
        sys.stderr.write("ptc-front: " + (format % args) + "\n")


def serve(host: str, port: int) -> None:
    server = ThreadingHTTPServer((host, port), Handler)
    print(f"http://{host}:{server.server_port}")
    server.serve_forever()


def main() -> int:
    parser = argparse.ArgumentParser(description="Serve the static Offline code generator PWA.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    serve(args.host, args.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
