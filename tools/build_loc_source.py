#!/usr/bin/env python3
"""Список строк для перевода: тексты из данных игры, литералы её кода и
надписи порта. Ключ строки -- FNV-1a 64 по символам UTF-16 (как NxLoc.Hash).

    python tools/build_loc_source.py --strings strings_en.json --literals literals_en.json -o source.json

source.json содержит английский текст игры и в репозиторий не кладётся:
переводы (loc/<код>.json) хранятся по ключу, без оригинала.
"""
import argparse
import json
import re

# поля данных, которые игрок видит на экране
FIELDS = {
    "m_text", "m_Text", "subtitle", "message", "message2", "text", "text1", "text2", "label", "content",
    "description", "strategy", "levelName", "layerName", "LargeText", "tip", "objectName", "weaponName",
    "dropdownList[]", "taunts[]", "tauntsSecondPhase[]", "bossName", "overrideFullName", "type", "deathType",
    "strings[]", "fishName", "fullName", "painterName", "m_GenericText", "m_DualShockText", "title",
    "textBeforeName", "nameWhenDisabled", "preText", "ifMax", "ifMin", "specialScenes[]",
    "phaseChangeSubtitle", "styleName", "sideNote", "weaknesses[]", "m_Action",
}
# литералы кода, похожие на отладку, а не на текст для игрока
DEBUG = re.compile(r"Failed|not found|[Ii]nstance|Cannot|Exception|Warning|SetFloat|Applying|Loading stats|"
                   r"Length of|No duration|missing key|Unfortunately|Collider|Echo the given|parse|"
                   r"<Keyboard>|<Gamepad>|<Mouse>|\*\.|Portal |Prepass|occlusion|radiance tier|Slot\{0\}")
TAG = re.compile(r"<[^>]*>")

# надписи панели SWITCH и прочие строки порта (NxPortSettings.cs и др.)
PORT = [
    "ULTRAKILL - NX / SWITCH", "Render height", "Upscaler", "OFF", "ON", "FSR 1", "FSR 3.1 TEST",
    "Frame generation (TEST)", "Frame generation: unavailable", "Frame generation:", "too slow / fallback",
    "GPU fault / fallback", "presenting", "capture ready", "idle", "RCAS attenuation (0 = strongest)",
    "Gyroscope", "Gyro sensitivity", "Invert gyro X", "Invert gyro Y", "CPU loading boost",
    "SaltyNX installed: reboot Atmosphere to enable FPS overlay", "Saved to SD card", "Saving...",
    "Could not save: check free space / SD card", "BACK / B", "Enemy shader preload:", "Auto:", "SWITCH", "Language",
    "Frame Time", "Frame time", "Frametime", "FrameTime", "Render",
]

for _button in ("A", "B", "X", "Y", "ZL", "ZR", "L", "R", "+", "-", "LS", "RS"):
    for _template in ("Press {b} to close", "PRESS {b} TO CLOSE", "Press {b} to CLOSE",
                      'PRESS "<color=orange>{b}</color>" to CLOSE', "Press <color=orange>{b}</color> to close"):
        PORT.append(_template.format(b=_button))
PORT.append("[YOU ARE DEAD]" + "\n" * 6 + "Press ZR TO RESTART")


def fnv(s):
    h = 14695981039346656037
    b = s.encode("utf-16-le")
    for i in range(0, len(b), 2):
        h ^= b[i] | (b[i + 1] << 8)
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016x}"


def has_words(s):
    return re.search(r"[A-Za-z]{2}", TAG.sub("", s)) is not None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--strings", required=True)
    ap.add_argument("--literals", required=True)
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    entries = {}

    def add(s, ctx):
        if not has_words(s):
            return
        k = fnv(s)
        e = entries.setdefault(k, {"id": k, "en": s, "ctx": ctx})
        if e["en"] != s:
            raise SystemExit(f"коллизия хеша {k}")

    for s, info in json.load(open(a.strings, encoding="utf-8")).items():
        src = info["sources"][0]
        if src.rsplit(":", 1)[-1] in FIELDS:
            add(s, src)
    for s in json.load(open(a.literals, encoding="utf-8")):
        if not DEBUG.search(s) and not s.startswith("#"):
            add(s, "code")
    for s in PORT:
        add(s, "port")
    out = sorted(entries.values(), key=lambda e: (e["ctx"] != "port", e["ctx"], e["en"]))
    json.dump(out, open(a.out, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    words = sum(len(TAG.sub(" ", e["en"]).split()) for e in out)
    print(f"строк {len(out)}, слов {words}")


if __name__ == "__main__":
    main()
