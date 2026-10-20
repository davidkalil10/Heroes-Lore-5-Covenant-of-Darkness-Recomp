# Runtime, audio, and performance

## Accepted baseline

On 2026-10-20 the user tested the isolated Windows package and confirmed fluid,
fast execution and working sound, then confirmed the same success on Android
and Switch. The accepted changes now live in the main
source tree. Builds consume `src/`; `codex_lab/` is ignored and never used by CI.

The engine still interprets most ARM32 code. This is not a full native rewrite
or GPU renderer migration. Seven measured hot regions are compiled ahead of time
using the same decoder semantics as the interpreter. The generated C++ is compiled
normally by GCC/Clang on the host; no JIT or executable memory allocation is used.

## CPU acceleration and generation

`src/core/cpu.cpp` uses a compact entry map before dispatching to
`src/core/hot_blocks.inc`. Each region validates all its guest opcodes once before
activation. A mismatch disables that region and execution uses the interpreter.
The regions accelerate framebuffer blending, stretched/rotated sprite rows,
font bit scanning, rectangle blending, blur samples, and DES resource decoding.

Run `python tools/generate_hot_blocks.py` when changing the ARM decoder. The tool
reads `src/core/cpu.cpp` and `reference/libHeroesLore5.asm` and only rewrites the
generated include. It does not modify the interpreter, header, or runtime loop.
Check the generated diff and rerun runtime validation before committing it.

## SDL audio

`src/audio/audio.cpp` opens a stereo floating-point SDL device, decodes clips
outside its callback, resamples to the obtained sample rate, and mixes up to 32
voices. The JNI bridge implements music/effect loading, play/loop, pause/resume,
stop/free, and volume. A new music voice replaces the previous music voice.

The original game assets exercised by the tests are Ogg Vorbis, despite the VFS
container being named `data.vfs.mp3`. MP3 decoding is also supported. Audio device
failures are logged and leave the game playable. `putClip` generic PCM and
`getClipStatus` retain the original fallback behavior; they are not fully modeled.
Music is decoded as a whole; measure memory and load latency on Switch before
considering streaming. Do not decode or read files in the audio callback.

Third-party sources retain their complete embedded licenses:

- `dr_mp3.h`: existing dr_libs MP3 decoder (public domain/MIT-0).
- `stb_vorbis.c`: stb Vorbis v1.22, commit
  `f0569113c93ad095470c54bf34a17b36646bbbb5` of
  [nothings/stb](https://github.com/nothings/stb/blob/f0569113c93ad095470c54bf34a17b36646bbbb5/stb_vorbis.c).

`stb_vorbis.c` is included by `audio.cpp`. Switch must list `src/audio` among its
source directories and exclude this file from `CFILES`. Desktop/Android CMake
collect C++ files recursively. No SDL2_mixer or additional audio DLL is required.

## VFS index and clocks

The native `getAssetSizeFromVFS` hook creates an index for the immutable archive
instead of reopening and scanning it on each request. It preserves first-match
hash semantics, entry sizes, and offsets relative to the archive start. A malformed
archive falls back to the original function. Do not replace the VFS during a run.

The game clock accumulates fractional real milliseconds and caps a single elapsed
interval at 250 ms. Slow frames no longer advance by an artificial 16 ms, and
sub-millisecond frames no longer add 16 ms. There is no physics catch-up loop.
Presentation relies on vsync when supported; the manual limiter handles fallback
renderers. Monitors above 60 Hz can present above 60 Hz and need separate testing.

## Validation

After a Release build:

```bash
python tools/verify_runtime.py --exe build/hl5.exe
# Linux:
python tools/verify_runtime.py --exe build/hl5
```

The script runs 1,200 loops with a fixed 16 ms clock and a scripted sequence of
menu confirmations. It uses disposable saves, SDL dummy audio, and the original
game resources. It checks two entries of all seven compiled regions (registers,
flags, instruction counts, and the full 128 MiB guest memory), compares VFS results
with the original function, and requires nonzero audio samples. A second run
disables both optimizations and all 11 frame captures must match byte for byte.
Reports are written under `build/runtime-verification/`.

GitHub Actions performs this check for Windows and Linux before packaging and
uploads diagnostic logs and summaries. This does not execute APK/NRO builds on
devices or confirm touch, controller, audio latency, or SD performance there.

## Original PC performance evidence

Three repetitions per variant, 1,200 loops, fixed simulation clock, no window,
audio, profiler, or differential verifier. Values are medians across repetitions:

| Measurement | Original | Accepted optimized build |
| --- | ---: | ---: |
| Mean processing time | 9.092 ms | 2.904 ms |
| 95th percentile | 49.740 ms | 15.219 ms |
| Largest per-run spike | 644.688 ms | 127.524 ms |
| Loops above 16.67 ms | 195 / 1200 | 41 / 1200 |
| Effects segment p95 | 78.565 ms | 18.308 ms |

Mean speedup: 3.13x. Raw measurements: `docs/performance_baseline.json`.
These are loop processing measurements, not guaranteed displayed FPS. Loading
spikes remain. The user subsequently confirmed good gameplay and audio on PC,
Android and Switch. The user also accepted the subsequent aspect modes described in
`docs/DISPLAY.md`. Version 1.1.0 services/input require separate device testing.

## Diagnostic switches

Environment switches are opt-in; remove them for regular gameplay. Boolean
switches are enabled by their presence, even if their string value is `0`.

| Variable | Purpose |
| --- | --- |
| `HL5_NO_AOT` | Disable compiled ARM regions. |
| `HL5_NO_VFS_CACHE` | Use the original archive search. |
| `HL5_VERIFY_AOT` | Compare the first two executions of each region with the interpreter; high memory use, PC diagnostics only. |
| `HL5_VERIFY_VFS` | Compare each accelerated VFS lookup with the original function. |
| `HL5_FIXED_STEP_MS` | Fixed simulation interval for reproducible tests, clamped to 1–250 ms. |
| `HL5_AUDIO_TEST` | Initialize audio during headless runs. |
| `HL5_CAPTURE_AUDIO` | WAV output path for up to 30 seconds of the actual mixer output. |
| `HL5_PROF` | Instruction histogram in `prof.txt`; disables AOT. |
| `HL5_BENCH` | Collect loop timings during graphical runs in `frame_times.csv`; headless runs always collect them. |

Normal graphical play does not retain a growing vector of timing samples.
Do not ship APK/NRO launch environments with diagnostic switches enabled.
