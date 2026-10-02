#!/usr/bin/env python3
import http.server
import pathlib
import sys


class FixtureHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, format, *args):
        return

    def _send(self, body: bytes) -> None:
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        if self.path != "/get":
            self.send_error(404)
            return
        self._send(b"get-ok\n")

    def do_POST(self) -> None:
        if self.path != "/post":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        self._send(b"post:" + body + b"\n")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: emulator-http-fixture.py PORT READY_FILE", file=sys.stderr)
        return 2

    port = int(sys.argv[1])
    ready_file = pathlib.Path(sys.argv[2])
    server = http.server.HTTPServer(("127.0.0.1", port), FixtureHandler)
    ready_file.write_text(f"{port}\n", encoding="ascii")
    try:
        server.handle_request()
        server.handle_request()
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
