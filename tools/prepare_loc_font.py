"""Instantiate the OFL Noto Sans CJK variable TTF for Unity's static font importer.

Install fonttools, then pass the upstream TTF and an output NxCjk.ttf path.
The caller must distribute the upstream SIL OFL license with the resulting font.
Source: https://github.com/notofonts/noto-cjk/tree/main/Sans/Variable/TTF
"""
import argparse
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.name != 'NxCjk.ttf':
        parser.error('Output must be named NxCjk.ttf to match the runtime bundle lookup')
    font = instantiateVariableFont(TTFont(args.input), {'wght': 400}, inplace=True)
    for name in font['name'].names:
        if name.nameID in (1, 4, 6, 16):
            name.string = 'NxCjk'.encode(name.getEncoding())
        elif name.nameID in (2, 17):
            name.string = 'Regular'.encode(name.getEncoding())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    font.save(args.output)
    print(args.output)

if __name__ == '__main__':
    main()
