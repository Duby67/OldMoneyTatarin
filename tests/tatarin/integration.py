"""Интеграционные проверки реального C++ клиента через локальный HTTP."""

import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from functools import partial


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


class Handler(SimpleHTTPRequestHandler):
    requests = []

    def do_GET(self):
        self.requests.append(self.path)
        super().do_GET()

    def log_message(self, *args):
        pass


def main(executable):
    with tempfile.TemporaryDirectory(prefix="tatarin-tests-") as temporary:
        root = Path(temporary)
        server_root = root / "server"
        server_root.mkdir()
        instance = root / "instance"
        game = instance / "minecraft"
        server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Handler, directory=str(server_root)))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        url = f"http://127.0.0.1:{server.server_port}/"

        def run(mode, success=True):
            result = subprocess.run([executable, url, str(instance), mode], capture_output=True, text=True, encoding="utf-8")
            assert (result.returncode == 0) == success, (mode, result.stdout, result.stderr)

        def release(name, contents):
            files = []
            for path, data in contents.items():
                data = data.encode()
                obj = server_root / "objects" / sha(data)
                obj.parent.mkdir(exist_ok=True)
                obj.write_bytes(data)
                files.append({"path": path, "size": len(data), "sha256": sha(data), "policy": "managed"})
            value = {"schema": 1, "id": "regionalworlds", "name": "Test", "release": name,
                     "minecraft": "1.21.1", "neoforge": "21.1.264", "files": files}
            publish(value)
            return value

        def publish(value):
            relative = f"v1/packs/regionalworlds/releases/{value['release']}.json"
            path = server_root / relative
            write_json(path, value)
            write_json(server_root / "v1/catalog.json", {"schema": 1, "packs": [
                {"id": "regionalworlds", "name": "Test", "release": value["release"],
                 "manifest": relative, "sha256": sha(path.read_bytes())}]})

        try:
            first = release("test-1", {"mods/a.jar": "old-a", "mods/remove.jar": "remove", "config/main.toml": "old-config"})
            run("install")
            (game / "saves").mkdir()
            (game / "saves/world.txt").write_text("world")
            (game / "options.txt").write_text("user-options")
            (game / "mods/user.jar").write_text("user-mod")
            second = release("test-2", {"mods/a.jar": "new-a", "mods/new.jar": "new", "config/main.toml": "new-config"})
            run("sync")
            assert (game / "mods/a.jar").read_text() == "new-a"
            assert not (game / "mods/remove.jar").exists()
            assert (game / "mods/user.jar").read_text() == "user-mod"
            assert (game / "saves/world.txt").read_text() == "world"
            assert (game / "options.txt").read_text() == "user-options"
            Handler.requests.clear()
            run("sync")
            assert not any(p.startswith("/objects/") for p in Handler.requests), Handler.requests
            (game / "mods/a.jar").write_text("damaged")
            run("verify", False)
            run("sync")
            run("verify")
            # Ошибка после начала применения: часть файлов заменена, журнал остаётся.
            fourth = release("test-4", {"mods/a.jar": "fourth-a", "config/main.toml": "fourth-config", "mods/z-block.jar": "block"})
            run("failapply", False)
            assert (instance / "tatarin/pending.json").exists()
            assert (game / "config/main.toml").read_text() == "fourth-config"
            (game / "mods/z-block.jar").rmdir()
            run("verify")
            assert (game / "config/main.toml").read_text() == "new-config"
            assert (game / "mods/a.jar").read_text() == "new-a"
            third = release("test-3", {"mods/a.jar": "third-a", "mods/new.jar": "third-new", "config/main.toml": "third-config"})
            run("cancel", False)
            assert (game / "mods/a.jar").read_text() == "new-a"
            assert json.loads((instance / "tatarin/installed.json").read_text())["release"] == "test-2"
            # Повреждение объекта не меняет рабочую установку.
            (server_root / "objects" / third["files"][0]["sha256"]).write_text("bad")
            run("sync", False)
            assert (game / "mods/a.jar").read_text() == "new-a"
            # Подготовленный журнал имитирует обрыв процесса после замены одного файла.
            transaction = "11111111-1111-1111-1111-111111111111"
            backup = instance / "tatarin/backups" / transaction / "mods/a.jar"
            backup.parent.mkdir(parents=True)
            backup.write_text("new-a")
            write_json(instance / "tatarin/pending.json", {"transaction": transaction, "before": second, "after": third,
                       "operations": [{"path": "mods/a.jar", "existed": True, "beforeHash": sha(b"new-a"), "download": sha(b"third-a")},
                                      {"path": "mods/newer.jar", "existed": False}]})
            # Неизвестная операция должна быть отклонена до восстановления.
            (game / "mods/a.jar").write_text("half-applied")
            run("verify", False)
            assert (game / "mods/a.jar").read_text() == "half-applied"
            pending = json.loads((instance / "tatarin/pending.json").read_text())
            pending["operations"] = pending["operations"][:1]
            write_json(instance / "tatarin/pending.json", pending)
            run("verify")
            assert (game / "mods/a.jar").read_text() == "new-a"
            assert not (instance / "tatarin/pending.json").exists()
            # Пути за пределами Minecraft, ADS, reserved Windows names и коллизии регистра.
            for path in ["../escape", "mods/../../escape", "mods/a:stream", "mods/CON.jar", "saves/world.txt", "mods/x."]:
                bad = json.loads(json.dumps(second))
                bad["release"] = "bad"
                bad["files"][0]["path"] = path
                publish(bad)
                run("sync", False)
            bad = json.loads(json.dumps(second))
            bad["files"].append(dict(bad["files"][0], path="mods/A.jar"))
            publish(bad)
            run("sync", False)
            publish(second)
            # Ошибка сети не портит проверенную локальную версию.
            (server_root / "v1/catalog.json").unlink()
            run("sync", False)
            run("verify")
            print("PASS: HTTP install/update/repair, no redundant downloads, cancel, corrupt object, recovery, unsafe paths, offline integrity")
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    main(str(Path(sys.argv[1]).resolve()))
