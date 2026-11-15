"""Проверки подготовки и публикации релизов без обращений к GitHub."""

import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from prepare_release import prepare_release, validate_base
from publish_release import merged_commit, publish


SHA = "a" * 40
OTHER_SHA = "b" * 40
REPO = "Duby67/OldMoneyTatarin"
TAG = "v0.3.0"
NOTES = "### Исправлено\n\n- Проверка обновлений.\n"


class FakeGitHub:
    def __init__(self):
        self.commit = None
        self.release = None
        self.latest = None
        self.writes = []
        self.fail_release = False

    def request(self, method, path, payload=None, missing=False):
        if method == "GET":
            if path == f"/git/ref/tags/{TAG}":
                return {"object": {"type": "commit", "sha": self.commit}} if self.commit else None
            if path == f"/releases/tags/{TAG}":
                return self.release
            if path == "/releases/latest":
                return self.latest
        self.writes.append((method, path, payload))
        if method == "POST" and path == "/git/refs":
            self.commit = payload["sha"]
            return {}
        if method == "POST" and path == "/releases":
            if self.fail_release:
                raise RuntimeError("Simulated API failure")
            self.release = dict(payload)
            self.latest = dict(payload)
            return self.release
        raise AssertionError((method, path))


class ReleaseTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "VERSION").write_text(TAG + "\n", encoding="utf-8")
        self.header = f"## [{TAG}](https://github.com/{REPO}/releases/tag/{TAG}) — 2026-11-15\n\n"
        self.changelog(self.header + NOTES)

    def changelog(self, content):
        (self.root / "CHANGELOG.md").write_text("# Изменения\n\n" + content, encoding="utf-8")

    def test_extract_only_current_notes(self):
        self.changelog(self.header + NOTES + "\n## [v0.2.0] — 2026-11-01\n\n- Старые изменения.\n")
        self.assertEqual(prepare_release(self.root, TAG), NOTES)

    def test_invalid_headers_and_notes(self):
        for content in [
            self.header.replace("2026-11-15", "2026-02-30") + NOTES,
            self.header.replace(f"/tag/{TAG}", "/tag/v0.2.0") + NOTES,
            f"## [{TAG}] — 2026-11-15\n\n" + NOTES,
            self.header + NOTES + "\n" + self.header + NOTES,
            self.header + "Нет пунктов патчноутов.",
            "## [Unreleased]\n\n- Не перенесено.\n\n" + self.header + NOTES,
            "## [Unreleased]\n\n* Не перенесено.\n\n" + self.header + NOTES,
            "## [v0.4.0] — 2026-11-16\n\n- Новее.\n\n" + self.header + NOTES,
        ]:
            with self.subTest(content=content), self.assertRaises(ValueError):
                self.changelog(content)
                prepare_release(self.root, TAG)

    def test_tag_must_match_version(self):
        with self.assertRaises(ValueError):
            prepare_release(self.root, "v0.4.0")

    def test_publish_exact_commit_and_notes(self):
        api = FakeGitHub()
        publish(self.root, api, SHA)
        self.assertEqual(api.writes[0], ("POST", "/git/refs", {"ref": f"refs/tags/{TAG}", "sha": SHA}))
        metadata = api.writes[1][2]
        self.assertEqual(metadata["body"], NOTES)
        self.assertEqual(metadata["target_commitish"], SHA)
        self.assertFalse(metadata["draft"])
        self.assertFalse(metadata["prerelease"])
        self.assertEqual(metadata["make_latest"], "true")
        self.assertNotIn("assets", metadata)

    def test_rerun_does_not_rewrite_release(self):
        api = FakeGitHub()
        publish(self.root, api, SHA)
        api.writes.clear()
        publish(self.root, api, SHA)
        self.assertEqual(api.writes, [])

    def test_different_tag_commit_is_never_overwritten(self):
        api = FakeGitHub()
        api.commit = OTHER_SHA
        with self.assertRaises(ValueError):
            publish(self.root, api, SHA)
        self.assertEqual(api.writes, [])

    def test_different_existing_release_is_never_overwritten(self):
        api = FakeGitHub()
        publish(self.root, api, SHA)
        api.writes.clear()
        api.release["body"] = "Manual notes"
        with self.assertRaises(ValueError):
            publish(self.root, api, SHA)
        self.assertEqual(api.writes, [])

    def test_downgrade_does_not_create_tag(self):
        for latest in [TAG, "v0.10.0"]:
            api = FakeGitHub()
            api.latest = {"tag_name": latest}
            with self.assertRaises(ValueError):
                publish(self.root, api, SHA)
            self.assertEqual(api.writes, [])

    def test_retry_after_tag_created(self):
        api = FakeGitHub()
        api.fail_release = True
        with self.assertRaises(RuntimeError):
            publish(self.root, api, SHA)
        api.fail_release = False
        api.writes.clear()
        publish(self.root, api, SHA)
        self.assertEqual([write[1] for write in api.writes], ["/releases"])

    def test_only_merged_internal_develop_to_main(self):
        event = {"action": "closed", "repository": {"full_name": REPO}, "pull_request": {
            "merged": True, "merge_commit_sha": SHA, "head": {"ref": "develop", "repo": {"full_name": REPO}},
            "base": {"ref": "main", "repo": {"full_name": REPO}}}}
        self.assertEqual(merged_commit(event, REPO), SHA)
        invalid = []
        for value in [False, "true"]:
            candidate = copy.deepcopy(event)
            candidate["pull_request"]["merged"] = value
            invalid.append(candidate)
        for field, value in [("head", "feature"), ("base", "develop")]:
            candidate = copy.deepcopy(event)
            candidate["pull_request"][field]["ref"] = value
            invalid.append(candidate)
        candidate = copy.deepcopy(event)
        candidate["pull_request"]["head"]["repo"]["full_name"] = "fork/OldMoneyTatarin"
        invalid.append(candidate)
        candidate = copy.deepcopy(event)
        candidate["action"] = "opened"
        invalid.append(candidate)
        for candidate in invalid:
            with self.subTest(candidate=candidate), self.assertRaises(ValueError):
                merged_commit(candidate, REPO)

    def test_version_must_increase_against_main(self):
        def git(*args):
            return subprocess.run(["git", *args], cwd=self.root, check=True, capture_output=True, text=True).stdout.strip()
        git("init", "-b", "main")
        git("add", ".")
        git("-c", "user.name=ReleaseTest", "-c", "user.email=test@example.invalid", "commit", "-m", "fixture")
        base = git("rev-parse", "HEAD")
        with self.assertRaises(ValueError):
            validate_base(self.root, TAG, base)
        validate_base(self.root, "v0.10.0", base)
        with self.assertRaises(ValueError):
            validate_base(self.root, "v0.2.0", base)


if __name__ == "__main__":
    unittest.main()
