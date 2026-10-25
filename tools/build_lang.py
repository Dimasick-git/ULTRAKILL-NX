#!/usr/bin/env python3
"""Таблицы языков для NxLoc: loc/<код>.json (ключ -> перевод) -> lang/<код>.tsv.

Строка .tsv: «ключ<TAB>перевод», \\n \\r \\t \\\\ экранированы. Для строк с
разметкой и переносами добавляются пары кусков (между тегами и переводами
строк), если у оригинала и перевода их поровну: NxLoc переводит по ним
строки, которые код игры собирает из частей.

    python tools/build_lang.py --source source.json --loc loc --out lang
"""
import argparse
import importlib.util
import json
import os
import re

spec = importlib.util.spec_from_file_location("bls", os.path.join(os.path.dirname(__file__), "build_loc_source.py"))
bls = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bls)

SPLIT = re.compile(r"(<[^>]*>|\r\n|\n|\r)")


def pieces(s):
    out = []
    for part in SPLIT.split(s):
        if not part or SPLIT.fullmatch(part):
            continue
        core = part.strip()
        if core:
            out.append(core)
    return out


def escape(s):
    return s.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", required=True)
    ap.add_argument("--loc", required=True, help="папка с <код>.json")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    source = {e["id"]: e["en"] for e in json.load(open(a.source, encoding="utf-8"))}
    os.makedirs(a.out, exist_ok=True)
    for name in sorted(os.listdir(a.loc)):
        if not name.endswith(".json"):
            continue
        code = name[:-5]
        tr = json.load(open(os.path.join(a.loc, name), encoding="utf-8"))
        table, derived = {}, 0
        for k, t in tr.items():
            # A row equal to the original only costs memory: no row means
            # NxLoc keeps the English text anyway.
            if k not in source or not t or t == source[k]:
                continue
            table[k] = t
            en = source[k]
            pe, pt = pieces(en), pieces(t)
            if len(pe) > 1 and len(pe) == len(pt):
                for x, y in zip(pe, pt):
                    hk = bls.fnv(x)
                    if hk not in source and hk not in table and re.search(r"[A-Za-z]{2}", x):
                        table[hk] = y
                        derived += 1
        # Settings builders uppercase their serialized labels at runtime. A
        # title-case source hash alone cannot match e.g. INVERT X AXIS.
        aliases = {}
        ambiguous = set()
        for k, t in tr.items():
            en = source.get(k, '')
            if not en or len(en) > 160 or '\n' in en or '\r' in en or t == en:
                continue
            upper = ''.join(part if i % 2 else part.upper()
                            for i, part in enumerate(re.split(r'(<[^>]+>|\{[^}\s]*\})', t)))
            for original, translated in ((en.upper(), upper), (en.strip(), t.strip())):
                hk = bls.fnv(original)
                if hk in source or hk in table:
                    continue
                if hk in aliases and aliases[hk] != translated:
                    ambiguous.add(hk)
                aliases[hk] = translated
        table.update({k:v for k,v in aliases.items() if k not in ambiguous})
        with open(os.path.join(a.out, code + ".tsv"), "w", encoding="utf-8", newline="\n") as fp:
            fp.write(f"# ULTRAKILL NX: {code}, строк {len(table)}\n")
            for k in sorted(table):
                fp.write(f"{k}\t{escape(table[k])}\n")
        print(f"{code}: перевод {len(tr)}/{len(source)} ({100 * len(tr) // max(1, len(source))}%), "
              f"кусков {derived}, всего {len(table)}")


if __name__ == "__main__":
    main()
