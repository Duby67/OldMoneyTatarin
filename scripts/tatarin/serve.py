"""Локальная HTTP-раздача TATARIN: только IPv4 loopback, без Docker."""

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def log_message(self, format, *args):
        # URL и пути могут содержать пользовательские данные; журнал запросов не нужен.
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--port", type=int, default=18765)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    if not (root / "v1/catalog.json").is_file():
        parser.error("В каталоге нет v1/catalog.json")
    print(f"TATARIN: http://127.0.0.1:{args.port}/v1/catalog.json", flush=True)
    with ThreadingHTTPServer(("127.0.0.1", args.port), partial(Handler, directory=str(root))) as server:
        server.serve_forever()
