"""Создать из игровой папки отдельный снимок без миров и данных игрока."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil


POLICY = {
    "mods": "managed",
    "config": "managed",
    "defaultconfigs": "managed",
    "configureddefaults": "seed",
    "kubejs": "managed",
    "resourcepacks": "managed",
    "shaderpacks": "seed",
    "datapacks": "managed",
    "moonlight-global-datapacks": "managed",
    "patchouli_books": "managed",
}
EXCLUDED_PARTS = {
    "logs", "cache", ".cache", "local", "saves", "screenshots", "xaero",
    "e4mc", "spark", "crash_assistant", "backups", "session", "sessions",
}
SECRET = re.compile(
    r'(?im)["\']?(?:access[_-]?token|refresh[_-]?token|password|passwd|secret|api[_-]?key|authorization|cookie)["\']?\s*[:=]\s*["\']?([^\s"\',}\]]+)'
)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def excluded(path):
    return (
        any(part.lower() in EXCLUDED_PARTS for part in path.parts)
        or any(part.startswith(".") for part in path.parts)
        or path.name.lower().endswith((".bak", ".old", ".log", ".tmp", ".lock"))
        or path.name.lower() in {"options.txt", "servers.dat", "usercache.json", "usernamecache.json"}
    )


def make_snapshot(source, destination, minecraft, neoforge):
    source, destination = source.resolve(), destination.resolve()
    if destination == source or source in destination.parents or destination in source.parents:
        raise ValueError("Источник и снимок должны находиться в независимых каталогах")
    if destination.exists():
        raise ValueError("Каталог снимка уже существует; укажите новый каталог")
    if not (source / "mods").is_dir():
        raise ValueError("В источнике нет mods")
    destination.mkdir(parents=True)
    files = []
    omitted = 0
    try:
        for root, mode in POLICY.items():
            folder = source / root
            if not folder.exists():
                continue
            if folder.is_symlink() or folder.is_junction():
                omitted += 1
                continue
            for current, dirs, names in os.walk(folder, followlinks=False):
                current = Path(current)
                for directory in list(dirs):
                    candidate = current / directory
                    if candidate.is_symlink() or candidate.is_junction() or excluded(candidate.relative_to(source)):
                        dirs.remove(directory)
                        omitted += 1
                for name in sorted(names):
                    path = current / name
                    relative = path.relative_to(source)
                    if path.is_symlink() or excluded(relative):
                        omitted += 1
                        continue
                    # Не публикуем непустые поля с учётными данными. Значения не выводятся.
                    if path.suffix.lower() in {".json", ".json5", ".toml", ".cfg", ".properties", ".txt", ".yaml", ".yml"}:
                        contents = path.read_text(encoding="utf-8", errors="replace")
                        if any(m.group(1).lower() not in {"false", "true", "null", "none", "changeme"} for m in SECRET.finditer(contents)):
                            omitted += 1
                            continue
                    target = destination / "minecraft" / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(path, target)
                    files.append({"path": relative.as_posix(), "size": target.stat().st_size,
                                  "sha256": digest(target), "policy": mode})
        manifest = {"schema": 1, "id": "regionalworlds", "name": "RegionalWorlds",
                    "minecraft": minecraft, "neoforge": neoforge,
                    "files": sorted(files, key=lambda item: item["path"])}
        (destination / "snapshot.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    except Exception:
        # Не удаляем частичный снимок: пользователь может изучить ошибку и выбрать новый путь.
        raise
    print(f"Снимок: {len(files)} файлов, {sum(f['size'] for f in files)} байт; исключено внутри разрешённых папок: {omitted}")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--minecraft", required=True)
    parser.add_argument("--neoforge", required=True)
    args = parser.parse_args()
    make_snapshot(args.source, args.destination, args.minecraft, args.neoforge)
