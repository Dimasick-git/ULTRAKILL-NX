"""Reset hacBrewPack's RomFS copy buffer after each small payload file."""
from pathlib import Path
import argparse

def main():
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);a=p.parse_args()
    old='    while (cur_file != NULL)\n    {\n        FILE *f_in'
    new='    while (cur_file != NULL)\n    {\n        read_size = 0x61A8000; /* Reset the buffer after small files. */\n        FILE *f_in'
    source=a.source.read_text(encoding='utf-8')
    if new in source:print('Buffer reset already applied');return
    assert source.count(old)==1,'Unexpected hacBrewPack romfs.c copy loop'
    backup=a.source.with_suffix('.c.before-buffer-reset')
    if not backup.exists():backup.write_bytes(a.source.read_bytes())
    a.source.write_bytes(source.replace(old,new).encode())
    print('Reset RomFS copy buffer per file; rebuild hacBrewPack')
if __name__=='__main__':main()
