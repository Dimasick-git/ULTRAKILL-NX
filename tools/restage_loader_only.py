"""Новая ревизия, в которой меняется только загрузчик (ultrakill_nx.nro).

Все остальные файлы базовой выкладки ставятся жёсткими ссылками: места почти не
занимает, а stage_full_game.py копировал бы всю игру (3.8 ГБ). Перед этим
каждый файл сверяется с отчётом базовой выкладки; в отчёт новой ревизии
переносятся проверки импорта и хеши, меняется только .nro.

    python restage_loader_only.py --base release/r53-game \
        --base-report release/r53-game.verified.json --nro loader/ultrakill_nx.nro \
        --version r54 --output release/r54-game
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil

NRO = 'ultrakill_nx.nro'
METADATA = 'assets/bin/Data/Managed/Metadata/global-metadata.dat'
BOOT = 'assets/NxPort/boot-content.json'


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def main():
    p = argparse.ArgumentParser()
    for name in ('base', 'base-report', 'nro', 'output'):
        p.add_argument('--' + name, required=True, type=Path)
    p.add_argument('--version', required=True)
    p.add_argument('--add', action='append', default=[],
                   help='путь/в/выкладке=исходный файл: новый или заменяемый файл (кроме .nro)')
    a = p.parse_args()
    adds = {}
    for item in a.add:
        rel, src = item.split('=', 1)
        rel = rel.replace('\\', '/')
        assert rel != NRO and (not rel.startswith('assets/bin/Data/') or rel == METADATA), 'Так нельзя менять: ' + rel
        adds[rel] = Path(src)
    # метаданные IL2CPP меняются только вместе с libil2cpp из той же выгрузки
    assert METADATA not in adds or 'libil2cpp.so' in adds, 'global-metadata.dat без libil2cpp.so'
    assert not a.output.exists(), 'Нужна новая, несуществующая папка выкладки'
    nro = a.nro.read_bytes()
    assert ('ultrakill_nx ' + a.version).encode() in nro, 'В .nro нет строки версии ' + a.version
    base = json.loads(a.base_report.read_text(encoding='utf-8'))
    files = base['files']
    present = {f.relative_to(a.base).as_posix() for f in a.base.rglob('*') if f.is_file()}
    assert present == set(files), 'Выкладка не совпадает с отчётом: ' + str(sorted(present ^ set(files))[:5])
    for rel, info in sorted(files.items()):
        if rel == NRO:
            continue
        src = a.base / rel
        assert src.stat().st_size == info['bytes'] and sha(src) == info['sha256'], 'Хеш не совпал: ' + rel
    linked = copied = 0
    for rel in sorted(set(files) | set(adds)):
        dst = a.output / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if rel == NRO:
            dst.write_bytes(nro)
            continue
        if rel in adds:
            shutil.copy2(adds[rel], dst)
            copied += 1
            continue
        try:
            os.link(a.base / rel, dst)
            linked += 1
        except OSError:
            shutil.copy2(a.base / rel, dst)
            copied += 1
    if METADATA in adds:
        # boot-content.json хранит хеши загрузочных файлов; новый файл вместо
        # жёсткой ссылки, чтобы не изменить исходную выкладку
        boot_path = a.output / BOOT
        boot = json.loads(boot_path.read_text(encoding='utf-8'))
        name = METADATA[len('assets/bin/Data/'):]
        assert name in boot['files'], 'В boot-content.json нет ' + name
        boot['files'][name] = sha(a.output / METADATA)
        boot_path.unlink()
        boot_path.write_text(json.dumps(boot, indent=2) + '\n', encoding='utf-8')
        adds[BOOT] = boot_path
    report = dict(base)
    report['version'] = a.version
    report['base_version'] = base.get('version')
    report['loader_only'] = True
    report['files'] = dict(files)
    report['files'][NRO] = {'bytes': len(nro), 'sha256': hashlib.sha256(nro).hexdigest()}
    for rel in adds:
        report['files'][rel] = {'bytes': (a.output / rel).stat().st_size, 'sha256': sha(a.output / rel)}
    report['added'] = sorted(adds)
    out = a.output.with_name(a.output.name + '.verified.json')
    out.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(json.dumps({'version': a.version, 'base': base.get('version'), 'files': len(report['files']),
                      'hardlinked': linked, 'copied': copied, 'nro_sha256': report['files'][NRO]['sha256']},
                     indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
