"""Проверка сетевого кода лаунчера через изолированный локальный HTTP-сервер."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import threading
import time


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        release = {"tag_name": "v0.3.0", "draft": False, "prerelease": False,
                   "html_url": "http://127.0.0.1:18766/releases/v0.3.0/", "body": "Test notes"}
        code = 200
        if self.path == "/slow":
            time.sleep(1)
        elif self.path == "/current":
            release["tag_name"] = "v0.2.0"
        elif self.path == "/older":
            release["tag_name"] = "v0.1.0"
        elif self.path == "/numeric":
            release["tag_name"] = "v0.10.0"
        elif self.path == "/missing":
            code = 404
        elif self.path == "/limited":
            code = 403
        elif self.path == "/redirect":
            code = 302
        elif self.path in {"/draft", "/prerelease"}:
            release[self.path[1:]] = True
        elif self.path == "/unsafe":
            release["html_url"] = "file:///C:/Windows/System32/cmd.exe"
        elif self.path == "/invalid-version":
            release["tag_name"] = "v0.3.0-beta"
        data = json.dumps(release).encode()
        if self.path == "/invalid-json":
            data = b"not-json"
        elif self.path == "/oversized":
            data = b"x" * (3 * 1024 * 1024)
        self.send_response(code)
        if code == 302:
            self.send_header("Location", "/update")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def log_message(self, *args):
        pass


def main(executable):
    subprocess.run([executable], check=True)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        cases = {"update": "update", "current": "current", "older": "current", "numeric": "update",
                 "missing": "error", "limited": "error", "redirect": "error", "draft": "error",
                 "prerelease": "error", "unsafe": "error", "invalid-version": "error", "invalid-json": "error",
                 "oversized": "error", "slow": "error"}
        for path, expected in cases.items():
            current = "0.3.0" if path == "numeric" else "0.2.0"
            result = subprocess.run([executable, f"http://127.0.0.1:{server.server_port}/{path}", current, expected,
                                     "150" if path == "slow" else "2000"], capture_output=True, text=True, timeout=10)
            assert result.returncode == 0, (path, result.stdout, result.stderr)
        print(f"PASS: {len(cases)} HTTP update scenarios (including 0.2.0 -> 0.3.0), failure handling and timeout")
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main(str(Path(sys.argv[1]).resolve()))
