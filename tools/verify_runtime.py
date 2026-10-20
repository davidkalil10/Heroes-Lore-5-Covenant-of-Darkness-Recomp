#!/usr/bin/env python3
"""Compare deterministic accelerated/oracle runs without touching player saves."""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--report-dir', type=Path, default=Path('build/runtime-verification'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    exe = args.exe.resolve()
    reports = args.report_dir.resolve()
    reports.mkdir(parents=True, exist_ok=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith('HL5_')}
    env.update(HL5_FIXED_STEP_MS='16', HL5_AUDIO_TEST='1', SDL_AUDIODRIVER='dummy')
    keys = [arg for frame in range(60, 901, 60) for arg in ('--press-at', f'{frame}:-5')]
    summary = {}
    with tempfile.TemporaryDirectory(prefix='hl5-runtime-', dir=reports) as temporary:
        temporary = Path(temporary)
        for label in ('optimized', 'oracle'):
            run_env = env.copy()
            if label == 'optimized':
                run_env.update(HL5_VERIFY_AOT='1', HL5_VERIFY_VFS='1')
            else:
                run_env.update(HL5_NO_AOT='1', HL5_NO_VFS_CACHE='1')
            dump = temporary / label / 'frames'
            command = [str(exe), '--apk', str(root / 'reference/extracted_apk'),
                       '--data', str(temporary / label / 'saves'), '--headless',
                       '--frames', '1200', '--aspect', 'native', '--dump', str(dump), '--dump-every', '120', *keys]
            log = reports / f'{label}.log'
            with log.open('w', encoding='utf-8') as output:
                subprocess.run(command, cwd=root, env=run_env, stdout=output,
                               stderr=output, check=True, timeout=180)
            text = log.read_text(encoding='utf-8', errors='replace')
            if 'FAILURE' in text:
                raise RuntimeError(f'Differential failure: {log}')
            audio = re.search(r'\[audio\] mixer samples=(\d+) nonzero=(\d+) peak=([\d.]+)', text)
            if not audio or int(audio[2]) == 0:
                raise RuntimeError(f'Audio callback produced no signal: {log}')
            passes = len(re.findall(r'\[aot\] differential PASS', text))
            if label == 'optimized' and passes != 14:
                raise RuntimeError(f'Expected 14 ARM differential checks, got {passes}: {log}')
            if label == 'optimized' and '[vfs-cache] differential PASS' not in text:
                raise RuntimeError(f'VFS differential checks did not run: {log}')
            times = root / 'frame_times.csv'
            shutil.copy2(times, reports / f'{label}-times.csv')
            with times.open(newline='') as data:
                values = sorted(float(row['loop_ms']) for row in csv.DictReader(data))
            if len(values) != 1200:
                raise RuntimeError(f'Expected 1200 loop measurements for {label}')
            summary[label] = dict(frames=len(values), aot_checks=passes,
                                  mean_ms=statistics.mean(values),
                                  p95_ms=values[int((len(values) - 1) * .95)],
                                  audio_samples=int(audio[1]), nonzero_samples=int(audio[2]))
        expected = sorted((temporary / 'oracle/frames').glob('*.bmp'))
        if len(expected) != 11:
            raise RuntimeError(f'Expected 11 reference images, got {len(expected)}')
        for original in expected:
            optimized = temporary / 'optimized/frames' / original.name
            if original.read_bytes() != optimized.read_bytes():
                raise RuntimeError(f'Render mismatch: {original.name}')
        summary['identical_images'] = len(expected)
    (reports / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print('PASS: 14 ARM state checks; VFS oracle; 11 identical images; audio signal in both runs.')
    print(f'Reports: {reports}')


if __name__ == '__main__':
    main()
