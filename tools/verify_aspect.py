#!/usr/bin/env python3
"""Exercise real viewport expansion, orientation changes, live dialogs and reuse of buffers."""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def bmp(path):
    data = path.read_bytes()
    width, height = struct.unpack_from('<ii', data, 18)
    return width, abs(height), data[54:]


def has_patch(image, patch):
    width, height, data = image
    stride = width * 4
    first = patch[0]
    pos = 0
    while (pos := data.find(first, pos)) != -1:
        row, column = divmod(pos, stride)
        if column % 4 == 0 and column + len(first) <= stride and row + len(patch) <= height:
            if all(data[pos + y * stride:pos + y * stride + len(line)] == line for y, line in enumerate(patch)):
                return True
        pos += 4
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--report-dir', type=Path, default=Path('build/aspect-verification'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    reports = args.report_dir.resolve()
    reports.mkdir(parents=True, exist_ok=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith('HL5_')}
    env.update(HL5_FIXED_STEP_MS='16', HL5_AUDIO_TEST='1', SDL_AUDIODRIVER='dummy')
    keys = [part for frame in range(60, 901, 60) for part in ('--press-at', f'{frame}:-5')]
    modes = ['--aspect-at', '700:wide', '--aspect-at', '1200:native', '--aspect-at', '1450:auto',
             '--window-at', '1700:1080x2400', '--window-at', '1900:1920x1080']
    with tempfile.TemporaryDirectory(prefix='hl5-aspect-', dir=reports) as temporary:
        temporary = Path(temporary)
        dump = temporary / 'frames'
        command = [str(args.exe.resolve()), '--apk', str(root / 'reference/extracted_apk'),
                   '--data', str(temporary / 'saves'), '--headless', '--frames', '2100',
                   '--window', '1280x720', '--aspect', 'native', '--dump', str(dump),
                   '--dump-every', '100', *keys, *modes]
        log = reports / 'transitions.log'
        with log.open('w', encoding='utf-8') as output:
            subprocess.run(command, cwd=root, env=env, stdout=output, stderr=output,
                           check=True, timeout=180)
        text = log.read_text(encoding='utf-8', errors='replace')
        for forbidden in ('FAULT', 'bad free', 'exhausted', 'resize failed', 'missing export'):
            if forbidden in text:
                raise RuntimeError(f'{forbidden} in {log}')
        expected = {0: (240, 400), 800: (712, 400), 1300: (240, 400),
                    1500: (712, 400), 1800: (240, 534), 2000: (712, 400)}
        images = {}
        for frame, size in expected.items():
            image = bmp(dump / f'frame_{frame}.bmp')
            if image[:2] != size:
                raise RuntimeError(f'Frame {frame}: expected {size}, got {image[:2]}')
            images[frame] = image
        width, height, pixels = images[1500]
        for lo, hi in ((0, 180), (532, 712)):
            lit = sum(pixels[(y * width + x) * 4:(y * width + x) * 4 + 3] != b'\0\0\0'
                      for y in range(300) for x in range(lo, hi))
            if lit < 12000:
                raise RuntimeError('Expanded wing contains too little rendered scenery')
        # At least three exact 16x16 native-scale scenery patches must also occur
        # in the wide render. This catches an implementation that only stretches.
        nw, nh, native = images[1300]
        found = 0
        for x, y in ((32, 64), (64, 96), (96, 128), (144, 96), (176, 144)):
            patch = [native[((y + row) * nw + x) * 4:((y + row) * nw + x + 16) * 4] for row in range(16)]
            found += has_patch(images[1500], patch)
        if found < 3:
            raise RuntimeError(f'Native sprite/tile scale not preserved: only {found} patches found')
        audio = re.search(r'nonzero=(\d+)', text)
        if not audio or int(audio[1]) == 0:
            raise RuntimeError('No audio during aspect changes')
        # Save only representative images; temporary saves and the other captures
        # are disposed automatically and never touch the player's data.
        for frame in (1300, 1500, 1800):
            (reports / f'frame_{frame}.bmp').write_bytes((dump / f'frame_{frame}.bmp').read_bytes())
        stress = []
        for n, frame in enumerate(range(1000, 1900, 15)):
            stress += ['--aspect-at', f"{frame}:{('wide', 'native')[n % 2]}"]
        command = [str(args.exe.resolve()), '--apk', str(root / 'reference/extracted_apk'),
                   '--data', str(temporary / 'stress-saves'), '--headless', '--frames', '2100',
                   '--aspect', 'native', *keys, *stress]
        with (reports / 'stress.log').open('w', encoding='utf-8') as output:
            subprocess.run(command, cwd=root, env=env, stdout=output, stderr=output, check=True, timeout=180)
        stress_text = (reports / 'stress.log').read_text(encoding='utf-8', errors='replace')
        count = stress_text.count('[viewport] resize')
        if count != 60 or any(word in stress_text for word in ('FAULT', 'bad free', 'exhausted', 'resize failed')):
            raise RuntimeError(f'Repeated resize failed: {count}/60 changes')
        summary = dict(geometry_checks=len(expected), exact_native_tile_patches=found,
                       repeated_resizes=count, audio_nonzero_samples=int(audio[1]))
    (reports / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print('PASS: real expanded scenery; native pixel scale; portrait/landscape; 60 live switches; audio.')


if __name__ == '__main__':
    main()
