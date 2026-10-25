#!/usr/bin/env python3
"""Строковые литералы C# из global-metadata.dat (IL2CPP v29), похожие на текст
для игрока: заглавные слова и фразы с пробелами, без путей и идентификаторов.

    python -I tools/extract_literals.py global-metadata.dat -o literals_en.json
"""
import argparse
import json
import re
import struct

LETTER = re.compile(r"[A-Za-z]")
IDENT = re.compile(r"^[A-Za-z0-9_./\\:#{}\[\]-]+$")
CODE = re.compile(r"(\{0\w*\}|::|=>|\(\)|\bnull\b|\.cs\b|\.dll\b|https?://|\bException\b|\bfailed\b|\bError\b)")


def literals(path):
    data = open(path, "rb").read()
    sanity, version = struct.unpack_from("<II", data, 0)
    if sanity != 0xFAB11BAF:
        raise SystemExit("не global-metadata.dat")
    lit_off, lit_size, dat_off, dat_size = struct.unpack_from("<IIII", data, 8)
    out = []
    for i in range(lit_size // 8):
        length, index = struct.unpack_from("<II", data, lit_off + i * 8)
        out.append(data[dat_off + index:dat_off + index + length].decode("utf-8", "replace"))
    return version, out


def player_text(s):
    t = s.strip()
    if len(t) < 2 or not LETTER.search(t) or len(t) > 2000:
        return False
    if IDENT.match(t):
        return t.isupper() and len(t) >= 3      # «DESTRUCTIVE», «RESTART»
    if CODE.search(t):
        return False
    words = re.findall(r"[A-Za-z']+", t)
    return len(words) >= 2 and (t.upper() == t or t[0].isupper() or t.startswith("+"))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("metadata")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    version, items = literals(a.metadata)
    picked = sorted({s for s in items if player_text(s)})
    with open(a.out, "w", encoding="utf-8") as fp:
        json.dump(picked, fp, ensure_ascii=False, indent=1)
    print(f"metadata v{version}: литералов {len(items)}, похожих на текст {len(picked)}, "
          f"слов {sum(len(s.split()) for s in picked)}")


if __name__ == "__main__":
    main()
