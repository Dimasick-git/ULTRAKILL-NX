"""Отчёт сэмплирующего профайлера (строки [prof] из debug.log, nx_prof.c).

    python prof_report.py run.log --unity-sym libunity.sym.so --il2cpp-sym libil2cpp.sym.so \
        --elf ultrakill_nx.elf [--below-fps 50] [--top 40]

Складывает окна по 10 с (при --below-fps -- только окна, после которых [perf]
показал FPS ниже порога, то есть бой и просадки), подставляет имена функций и
печатает топ по каждому потоку с долей от всех выборок этого потока.
"""
from pathlib import Path
import argparse, bisect, re, subprocess, sys
from collections import defaultdict

sys.path.insert(0, str(Path(__file__).resolve().parent))
from derive_offsets import Elf  # noqa: E402

CXXFILT = r'C:\devkitPro\devkitA64\bin\aarch64-none-elf-c++filt.exe'


class Syms:
    """Имена по адресу. Компоновщик склеивает одинаковые функции (ICF), и под
    одним адресом бывает несколько имён: показываем первое и число псевдонимов."""

    def __init__(self, path):
        s = Elf(path).symbols() if path else {}
        by_addr = defaultdict(list)
        for n, (v, z) in s.items():
            if v and z > 0:
                by_addr[v].append(n)
        self.addr = sorted(by_addr)
        self.names = [sorted(by_addr[v]) for v in self.addr]

    def name(self, off):
        i = bisect.bisect_right(self.addr, off) - 1
        if i < 0 or off - self.addr[i] >= 0x100000:
            return f'?+0x{off:x}'
        names = self.names[i]
        return names[0] + (f' [+{len(names) - 1} псевд.]' if len(names) > 1 else '')


def demangle(names):
    try:
        out = subprocess.run([CXXFILT], input='\n'.join(names), capture_output=True, text=True, timeout=60).stdout
        res = out.splitlines()
        return res if len(res) == len(names) else names
    except Exception:  # noqa: BLE001
        return names


def main():
    p = argparse.ArgumentParser()
    p.add_argument('log', type=Path)
    p.add_argument('--unity-sym')
    p.add_argument('--il2cpp-sym')
    p.add_argument('--elf')
    p.add_argument('--below-fps', type=float)
    p.add_argument('--top', type=int, default=40)
    a = p.parse_args()
    mods = {'libunity.so': Syms(a.unity_sym), 'libil2cpp.so': Syms(a.il2cpp_sym), 'NRO': Syms(a.elf)}
    rhead = re.compile(r'^\[prof\] (\S+) n=(\d+) unity=([\d.]+)% il2cpp=([\d.]+)% nro=([\d.]+)% other=([\d.]+)%')
    rrow = re.compile(r'^\[prof\] (\S+)\s+([\d.]+)% (libunity\.so|libil2cpp\.so|NRO)\+0x([0-9a-f]+)')
    rperf = re.compile(r'^\[perf\] present=([\d.]+) FPS')
    rincl = re.compile(r'^\[prof-incl\] (\S+)\s+([\d.]+)% (libunity\.so|libil2cpp\.so|NRO)\+0x([0-9a-f]+)')
    rwait = re.compile(r'^\[prof-wait\] (\S+)\s+([\d.]+)% (\S+) < (\S*) < (\S*)')
    windows, cur = [], None
    last_fps = None
    for line in a.log.read_text(encoding='utf-8', errors='replace').splitlines():
        m = rperf.match(line)
        if m:
            last_fps = float(m.group(1))
            continue
        m = rhead.match(line)
        if m:
            cur = {'thread': m.group(1), 'n': int(m.group(2)), 'split': tuple(map(float, m.group(3, 4, 5, 6))),
                   'rows': [], 'incl': [], 'wait': [], 'fps': last_fps}
            windows.append(cur)
            continue
        m = rrow.match(line)
        if m and cur and m.group(1) == cur['thread']:
            cur['rows'].append((m.group(3), int(m.group(4), 16), float(m.group(2))))
            continue
        m = rincl.match(line)
        if m and cur and m.group(1) == cur['thread']:
            cur['incl'].append((m.group(3), int(m.group(4), 16), float(m.group(2))))
            continue
        m = rwait.match(line)
        if m and cur and m.group(1) == cur['thread']:
            cur['wait'].append((tuple(x for x in m.group(3, 4, 5) if x), float(m.group(2))))
    if a.below_fps is not None:
        windows = [w for w in windows if w['fps'] is not None and w['fps'] < a.below_fps]
    def label(addr):
        m = re.match(r'(libunity\.so|libil2cpp\.so|NRO)\+0x([0-9a-f]+)$', addr)
        return mods[m.group(1)].name(int(m.group(2), 16)) if m else addr

    by_thread = defaultdict(lambda: {'n': 0, 'split': [0.0] * 4, 'funcs': defaultdict(float),
                                     'incl': defaultdict(float), 'wait': defaultdict(float)})
    for w in windows:
        t = by_thread[w['thread']]
        t['n'] += w['n']
        for i, v in enumerate(w['split']):
            t['split'][i] += v * w['n'] / 100
        for mod, off, pct in w['rows']:
            t['funcs'][(mod, mods[mod].name(off))] += pct * w['n'] / 100
        for mod, off, pct in w['incl']:
            t['incl'][(mod, mods[mod].name(off))] += pct * w['n'] / 100
        for stack, pct in w['wait']:
            t['wait'][stack] += pct * w['n'] / 100
    for thread, t in by_thread.items():
        n = max(t['n'], 1)
        s = t['split']
        print(f'\n=== {thread}: выборок {t["n"]}, окон {sum(1 for w in windows if w["thread"] == thread)}; '
              f'libunity {s[0] / n * 100:.1f}% il2cpp {s[1] / n * 100:.1f}% NRO {s[2] / n * 100:.1f}% '
              f'прочее {s[3] / n * 100:.1f}%')
        top = sorted(t['funcs'].items(), key=lambda kv: -kv[1])[:a.top]
        names = demangle([k[1] for k, _ in top])
        print('  -- собственное время (где стоит PC):')
        for ((mod, _), cnt), nm in zip(top, names):
            print(f'  {cnt / n * 100:5.1f}%  {mod:12s} {nm[:150]}')
        if t['wait']:
            print('  -- ожидания (выборки в NRO/svc): кто ждёт, три кадра движка:')
            wtop = sorted(t['wait'].items(), key=lambda kv: -kv[1])[:15]
            flat = demangle([label(x) for stack, _ in wtop for x in stack])
            pos = 0
            for stack, cnt in wtop:
                parts = flat[pos:pos + len(stack)]
                pos += len(stack)
                print(f'  {cnt / n * 100:5.1f}%  ' + '  <  '.join(p[:70] for p in parts))
        if t['incl']:
            print('  -- включительное время (функция и всё, что под ней):')
            itop = sorted(t['incl'].items(), key=lambda kv: -kv[1])[:a.top]
            inames = demangle([k[1] for k, _ in itop])
            for ((mod, _), cnt), nm in zip(itop, inames):
                print(f'  {cnt / n * 100:5.1f}%  {mod:12s} {nm[:150]}')


if __name__ == '__main__':
    main()
