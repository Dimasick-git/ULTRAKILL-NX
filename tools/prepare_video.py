#!/usr/bin/env python3
"""Convert a local movie to indexed JPEG frames + streaming float PCM for NX.

Usage: prepare_video.py input.webm output.nxv
No Android codec or whole-movie allocation is required at runtime.
"""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

HEADER = struct.Struct('<8s8I3Q')


def convert(src, dst):
    probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error',
        '-show_streams', '-of', 'json', str(src)], encoding='utf-8'))
    video = next(s for s in probe['streams'] if s['codec_type'] == 'video')
    width = min(640, video['width']) // 2 * 2
    height = round(width * video['height'] / video['width'] / 2) * 2
    sound = any(s['codec_type'] == 'audio' for s in probe['streams'])
    dst.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nx-video-') as temp:
        jpeg, pcm = Path(temp)/'frames.mjpeg', Path(temp)/'audio.f32'
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(src), '-an',
            '-vf', f'scale={width}:{height},fps=30', '-c:v', 'mjpeg', '-q:v', '5',
            '-pix_fmt', 'yuvj420p', '-f', 'mjpeg', str(jpeg)], check=True)
        offsets = []
        with dst.open('w+b') as out, jpeg.open('rb') as frames:
            out.write(bytes(HEADER.size))
            pending = bytearray()
            while True:
                chunk = frames.read(1024*1024)
                pending.extend(chunk)
                while True:
                    end = pending.find(b'\xff\xd9')
                    if end < 0:
                        break
                    assert pending[:2] == b'\xff\xd8', 'Invalid JPEG boundary'
                    offsets.append(out.tell())
                    out.write(pending[:end+2])
                    del pending[:end+2]
                if not chunk:
                    break
            assert not pending and offsets, 'Truncated JPEG stream'
            offsets.append(out.tell())
            index_offset = out.tell()
            count = len(offsets)-1
            out.write(struct.pack(f'<{len(offsets)}Q', *offsets))
            audio_offset = out.tell()
            audio_frames = 0
            if sound:
                duration = count/30
                subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(src),
                    '-vn', '-ac', '2', '-ar', '48000', '-af', f'apad=whole_dur={duration}',
                    '-t', str(duration), '-f', 'f32le', str(pcm)], check=True)
                assert pcm.stat().st_size % 8 == 0
                audio_frames = pcm.stat().st_size//8
                with pcm.open('rb') as audio:
                    shutil.copyfileobj(audio, out)
            out.seek(0)
            out.write(HEADER.pack(b'NXVID001', width, height, 30, 1, count,
                48000 if sound else 0, 2 if sound else 0, 0,
                index_offset, audio_offset, audio_frames))
    print(f'{dst.name}: {width}x{height}, {count} frames, {count/30:.2f}s, '
          f'audio={sound}, {dst.stat().st_size/1024**2:.1f} MiB', flush=True)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('src', type=Path)
    ap.add_argument('dst', type=Path)
    args = ap.parse_args()
    convert(args.src, args.dst)
