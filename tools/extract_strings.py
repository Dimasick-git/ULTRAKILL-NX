#!/usr/bin/env python3
"""Выгрузка текстов игры для перевода.

Обходит бандлы Addressables (assets/aa/*/*.bundle) и файлы assets/bin/Data,
собирает строки из TextMeshPro (m_text), UI.Text (m_Text) и строковых полей
прочих MonoBehaviour/ScriptableObject, в которых есть буквы. Результат --
JSON: исходная строка -> {count, sources[]} (до 5 источников на строку).

    python -I tools/extract_strings.py <папка игры> -o strings_en.json

Нужен UnityPy. Файлы игры не меняются.
"""
import argparse
import json
import os
import re
import sys
from collections import defaultdict

import UnityPy

TEXT_FIELDS = ("m_text", "m_Text")
LETTER = re.compile(r"[A-Za-z]")
# строки-идентификаторы: пути, имена ассетов, ключи без пробелов в camelCase
IDENT = re.compile(r"^[A-Za-z0-9_./\\:-]+$")


def walk(value, path, out, depth=0):
    if depth > 6:
        return
    if isinstance(value, str):
        out.append((path, value))
    elif isinstance(value, dict):
        for k, v in value.items():
            walk(v, f"{path}.{k}" if path else k, out, depth + 1)
    elif isinstance(value, list):
        for i, v in enumerate(value[:400]):
            walk(v, f"{path}[]", out, depth + 1)


def wanted(field, s):
    if not s or not LETTER.search(s) or len(s) > 4000:
        return False
    last = field.rsplit(".", 1)[-1]
    if last in TEXT_FIELDS:
        return True
    # прочие поля: только похожее на текст для игрока
    if last in ("m_Name", "name", "m_Script", "guid", "m_GameObject"):
        return False
    if IDENT.match(s) and " " not in s:
        return False
    return " " in s.strip() or s.isupper()


def scan_file(path, table, stats):
    try:
        env = UnityPy.load(path)
    except Exception as e:
        stats["errors"].append(f"{path}: {e}")
        return
    base = os.path.basename(path)
    for obj in env.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        try:
            tree = obj.read_typetree()
        except Exception:
            continue
        found = []
        walk(tree, "", found)
        for field, s in found:
            if not wanted(field, s):
                continue
            entry = table[s]
            entry["count"] += 1
            if len(entry["sources"]) < 5:
                entry["sources"].append(f"{base}:{field.rsplit('.', 1)[-1]}")
        stats["objects"] += 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game", help="папка игры (с assets/)")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    files = []
    for root, _, names in os.walk(os.path.join(a.game, "assets")):
        for n in names:
            if n.endswith(".bundle") or n in ("globalgamemanagers", "resources.assets") or \
               re.match(r"^(level\d+|sharedassets\d+\.assets)$", n):
                files.append(os.path.join(root, n))
    table = defaultdict(lambda: {"count": 0, "sources": []})
    stats = {"objects": 0, "errors": []}
    for i, f in enumerate(sorted(files)):
        scan_file(f, table, stats)
        print(f"[{i + 1}/{len(files)}] {os.path.basename(f)}: строк {len(table)}", file=sys.stderr, flush=True)
    with open(a.out, "w", encoding="utf-8") as fp:
        json.dump(dict(sorted(table.items())), fp, ensure_ascii=False, indent=1)
    words = sum(len(s.split()) for s in table)
    print(f"файлов {len(files)}, объектов {stats['objects']}, строк {len(table)}, слов {words}, ошибок {len(stats['errors'])}")
    for e in stats["errors"][:10]:
        print("  ", e)


if __name__ == "__main__":
    main()
