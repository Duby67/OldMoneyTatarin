"""Проверить автоматическую проверку в полном приложении, в изолированном профиле."""

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time


class Handler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def main(executable, plugin_path):
    temporary = tempfile.mkdtemp(prefix="omt-app-check-", dir=Path(__file__).resolve().parents[2] / "run")
    try:
        root = Path(temporary)
        source = root / "source"
        source.mkdir()
        server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Handler, directory=str(source)))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        url = f"http://127.0.0.1:{server.server_port}/latest.json"
        environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_PLUGIN_PATH=plugin_path)
        try:
            for tag, expected in [("v0.2.0", "Обновлений нет"), ("v0.3.0", "Доступна новая версия")]:
                (source / "latest.json").write_text(json.dumps({"tag_name": tag, "draft": False, "prerelease": False,
                    "html_url": f"http://127.0.0.1:{server.server_port}/", "body": "Isolated smoke test"}), encoding="utf-8")
                profile = root / tag
                profile.mkdir()
                config = profile / "elyprismlauncher.cfg"
                config.write_text("[General]\nLanguage=ru\nIconTheme=pe_colored\nApplicationTheme=bright\n"
                                  "AutomaticJavaDownload=true\nInstanceDirConfigured=true\n"
                                  "LauncherAutoUpdateCheck=true\nLauncherUpdateInterval=0\n"
                                  f"LauncherUpdateSource={url}\n", encoding="utf-8")
                with (root / f"{tag}.log").open("w", encoding="utf-8") as log:
                    process = subprocess.Popen([executable, "--dir", str(profile)], env=environment, stdout=log, stderr=log,
                                               creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                    try:
                        deadline = time.monotonic() + 25
                        found = False
                        while time.monotonic() < deadline and process.poll() is None:
                            if expected in config.read_text(encoding="utf-8"):
                                found = True
                                break
                            time.sleep(0.2)
                        if not found:
                            raise AssertionError(f"Automatic update result not observed for {tag}; exit={process.poll()}")
                        print(f"PASS: full application automatic result for {tag}")
                    finally:
                        if process.poll() is None:
                            process.terminate()
                        process.wait(timeout=10)
        finally:
            server.shutdown()
            server.server_close()
    finally:
        # Оставляем изолированный профиль и журнал для диагностики, без личных аккаунтов.
        print(f"Smoke-test files: {temporary}")


if __name__ == "__main__":
    main(str(Path(sys.argv[1]).resolve()), str(Path(sys.argv[2]).resolve()))
