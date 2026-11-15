"""Проверить версию релиза и извлечь его патчноуты из CHANGELOG.md."""

import argparse
from datetime import date
from pathlib import Path
import re
import sys
import subprocess


REPOSITORY = "Duby67/OldMoneyTatarin"
VERSION_PATTERN = r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"


def version_tuple(tag: str) -> tuple[int, int, int]:
    match = re.fullmatch(VERSION_PATTERN, tag)
    if not match:
        raise ValueError("VERSION должен содержать vX.Y.Z без ведущих нулей")
    return tuple(int(value) for value in match.groups())


def prepare_release(root: Path, tag: str) -> str:
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    version_tuple(version)
    if tag != version:
        raise ValueError(f"Тег {tag!r} не совпадает с VERSION {version!r}")

    changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    sections = list(re.finditer(r"^## \[([^\]]+)\](?:\(([^\n)]+)\))?([^\n]*)$", changelog, re.MULTILINE))
    matches = [i for i, section in enumerate(sections) if section[1] == version]
    if len(matches) != 1:
        raise ValueError(f"CHANGELOG.md должен содержать ровно один раздел [{version}]")
    index = matches[0]
    section = sections[index]
    if section[2] != f"https://github.com/{REPOSITORY}/releases/tag/{tag}":
        raise ValueError("Заголовок текущего выпуска должен ссылаться на его будущий GitHub tag/release")
    if any(s[1] not in {"Unreleased", "Не выпущено"} for s in sections[:index]):
        raise ValueError("Текущий выпуск должен быть первым разделом версии")
    for i, previous in enumerate(sections[:index]):
        end = sections[i + 1].start()
        if changelog[previous.end():end].strip():
            raise ValueError("Перед выпуском перенесите патчноуты из Unreleased в раздел версии")
    release_date = re.fullmatch(r" — (\d{4}-\d{2}-\d{2})", section[3])
    if not release_date:
        raise ValueError("Заголовок релиза должен иметь вид ## [vX.Y.Z](ссылка-на-тег) — YYYY-MM-DD")
    date.fromisoformat(release_date[1])
    end = sections[index + 1].start() if index + 1 < len(sections) else len(changelog)
    notes = changelog[section.end():end].strip()
    if not any(line.startswith("- ") for line in notes.splitlines()):
        raise ValueError("В разделе релиза должен быть хотя бы один пункт патчноутов")
    return notes + "\n"


def validate_base(root: Path, tag: str, base_ref: str) -> None:
    if not re.fullmatch(r"[0-9a-f]{40}", base_ref):
        raise ValueError("Базовый коммит должен быть полным SHA")
    # Отсутствие VERSION допустимо только при первоначальном заполнении main.
    subprocess.run(["git", "cat-file", "-e", f"{base_ref}^{{commit}}"], cwd=root, check=True, capture_output=True)
    has_version = subprocess.run(["git", "cat-file", "-e", f"{base_ref}:VERSION"], cwd=root, capture_output=True)
    if has_version.returncode:
        return
    previous = subprocess.run(["git", "show", f"{base_ref}:VERSION"], cwd=root, check=True,
                              capture_output=True, text=True).stdout.strip()
    if version_tuple(tag) <= version_tuple(previous):
        raise ValueError(f"Перед PR в main повысьте VERSION: {tag} не новее {previous}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--notes-file", type=Path)
    parser.add_argument("--base-ref", help="SHA main до PR; новая версия должна быть больше")
    args = parser.parse_args()
    try:
        notes = prepare_release(Path(__file__).resolve().parents[1], args.tag)
        if args.base_ref:
            validate_base(Path(__file__).resolve().parents[1], args.tag, args.base_ref)
        if args.notes_file:
            args.notes_file.write_text(notes, encoding="utf-8")
        else:
            print(notes, end="")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Ошибка подготовки релиза: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
