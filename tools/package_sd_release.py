"""Reconstruct a verified SD package on a runner, then attach two complete ZIPs.

Large unchanged files come from the previous release. A hashed input archive
provides replacements and the exact desired file inventory, including deletions.
This never modifies the previous release or publishes the destination draft.
"""
from pathlib import Path, PurePosixPath
import hashlib
import json
import os
import re
import subprocess
import zipfile


def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def safe(name):
    path = PurePosixPath(name)
    assert name and not path.is_absolute() and '\\' not in name
    assert ':' not in name and all(p not in ('.', '..') for p in name.split('/'))
    return name


def extract(path, destination):
    with zipfile.ZipFile(path) as archive:
        names = set()
        for entry in archive.infolist():
            if entry.is_dir():
                continue
            name = safe(entry.filename)
            assert name not in names
            names.add(name)
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(entry) as source, target.open('wb') as output:
                while chunk := source.read(4 * 1024 * 1024):
                    output.write(chunk)


def main():
    repo = os.environ['GITHUB_REPOSITORY']
    tag = os.environ['PACKAGE_TAG']
    patch_name = os.environ['PACKAGE_INPUT']
    patch_sha = os.environ['PACKAGE_INPUT_SHA']
    assert re.fullmatch(r'v\d+\.\d+\.\d+', tag)
    assert re.fullmatch(r'Build_Input_\d+\.\d+\.\d+\.(?:zip|index\.json)', patch_name)
    assert re.fullmatch('[a-f0-9]{64}', patch_sha)
    work = Path('release-work/sd')
    inputs, game, output = (work / p for p in ('input', 'package', 'output'))
    assert not work.exists()
    for p in (inputs, game, output):
        p.mkdir(parents=True, exist_ok=True)

    def api(path):
        return json.loads(gh('api', f'repos/{repo}/{path}'))

    releases = api('releases')
    draft = next(r for r in releases if r['tag_name'] == tag)
    assert draft['draft'] and not draft['prerelease']
    gh('release', 'download', tag, '--repo', repo, '--pattern', patch_name, '--dir', str(inputs))
    patch = inputs / patch_name
    assert digest(patch) == patch_sha
    if patch_name.endswith('.index.json'):
        descriptor = json.loads(patch.read_text(encoding='utf-8'))
        assert 0 < len(descriptor['parts']) <= 64
        merged = inputs / 'assembled.zip'
        with merged.open('wb') as dest:
            for part in descriptor['parts']:
                name = part['name']
                assert re.fullmatch(r'Build_Input_[0-9.]+_\d{2}of\d{2}\.part', name)
                gh('release', 'download', tag, '--repo', repo, '--pattern', name, '--dir', str(inputs))
                source = inputs / name
                assert source.stat().st_size == part['bytes']
                assert digest(source) == part['sha256']
                with source.open('rb') as stream:
                    while chunk := stream.read(4 * 1024 * 1024):
                        dest.write(chunk)
        assert digest(merged) == descriptor['archive_sha256']
        patch = merged
    with zipfile.ZipFile(patch) as archive:
        plan = json.loads(archive.read('__release_plan__.json'))
    assert tag == 'v' + plan['version'] and plan['base_tag'] != tag
    assert re.fullmatch('[a-zA-Z0-9._-]+', plan['base_tag'])
    expected = plan['expected']
    assert 0 < len(expected) < 10000
    assert all(safe(n) for n in expected)
    groups = [set(group) for group in plan['groups']]
    assert len(groups) == 2 and groups[0] | groups[1] == set(expected)
    assert groups[0] & groups[1] == {'README_RU.txt', 'SHA256SUMS.txt'}
    base = api('releases/tags/' + plan['base_tag'])
    base_assets = {a['name']: a for a in base['assets']}
    names = ['ULTRAKILL-NX_PART_1.zip', 'ULTRAKILL-NX_PART_2.zip']
    base_hashes = {}
    for name in names:
        gh('release', 'download', plan['base_tag'], '--repo', repo, '--pattern', name, '--dir', str(inputs))
        source = inputs / name
        assert 'sha256:' + digest(source) == base_assets[name]['digest']
        with zipfile.ZipFile(source) as archive:
            for entry in archive.infolist():
                if entry.is_dir():
                    continue
                n = safe(entry.filename)
                with archive.open(entry) as stream:
                    h = hashlib.file_digest(stream, 'sha256').hexdigest()
                assert n not in base_hashes or base_hashes[n] == h
                base_hashes[n] = h
        extract(source, game)
    sums = dict((n, h) for h, n in (line.split('  ', 1) for line in
                (game / 'SHA256SUMS.txt').read_text(encoding='utf-8-sig').splitlines() if line))
    assert {n: h for n, h in base_hashes.items() if n != 'SHA256SUMS.txt'} == sums
    extract(patch, game)
    for n, entry in expected.items():
        assert (game / n).stat().st_size == entry['bytes'], n
        assert digest(game / n) == entry['sha256'], n
    print(f'PASS: all {len(expected)} files equal the verified local package', flush=True)

    report = {}
    for name, members in zip(names, groups):
        target = output / name
        with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for n in sorted(members):
                # Fixed metadata makes output independent of runner extraction times.
                entry = zipfile.ZipInfo(n, date_time=(2026, 10, 25, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.external_attr = 0o100644 << 16
                with (game / n).open('rb') as source, archive.open(entry, 'w', force_zip64=True) as dest:
                    while chunk := source.read(4 * 1024 * 1024):
                        dest.write(chunk)
        assert target.stat().st_size < 2 * 1024**3
        with zipfile.ZipFile(target) as archive:
            assert set(archive.namelist()) == members
            for n in members:
                with archive.open(n) as stream:
                    assert hashlib.file_digest(stream, 'sha256').hexdigest() == expected[n]['sha256'], n
        report[name] = {'bytes': target.stat().st_size, 'sha256': digest(target)}
        gh('release', 'upload', tag, str(target), '--repo', repo)
        print('Uploaded and checked:', name, flush=True)
    current = api('releases/' + str(draft['id']))
    assets = {a['name']: a for a in current['assets']}
    for name, entry in report.items():
        assert assets[name]['state'] == 'uploaded' and assets[name]['size'] == entry['bytes']
        assert assets[name]['digest'] == 'sha256:' + entry['sha256']
    summary = {'runtime': plan['runtime'], 'files': len(expected), 'archives': report,
               'destination': tag, 'draft': True, 'input_sha256': patch_sha}
    print(json.dumps(summary, indent=2), flush=True)
    with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as stream:
        stream.write('Verified SD package\n\n```json\n' + json.dumps(summary, indent=2) + '\n```\n')


if __name__ == '__main__':
    main()
