"""Проверки состава снимка и неизменяемости локальных выпусков."""

import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/tatarin"))
from snapshot import make_snapshot
from publish import publish


class ToolsTest(unittest.TestCase):
    def test_snapshot_and_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            for path, content in {
                "mods/a.jar": "mod", "config/common.toml": "enabled=true",
                "config/common.toml.bak": "backup", "config/e4mc/session.json": "private",
                "config/private.toml": 'access_token="test-private-value"',
                "saves/world.txt": "world", "options.txt": "user", "logs/latest.log": "log",
                "configureddefaults/config/client.toml": "initial",
            }.items():
                file = source / path
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text(content)
            snapshot = root / "snapshot"
            value = make_snapshot(source, snapshot, "1.21.1", "21.1.264")
            self.assertEqual({f["path"] for f in value["files"]}, {
                "mods/a.jar", "config/common.toml", "configureddefaults/config/client.toml"})
            self.assertEqual((source / "saves/world.txt").read_text(), "world")
            self.assertNotIn("test-private-value", (snapshot / "snapshot.json").read_text())
            server = root / "server"
            publish(snapshot, server, "test-1", "test")
            old = (server / "v1/catalog.json").read_bytes()
            with self.assertRaises(ValueError):
                publish(snapshot, server, "test-1", "changed")
            self.assertEqual(old, (server / "v1/catalog.json").read_bytes())
            (snapshot / "minecraft/mods/a.jar").write_text("changed")
            with self.assertRaises(ValueError):
                publish(snapshot, server, "test-2", "test")
            self.assertEqual(old, (server / "v1/catalog.json").read_bytes())
            with self.assertRaises(ValueError):
                make_snapshot(source, source / "nested", "1.21.1", "21.1.264")


if __name__ == "__main__":
    unittest.main()
