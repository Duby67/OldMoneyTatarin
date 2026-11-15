"""Опубликовать локальный снимок как неизменяемый выпуск TATARIN."""

import argparse
import json
import re
import shutil
from pathlib import Path, PurePosixPath

from snapshot import digest, file_policy


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(path)


def publish(snapshot, root, release, notes, memory=None, client_presets=False):
    if not re.fullmatch(r"[a-zA-Z0-9._-]+", release) or release in {".", ".."}:
        raise ValueError("Недопустимый ID выпуска")
    manifest = json.loads((snapshot / "snapshot.json").read_text(encoding="utf-8"))
    if memory is not None:
        minimum, maximum = memory
        if not 256 <= minimum <= maximum <= 262144:
            raise ValueError("Некорректный диапазон памяти Java")
        manifest["memory"] = {"minMiB": minimum, "maxMiB": maximum, "systemReserveMiB": 2048}
    pack_id = manifest["id"]
    if not re.fullmatch(r"[a-z0-9-]+", pack_id):
        raise ValueError("Недопустимый ID сборки")
    target = root / "v1/packs" / pack_id / "releases" / (release + ".json")
    if target.exists():
        raise ValueError("Выпуск уже существует; используйте новый ID")
    for entry in manifest["files"]:
        if not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]):
            raise ValueError("Недопустимый SHA-256 в снимке")
        path = PurePosixPath(entry["path"])
        if client_presets:
            entry["policy"] = file_policy(path, entry["policy"])
        if path.is_absolute() or ".." in path.parts or "\\" in entry["path"] or ":" in entry["path"]:
            raise ValueError("Недопустимый путь в снимке")
        source = snapshot / "minecraft" / path
        if digest(source) != entry["sha256"] or source.stat().st_size != entry["size"]:
            raise ValueError("Содержимое снимка изменилось")
        object_path = root / "objects" / entry["sha256"]
        object_path.parent.mkdir(parents=True, exist_ok=True)
        if not object_path.exists():
            shutil.copyfile(source, object_path)
        elif digest(object_path) != entry["sha256"]:
            raise ValueError("Повреждён объект раздачи")
    manifest.update(release=release, notes=notes)
    write_json(target, manifest)
    catalog_path = root / "v1/catalog.json"
    catalog = json.loads(catalog_path.read_text(encoding="utf-8")) if catalog_path.exists() else {"schema": 1, "packs": []}
    catalog["packs"] = [p for p in catalog["packs"] if p["id"] != pack_id]
    catalog["packs"].append({"id": pack_id, "name": manifest["name"], "release": release,
                             "manifest": f"v1/packs/{pack_id}/releases/{release}.json",
                             "sha256": digest(target), "description": "Сборка TATARIN"})
    write_json(catalog_path, catalog)
    print(f"Опубликован локальный выпуск {release}: {len(manifest['files'])} файлов")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("root", type=Path)
    parser.add_argument("--release", required=True)
    parser.add_argument("--notes", default="Первый локальный выпуск")
    parser.add_argument("--min-memory-mib", type=int)
    parser.add_argument("--max-memory-mib", type=int)
    parser.add_argument("--client-presets", action="store_true", help="Пометить известные клиентские конфиги как seed")
    args = parser.parse_args()
    if (args.min_memory_mib is None) != (args.max_memory_mib is None):
        parser.error("Задайте обе границы памяти")
    memory = (args.min_memory_mib, args.max_memory_mib) if args.min_memory_mib is not None else None
    publish(args.snapshot, args.root, args.release, args.notes, memory, args.client_presets)
