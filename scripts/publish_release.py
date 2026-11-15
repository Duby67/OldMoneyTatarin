"""Опубликовать только тег и текст GitHub Release после merge develop → main."""

import json
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from prepare_release import prepare_release, version_tuple


class GitHub:
    def __init__(self, repository, token):
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository) or not token:
            raise ValueError("Не заданы репозиторий или GITHUB_TOKEN")
        self.base = f"https://api.github.com/repos/{repository}"
        self.token = token

    def request(self, method, path, payload=None, missing=False):
        data = json.dumps(payload).encode() if payload is not None else None
        request = Request(self.base + path, data=data, method=method, headers={
            "Authorization": f"Bearer {self.token}", "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "OldMoneyTatarin-release",
            "Content-Type": "application/json",
        })
        try:
            with urlopen(request, timeout=30) as response:
                return json.load(response)
        except HTTPError as error:
            if error.code == 404 and missing:
                return None
            # Не печатаем заголовки, токен или полное тело ответа.
            raise RuntimeError(f"GitHub API: {method} {path}, HTTP {error.code}") from None


def merged_commit(event, repository):
    pr = event.get("pull_request", {})
    head, base = pr.get("head", {}), pr.get("base", {})
    sha = pr.get("merge_commit_sha", "")
    if (event.get("action") != "closed" or pr.get("merged") is not True or
            event.get("repository", {}).get("full_name") != repository or
            head.get("ref") != "develop" or base.get("ref") != "main" or
            head.get("repo", {}).get("full_name") != repository or
            base.get("repo", {}).get("full_name") != repository or
            not re.fullmatch(r"[0-9a-f]{40}", sha)):
        raise ValueError("Выпуск разрешён только после merge PR develop → main внутри этого репозитория")
    return sha


def tag_commit(api, tag):
    ref = api.request("GET", f"/git/ref/tags/{tag}", missing=True)
    if ref is None:
        return None
    obj = ref["object"]
    for _ in range(5):
        if obj["type"] == "commit":
            return obj["sha"]
        if obj["type"] != "tag":
            break
        obj = api.request("GET", f"/git/tags/{obj['sha']}")["object"]
    raise ValueError("Не удалось разрешить существующий тег до коммита")


def publish(root, api, sha):
    tag = (root / "VERSION").read_text(encoding="utf-8").strip()
    notes = prepare_release(root, tag)
    commit = tag_commit(api, tag)
    if commit is not None and commit != sha:
        raise ValueError("Существующий тег указывает на другой коммит; перезапись запрещена")
    existing = api.request("GET", f"/releases/tags/{tag}", missing=True)
    if existing is not None:
        if (commit != sha or existing.get("tag_name") != tag or existing.get("draft") is not False or
                existing.get("prerelease") is not False or existing.get("name") != tag or
                existing.get("body", "").replace("\r\n", "\n").strip() != notes.strip()):
            raise ValueError("Существующий Release отличается; автоматическая перезапись запрещена")
        return f"Выпуск {tag} уже опубликован без изменений"
    latest = api.request("GET", "/releases/latest", missing=True)
    if latest is not None and version_tuple(latest["tag_name"]) >= version_tuple(tag):
        raise ValueError("Новый Release должен быть новее опубликованного Latest")
    if commit is None:
        api.request("POST", "/git/refs", {"ref": f"refs/tags/{tag}", "sha": sha})
    # Файлы/assets и сборка приложения здесь отсутствуют намеренно.
    api.request("POST", "/releases", {"tag_name": tag, "target_commitish": sha, "name": tag,
                                        "body": notes, "draft": False, "prerelease": False, "make_latest": "true"})
    return f"Опубликован стабильный Release {tag}"


def main():
    root = Path(__file__).resolve().parents[1]
    repository = os.environ["GITHUB_REPOSITORY"]
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
    sha = merged_commit(event, repository)
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, check=True, capture_output=True, text=True).stdout.strip()
    if head != sha:
        raise ValueError("Checkout должен указывать точно на merge_commit_sha принятого PR")
    print(publish(root, GitHub(repository, os.environ["GITHUB_TOKEN"]), sha))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Выпуск остановлен: {error}", file=sys.stderr)
        sys.exit(1)
