# Real viewport expansion

## Player controls

Cycle Native → Wide → Auto → Native with F9, the top-right NATIVE/WIDE/AUTO
button (mouse or touch), or Back/Select + Start (Switch − and +). Individual
Back/Start actions are sent on release; a chord consumes both actions. F11 or
Alt+Enter toggles desktop fullscreen. The preference is stored separately from
saves in `<data-root>/display.ini`; missing/invalid preference defaults to Native.
`--aspect native|wide|auto` overrides startup without writing the preference.

Native uses 240×400. Wide uses 712×400 (16:9 rounded to an even pixel width).
Auto uses the actual SDL window size and expands one native dimension: 640×400
for 16:10, 956×400 for 3440×1440, or 240×534 for 1080×2400 portrait. Neither
dimension is reduced below the native canvas. Each dimension is capped at 1280
to bound software rendering and guest memory. Extreme proportions may retain
borders at this cap. SDL always scales uniformly to fit; no geometry is stretched.
More pixels entail more software drawing, so Native remains a useful fallback.
Maps cannot reveal content outside their authored boundaries; menus and artwork
retain the original layout rules rather than acquiring newly authored assets.

## Engine implementation

`src/display_layout.*` contains pure sizing policy, preference handling, chord
logic, and the SDL host button. `src/main.cpp` creates the window before boot to
obtain its dimensions; Auto debounces resize events by 120 ms and recreates the
streaming texture only when logical dimensions change. Touch input is mapped
through the uniform destination rectangle; the host button consumes its gesture.
Existing active game touch is released before changing the canvas.

`Game::resizeViewport` in `src/hle/hle.cpp` uses the shipped ARM library's
`nativeInitDisplay` export. It frees old texture metadata and framebuffer pixels,
reallocates the scene cache, and retains the engine's framebuffer handles. The
previous image is centered/copied until the next native timed repaint, avoiding
an otherwise black intermediate frame. No game restart or save migration occurs.

Verified guest structure fields for the shipped binary (not portable ABIs):

- MIDASKernelManager: screen handle +0x4e0, pixels +0x4f0, bits/pixel +0x4f4,
  width +0x4fc, height +0x500, pitch +0x504.
- `_lcdG` Graphic object: current/default/cache framebuffer handles +0/+4/+8;
  framebuffer record width/height/pitch/bpp/pixels +0/+4/+8/+0xc/+0x10.
  Call `Graphic::ChangeFrameBuff` after reinitialization to refresh dimensions.
- Map::Paint at 0x1b129c: the CPU hook repaints pending cache once, using the
  currently painted map object. `Map::MapSet` recenters/clamps its camera using
  the previous canvas center; preserve scroll speed +0xb8/+0xba and flag +0xbc.
  A newly loaded map already uses the new canvas and needs no extra recentering.
- EventProc::Event_DialogWindow at 0x16afd0: resize live boxes/text widths and
  bottom anchoring while preserving text, typewriter progress and event state.
  Index is signed byte +0xac, records stride 60, box x/y/w/h +0xc4/+0xc6/+0xc8/+0xca,
  text x/y/w +0xbe/+0xc0/+0xc2 relative to the indexed event record.

Resize validates the Map paint address/opcode and dialog export before touching
these structures. Changing the guest library requires rechecking offsets/hooks.
ARM instruction semantics and the generated hot blocks are unchanged.

## Verification

```sh
ctest --test-dir build --output-on-failure
python tools/verify_runtime.py --exe build/hl5.exe
python tools/verify_aspect.py --exe build/hl5.exe
# Linux: replace build/hl5.exe with build/hl5
```

CTest covers aspect sizing including portrait/ultrawide, saved preferences,
button bounds and controller chords in both orders. The aspect script uses
disposable saves and dummy SDL audio: it checks logical dimensions, populated
extra scenery, byte-identical 16×16 scenery patches at native scale, continued
audio, orientation changes with an active dialogue, and 60 live resizes. Reports
and representative captures go to `build/aspect-verification/`. The runtime
verifier retains the original native rendering/ARM/audio regression checks.

Diagnostic options `--window WxH`, `--window-at frame:WxH` and
`--aspect-at frame:native|wide|auto` support reproducible tests. Desktop Wide
selection enlarges a portrait window to 960×540; mobile uses its existing screen.

PC checks do not replace Android/Switch tests: check touch/button placement,
rotation, docked/handheld operation, existing saves, UI, map boundaries and
performance on larger maps. The user's successful Android/Switch report applies
to the preceding performance/audio integration, before these aspect changes.
