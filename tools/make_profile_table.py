"""Таблица начал функций для сэмплирующего профайлера загрузчика (nx_prof.c).

Профайлер на консоли знает только адрес инструкции. Чтобы считать по функциям,
он ищет начало функции в этой таблице (двоичный поиск) и в журнал пишет
«модуль+начало функции»; имена подставляет tools/prof_report.py по тем же
.sym.so. Build-id библиотеки в игре и .sym.so обязаны совпадать.

Формат (little-endian): "NXPF", u32 версия=1, u32 число модулей; для каждого
модуля: char[32] имя (с нулями), u32 число функций, u32[] начала функций по
возрастанию (смещения от базы модуля).

    python make_profile_table.py --game release/r55-game \
        --sym libunity.so=<libunity.sym.so> --sym libil2cpp.so=<libil2cpp.sym.so> \
        -o nx_prof.bin
"""
from pathlib import Path
import argparse, struct, sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from derive_offsets import Elf  # noqa: E402


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--game', required=True, type=Path)
    p.add_argument('--sym', action='append', required=True, help='имя.so=путь к .sym.so')
    p.add_argument('-o', '--out', required=True, type=Path)
    a = p.parse_args()
    out = [b'NXPF', struct.pack('<II', 1, len(a.sym))]
    for item in a.sym:
        name, path = item.split('=', 1)
        lib, sym = Elf(a.game / name), Elf(Path(path))
        assert lib.build_id() and lib.build_id() == sym.build_id(), f'build-id {name} и {path} не совпадают'
        starts = sorted({v for _, (v, z) in sym.symbols().items() if v and z > 0})
        enc = name.encode()
        assert len(enc) < 32
        out.append(enc.ljust(32, b'\0'))
        out.append(struct.pack('<I', len(starts)))
        out.append(struct.pack(f'<{len(starts)}I', *starts))
        print(f'{name}: {len(starts)} функций, build-id {lib.build_id()}')
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_bytes(b''.join(out))
    print(f'{a.out}: {a.out.stat().st_size} байт')


if __name__ == '__main__':
    main()
