"""Проверить версию релиза и извлечь его патчноуты из CHANGELOG.md."""

import argparse
from datetime import date
from pathlib import Path
import re
import sys


def prepare_release(root: Path, tag: str) -> str:
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise ValueError("VERSION должен содержать vX.Y.Z без ведущих нулей")
    if tag != version:
        raise ValueError(f"Тег {tag!r} не совпадает с VERSION {version!r}")

    changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    sections = list(re.finditer(r"^## \[([^\]]+)\]([^\n]*)$", changelog, re.MULTILINE))
    matches = [i for i, section in enumerate(sections) if section[1] == version]
    if len(matches) != 1:
        raise ValueError(f"CHANGELOG.md должен содержать ровно один раздел [{version}]")
    index = matches[0]
    section = sections[index]
    release_date = re.fullmatch(r" — (\d{4}-\d{2}-\d{2})", section[2])
    if not release_date:
        raise ValueError("Заголовок релиза должен иметь вид ## [vX.Y.Z] — YYYY-MM-DD")
    date.fromisoformat(release_date[1])
    end = sections[index + 1].start() if index + 1 < len(sections) else len(changelog)
    notes = changelog[section.end():end].strip()
    if not any(line.startswith("- ") for line in notes.splitlines()):
        raise ValueError("В разделе релиза должен быть хотя бы один пункт патчноутов")
    return notes + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--notes-file", type=Path)
    args = parser.parse_args()
    try:
        notes = prepare_release(Path(__file__).resolve().parents[1], args.tag)
        if args.notes_file:
            args.notes_file.write_text(notes, encoding="utf-8")
        else:
            print(notes, end="")
    except (OSError, ValueError) as error:
        print(f"Ошибка подготовки релиза: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
