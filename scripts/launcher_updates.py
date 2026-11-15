"""Локальная публикация и HTTP-имитация GitHub Releases для проверки лаунчера."""

import argparse
from functools import partial
import html
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import shutil
from urllib.parse import urlparse

from prepare_release import prepare_release


def publish(root, installer, base_url):
    repository = Path(__file__).resolve().parents[1]
    tag = (repository / "VERSION").read_text(encoding="utf-8").strip()
    notes = prepare_release(repository, tag)
    url = urlparse(base_url)
    if url.scheme != "http" or url.hostname != "127.0.0.1" or url.path not in {"", "/"} or url.username or url.password or url.query or url.fragment:
        raise ValueError("Для симуляции нужен адрес вида http://127.0.0.1:18766/")
    if installer.name != f"OldMoneyTatarin-{tag[1:]}-Setup-x64.exe":
        raise ValueError("Имя установщика не соответствует VERSION")
    page = root / "releases" / tag / "index.html"
    if page.exists():
        raise ValueError("Выпуск уже опубликован локально; используйте новый каталог для повторного теста")
    if not installer.is_file():
        raise ValueError("Сначала соберите установщик")
    payload = root / "files" / installer.name
    payload.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(installer, payload)
    page.parent.mkdir(parents=True, exist_ok=True)
    page.write_text(f'<!doctype html><html lang="ru"><meta charset="utf-8"><title>OldMoneyTatarin {tag}</title>'
                    f'<h1>OldMoneyTatarin {tag}</h1><pre>{html.escape(notes)}</pre>'
                    f'<a href="../../files/{html.escape(installer.name)}">Скачать установщик</a></html>', encoding="utf-8")
    metadata = {"tag_name": tag, "draft": False, "prerelease": False, "body": notes,
                "html_url": base_url.rstrip("/") + f"/releases/{tag}/"}
    temporary = root / "latest.tmp"
    temporary.write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(root / "latest.json")
    print(f"Локально опубликован {tag}; источник: {base_url.rstrip('/')}/latest.json")


class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    publication = commands.add_parser("publish")
    publication.add_argument("--root", type=Path, default=Path("run/launcher-updates"))
    publication.add_argument("--installer", type=Path, required=True)
    publication.add_argument("--url", default="http://127.0.0.1:18766/")
    serving = commands.add_parser("serve")
    serving.add_argument("--root", type=Path, default=Path("run/launcher-updates"))
    serving.add_argument("--port", type=int, default=18766)
    args = parser.parse_args()
    if args.command == "publish":
        publish(args.root.resolve(), args.installer.resolve(), args.url)
    else:
        root = args.root.resolve(strict=True)
        if not (root / "latest.json").is_file():
            parser.error("В каталоге нет latest.json")
        print(f"Источник обновлений: http://127.0.0.1:{args.port}/latest.json", flush=True)
        with ThreadingHTTPServer(("127.0.0.1", args.port), partial(Handler, directory=str(root))) as server:
            server.serve_forever()


if __name__ == "__main__":
    main()
