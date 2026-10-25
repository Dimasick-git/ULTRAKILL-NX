#!/usr/bin/env python3
"""Порции для перевода и проверка готовых порций.

    python tools/loc_chunks.py split --source source.json --out chunks [--size 5500]
    python tools/loc_chunks.py check --source source.json --chunks chunks --loc loc/ru
    python tools/loc_chunks.py merge --loc loc/ru -o ru.json

Порция -- строки «ключ<TAB>текст», переносы и обратная косая черта экранированы.
Переведённая порция -- <номер>.json: {ключ: перевод}. check сверяет ключи, теги
разметки, подстановки {…} и служебные символы ▼ } у оригинала и перевода.
"""
import argparse
import json
import os
import re
from collections import Counter

TAGS = re.compile(r"<[^>]+>")
SUBST = re.compile(r"\{[^}\s]*\}")


def esc(s):
    return s.replace("\\", "\\\\").replace("\r", "\\r").replace("\n", "\\n")


def split(a):
    d = json.load(open(a.source, encoding="utf-8"))
    os.makedirs(a.out, exist_ok=True)
    chunks, cur, size = [], [], 0
    for e in d:
        n = len(e["en"])
        if cur and size + n > a.size:
            chunks.append(cur)
            cur, size = [], 0
        cur.append(e)
        size += n
    if cur:
        chunks.append(cur)
    for i, c in enumerate(chunks):
        with open(os.path.join(a.out, f"{i:03d}.txt"), "w", encoding="utf-8", newline="\n") as fp:
            for e in c:
                fp.write(e["id"] + "\t" + esc(e["en"]) + "\n")
    print(len(chunks), "порций")


def tags(s):
    return Counter(t.lower().replace(" ", "") for t in TAGS.findall(s))


def check(a):
    source = {e["id"]: e["en"] for e in json.load(open(a.source, encoding="utf-8"))}
    # A string missing from a portion stays in English: debug and log text is
    # left out on purpose, the game shows the original when no row matches.
    total, problems, english = 0, 0, 0
    for name in sorted(os.listdir(a.chunks)):
        if not name.endswith(".txt"):
            continue
        ids = [l.split("\t", 1)[0] for l in open(os.path.join(a.chunks, name), encoding="utf-8") if "\t" in l]
        path = os.path.join(a.loc, name[:-4] + ".json")
        expected = None
        if a.reference:
            expected = set(json.load(open(os.path.join(a.reference, name[:-4] + '.json'), encoding='utf-8')))
        if not os.path.exists(path):
            english += len(expected if expected is not None else ids)
            if expected:
                problems += 1
                print(f'{name}: отсутствует перевод порции ({len(expected)} строк)')
            continue
        tr = json.load(open(path, encoding="utf-8"))
        if expected is not None and set(tr) != expected:
            problems += 1
            print(f'{name}: пропущено {len(expected - set(tr))}, лишних {len(set(tr) - expected)}')
        english += sum(1 for k in ids if k not in tr)
        extra = [k for k in tr if k not in ids]
        if extra:
            problems += 1
            print(f"{name}: чужих строк {len(extra)} {extra[:3]}")
        for k, t in tr.items():
            if k not in source:
                continue
            total += 1
            en = source[k]
            notes = []
            if tags(en) != tags(t):
                notes.append("теги")
            if Counter(SUBST.findall(en)) != Counter(SUBST.findall(t)):
                notes.append("подстановки")
            # ▼ and } are markers the game splits on, $ breaks tutorial lines;
            # in the typewriter boot text (it has ½) # @ & ± ~ are codes too.
            for ch in "▼}$½" + ("§Ä#@&±~_" if "½" in en else ""):
                if en.count(ch) != t.count(ch):
                    notes.append(f"'{ch}' {en.count(ch)}/{t.count(ch)}")
            if notes:
                problems += 1
                print(f"{name} {k}: {', '.join(notes)}")
    print(f"проверено строк {total}, на английском {english}, замечаний {problems}")
    return 1 if problems else 0


def merge(a):
    out = {}
    for name in sorted(os.listdir(a.loc)):
        if re.fullmatch(r'\d{3}\.json', name):
            portion = json.load(open(os.path.join(a.loc, name), encoding="utf-8"))
            for key, value in portion.items():
                if not re.fullmatch(r'[0-9a-f]{16}', key) or not isinstance(value, str):
                    raise ValueError(f'{name}: invalid translation row {key!r}')
                if key in out:
                    raise ValueError(f'{name}: duplicate translation ID {key}')
                out[key] = value
    if not out:
        raise ValueError('No numbered translation portions found')
    json.dump(dict(sorted(out.items())), open(a.out, "w", encoding="utf-8"), ensure_ascii=False, indent=0)
    print(f"{a.out}: строк {len(out)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("split"); p.add_argument("--source", required=True); p.add_argument("--out", required=True)
    p.add_argument("--size", type=int, default=5500)
    p = sp.add_parser("check"); p.add_argument("--source", required=True); p.add_argument("--chunks", required=True)
    p.add_argument("--loc", required=True)
    p.add_argument("--reference", help="порции с обязательными ID (например loc/ru)")
    p = sp.add_parser("merge"); p.add_argument("--loc", required=True); p.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    return {"split": split, "check": check, "merge": merge}[a.cmd](a)


if __name__ == "__main__":
    raise SystemExit(main())
