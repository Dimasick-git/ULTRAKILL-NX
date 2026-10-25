#!/usr/bin/env python3
"""frame_report.py <debug.log> [--below-fps 50] -- сводка строк [frame] (nx_frame.c).

Берётся последний сеанс журнала. Окна с загрузками отбрасываются (кадр > 400 мс
или нет шагов физики). Понимает формат r63 (с приоритетом потока рендера, его
окна делятся по приоритету) и r64 (ожидание главного потока по фазам, доля
кадров, показанных без ожидания развёртки).
"""
import argparse
import re
import statistics as st
from collections import defaultdict

NUM = r"(-?[\d.]+)"
RX_HEAD = re.compile(r"^\[frame\] ([\d.]+) с, (\d+) кадров, ([\d.]+) FPS, кадр ([\d.]+) мс \| загрузчик ([\d.]+) \| "
                     r"движок ([\d.]+) \(фазы ([\d.]+)\) \| после ([-\d.]+) \| физ\. шагов ([\d.]+)"
                     r"(?: \| рендер-поток (0x[0-9a-f]+))?(?: \| ожидание ([\d.]+))?")
RX_PHASE = re.compile(r"^\[frame-phase\] (.*?)(?: \(всего/из них ожидание\))?$")
RX_ENTRY = re.compile(r"^\[frame\]\s+([\d.]+) мс x\s*([\d.]+) max\s+([\d.]+)(?: ожид\s+([\d.]+))?\s+(\S+)")
RX_HOOK = re.compile(r"^\[frame-r\]\s+([\d.]+) мс x\s*([\d.]+) ожид\s+([\d.]+)\s+(.+)$")
RX_CAMI = re.compile(r"^\[frame-cami\] (.+?): (.*)$")
RX_CAM = re.compile(r"^\[frame-cam\]\s+([\d.]+) мс x\s*([\d.]+) отсечение\s+([\d.]+) цикл\s+([\d.]+)\s+(.+)$")
RX_WAIT = re.compile(r"^\[frame-wait\] ожидание GPU ([\d.]+) мс/кадр \(([\d.]+) выз\.\), dequeue ([\d.]+) мс/кадр, "
                     r"паузы GC (\d+) шт ([\d.]+) мс \(max ([\d.]+)\)"
                     r"(?: \| показ без развёртки ([\d.]+)%, переключений (\d+))?")


RX_SCENE = re.compile(r"StandaloneWindows64/([a-z0-9_ -]*scenes_)([^ ,/]+)\.bundle,0x0")


def windows(lines):
    start = 0
    for i, l in enumerate(lines):
        if "=== ultrakill_nx" in l:
            start = i
    cur = None
    scene = "?"
    for l in lines[start:]:
        ms = RX_SCENE.search(l)
        if ms:
            scene = ms.group(2) if not ms.group(1).startswith("specialscenes") else "menu:" + ms.group(2)
        m = RX_HEAD.match(l)
        if m:
            if cur:
                yield cur
            v = m.groups()
            cur = dict(scene=scene, fps=float(v[2]), frame=float(v[3]), pre=float(v[4]), engine=float(v[5]), phases=float(v[6]),
                       post=float(v[7]), steps=float(v[8]), prio=v[9] or "-", wait_total=float(v[10] or 0),
                       groups={}, gwait={}, entries={}, ewait={}, hooks={}, cams={}, cami={}, wait=None)
            continue
        if not cur:
            continue
        m = RX_PHASE.match(l)
        if m:
            for part in m.group(1).split(" | "):
                name, value = part.rsplit(" ", 1)
                total, _, waited = value.partition("/")
                cur["groups"][name] = float(total)
                cur["gwait"][name] = float(waited or 0)
            continue
        m = RX_ENTRY.match(l)
        if m:
            cur["entries"][m.group(5)] = float(m.group(1))
            cur["ewait"][m.group(5)] = float(m.group(4) or 0)
            continue
        m = RX_HOOK.match(l)
        if m:
            cur["hooks"][m.group(4).strip()] = (float(m.group(1)), float(m.group(2)), float(m.group(3)))
            continue
        m = RX_CAMI.match(l)
        if m:
            parts = cur["cami"].setdefault(m.group(1).strip(), defaultdict(float))
            for part in m.group(2).split(" | "):
                if part.strip() in ("", "-"):
                    continue
                name, value = part.rsplit(" ", 1)
                parts[name.strip()] += float(value)
            continue
        m = RX_CAM.match(l)
        if m:
            name = m.group(5).strip()
            old = cur["cams"].get(name, (0.0, 0.0, 0.0, 0.0))
            cur["cams"][name] = tuple(o + float(x) for o, x in zip(old, m.group(1, 2, 3, 4)))
            continue
        m = RX_WAIT.match(l)
        if m:
            cur["wait"] = tuple(float(x) if x is not None else 0.0 for x in m.groups())
    if cur:
        yield cur


def summary(title, ws):
    if not ws:
        print(f"{title}: окон нет")
        return
    fps = [w["fps"] for w in ws]
    print(f"{title}: окон {len(ws)}, FPS медиана {st.median(fps):.1f} (мин {min(fps):.1f}), кадр {st.mean(w['frame'] for w in ws):.2f} мс")
    print(f"   загрузчик {st.mean(w['pre'] for w in ws):.2f} | движок {st.mean(w['engine'] for w in ws):.2f} "
          f"(фазы {st.mean(w['phases'] for w in ws):.2f}) | после {st.mean(w['post'] for w in ws):.2f} | "
          f"физ. шагов {st.mean(w['steps'] for w in ws):.2f} | ожидание главного потока {st.mean(w['wait_total'] for w in ws):.2f}")
    groups, gwait = defaultdict(list), defaultdict(list)
    for w in ws:
        for k, v in w["groups"].items():
            groups[k].append(v)
            gwait[k].append(w["gwait"].get(k, 0.0))
    print("   группы (всего/ожидание): " + " | ".join(f"{k} {st.mean(v):.2f}/{st.mean(gwait[k]):.2f}" for k, v in groups.items()))
    entries, ewait = defaultdict(float), defaultdict(float)
    for w in ws:
        for k, v in w["entries"].items():
            entries[k] += v
            ewait[k] += w["ewait"].get(k, 0.0)
    for k, v in sorted(entries.items(), key=lambda kv: -kv[1])[:14]:
        print(f"   {v / len(ws):6.2f} мс (ожид {ewait[k] / len(ws):5.2f})  {k}")
    hooks = defaultdict(lambda: [0.0, 0.0, 0.0])
    for w in ws:
        for k, (t, c, wt) in w["hooks"].items():
            h = hooks[k]
            h[0] += t; h[1] += c; h[2] += wt
    if hooks:
        print("   внутри отрисовки (включительно, мс/кадр):")
        for k, (t, c, wt) in sorted(hooks.items(), key=lambda kv: -kv[1][0]):
            print(f"   {t / len(ws):6.2f} мс x{c / len(ws):4.2f} (ожид {wt / len(ws):5.2f})  {k}")
    cams = defaultdict(lambda: [0.0, 0.0, 0.0, 0.0])
    for w in ws:
        for k, v in w["cams"].items():
            c = cams[k]
            for i in range(4):
                c[i] += v[i]
    if cams:
        print("   камеры (мс/кадр: всего, вызовов, отсечение, цикл рендера):")
        for k, (t, n, cull, loop) in sorted(cams.items(), key=lambda kv: -kv[1][0]):
            print(f"   {t / len(ws):6.2f} мс x{n / len(ws):4.2f} отсеч {cull / len(ws):5.2f} цикл {loop / len(ws):5.2f}  {k}")
            inner = defaultdict(float)
            for w in ws:
                for fn, v in w["cami"].get(k, {}).items():
                    inner[fn] += v
            if inner:
                print("          " + " | ".join(f"{fn} {v / len(ws):.2f}" for fn, v in
                                               sorted(inner.items(), key=lambda kv: -kv[1])))
    waits = [w["wait"] for w in ws if w["wait"]]
    if waits:
        print(f"   ожидание GPU {st.mean(x[0] for x in waits):.2f} мс/кадр, dequeue {st.mean(x[2] for x in waits):.2f} мс/кадр, "
              f"паузы GC {sum(x[3] for x in waits):.0f} шт, {sum(x[4] for x in waits):.0f} мс (max {max(x[5] for x in waits):.0f}), "
              f"показ без развёртки {st.mean(x[6] for x in waits):.0f}%")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("log")
    p.add_argument("--below-fps", type=float, default=0.0, help="только окна ниже этого FPS (бои)")
    p.add_argument("--menus", action="store_true", help="считать и окна меню/заставок (по умолчанию только уровни)")
    a = p.parse_args()
    lines = open(a.log, encoding="utf-8", errors="replace").read().splitlines()
    # Пауза: timeScale 0 -> шагов физики нет или почти нет. Игра от ~0.5 шага на кадр
    # (125 Гц физики при 120 FPS -- около 1 шага).
    ws = [w for w in windows(lines) if w["frame"] < 400 and w["steps"] >= 0.5
          and (a.menus or not (w["scene"].startswith("menu:") or w["scene"] == "?"))]
    if ws:
        print("уровни:", ", ".join(sorted({w['scene'] for w in ws})))
    if a.below_fps:
        ws = [w for w in ws if w["fps"] < a.below_fps]
    summary("все окна", ws)
    prios = sorted({w["prio"] for w in ws} - {"-"})
    for prio in prios if len(prios) > 1 else []:
        print()
        summary(f"рендер-поток {prio}", [w for w in ws if w["prio"] == prio])


if __name__ == "__main__":
    main()
