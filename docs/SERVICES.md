# Platform services — version 1.1.4

## Architecture and reference ports

The OAuth device-code flow and Windows/curl/Android JNI transports were adapted
from `heroes_lore_zero_recomp/src/platform/cloud_save.cpp`, also inspected alongside
HL1/HL2. HL5 uses the same installed OAuth client, requesting `drive.file`, but a
separate Drive folder/file: `Heroes Lore - 5/heroes_lore_5_save.json`. Never reuse
the other games' filenames or copy their Java RMS/VM reload code into this ARM port.

- `src/service_net.*`: WinHTTP on Windows, curl on Linux/Switch, existing
  SDLActivity HTTP/download/APK methods through JNI on Android. TLS peer/host
  verification stays enabled. Switch embeds `assets/cacert.pem` in RomFS.
- `src/services.*`: one owned SDL worker, mutex-protected value snapshots and
  cancellable login polling. Worker shutdown joins before networking/SDL teardown.
  No worker executes ARM code or accesses the renderer. Only requested operations
  write Drive; startup only reads the latest public GitHub release.
- `src/service_data.*`: JSON release parsing, numeric stable version comparison,
  URL constraints, SHA256, strict base64, save archive validation and staged restore.
- `src/service_ui.*`: responsive SDL Settings/Cloud/Update panels with Inter font
  rasterized via stb_truetype. F10/R3 expose it globally; an explicit native Options
  footer row exposes it through mouse/touch and the original menu navigation.
  The panel opens only from the explicit Options row or F10/R3. Native StateMainMenu::DrawOption at
  0x21a1ec only records the native Options screen; it never opens the modal on paint.
  The original option editing remains independent of the explicit port action.
- `src/input_mapping.*`: keyboard and labeled Nintendo/controller policy.

The host freezes guest frames while Settings is open. Restore now stages a
main-thread handoff: the old campaign must be destroyed before restored files
replace local saves, then title metadata must be reconstructed. Existing save formats remain raw guest bytes; no conversion
to host structs or other games' formats occurs. Sound continues in the modal.

## Cloud archive and restoration

Archive JSON has `app: heroes_lore_5`, `version: 1`, `platform`, and a `files`
object. Each key is an `internal/...` or `external/...` relative path containing
`data` (base64) and `sha256`. Extensionless `internal/kickass` and `LOCAL.sav` are
included. Dotfiles, symlinks, traversal, Windows reserved names, case collisions,
invalid base64/checksums and other game IDs are rejected. Limits: 64 files, 16 MiB
decoded total, 32 MiB JSON. Tokens, dictionaries, display/service settings, prior
backups and update packages are outside these two save directories and excluded.

Restore validates everything before writing, stages complete new directories,
moves old ones to `<data-root>/save-backups/<timestamp>/`, then renames staged
directories into place. A failed directory swap rolls back prior moves. Complete
replacement avoids mixing an old local slot with the downloaded snapshot. The
player confirms replacement, then returns to the native title in the same process.

Upload snapshots flushed disk files while the guest is paused. Users must save
inside the game first; unsaved RAM progress is not serialized. Before upload or
restore, the worker requeries the selected Drive file's ID/version and refuses if
it changed since the displayed backup. This reduces accidental overwrites; it is
not a distributed lock against simultaneous writes from multiple devices.
Backups are manual, never automatically uploaded on startup or exit.

OAuth tokens are per-device in `<data-root>/cloud_auth.json`, written by replace;
Unix permissions are 0600. The installed OAuth client values are reused from the
reference port in `src/google_oauth.h`; obfuscation is not secure secret storage.
Do not log access/refresh tokens or include them in packages, examples or commits.
Login polling honors pending/slow_down/denied/expiry and can be cancelled.

## Releases and installation

The endpoint is `https://api.github.com/repos/davidkalil10/Heroes-Lore-5-Covenant-of-Darkness-Recomp/releases/latest`.
Only stable numeric `vMAJOR.MINOR.PATCH` versions newer than `services::Version`
are offered. Assets must use this repository's HTTPS release URL and exact names:

| Platform | Required release asset |
| --- | --- |
| Windows | `heroes_lore_5_windows_x64.zip` |
| Switch | `heroes_lore_5.nro` |
| Android | `heroes_lore_5_android_universal.apk` |
| Linux | `heroes_lore_5_linux_x86_64.AppImage` |

Downloads have a 128 MiB transport/file limit and must match expected length and
file magic. When GitHub provides an asset SHA256 digest, it must match too. Older
release assets without a digest rely on HTTPS, length and format checks.

Windows writes `assets/update_windows.ps1` under the writable update directory,
launches it hidden and exits. The helper waits for process exit, validates ZIP
paths, extracts only allowlisted runtime files/resources (only `data/dict.bin`
from data), and keeps previous binaries/resources for rollback. It refuses links
in the install path, preserves player data/preferences/tokens, and relaunches the
same executable with the same data root. Failures log to `updates/update-install.log`.

Switch uses the actual NRO path from argv, stages a sibling `.new`, unmounts RomFS,
retains the previous NRO as `.old-<timestamp>` and swaps the new file. The guest
cannot resume after unmount/success; the user closes/reopens. Linux AppImage uses
`APPIMAGE` with the same staged swap. Native Linux development executables are
not replaced by an AppImage. Android stages a signed APK and opens the OS
installer; permission/installation stays with Android. The APK provider grants
read access only to APKs within the app's own `data/updates` directory.

Auto-check is on by default; `services.json` stores the user's choice. A newer
release opens the Update panel once; a banner is visible during the check.
Offline startup remains playable and retries are bounded to three attempts. A `main` push
only creates Actions artifacts. Publish a stable release/tag for the updater to
see it, keeping all version metadata consistent:

1. Change `services::Version` in `src/service_data.h`.
2. Change Switch `APP_VERSION` and Android `versionName`; increment `versionCode`.
3. Validate and build all platforms.
4. Publish the matching `vX.Y.Z` tag/release with the exact assets above (existing
   Actions release job handles tags or explicit release dispatch).

This change does not automatically publish a release. Version 1.1.0 will only
offer a subsequently published newer stable version.

## Packaging and validation

Packagers now copy only public `data/dict.bin`, never the whole data directory.
They include the service font/license, Windows installer and CA bundle.
Android's asset manifest records SHA256 as well as size. Unchanged manifests use
the existing size fast path; after an update, equal-size changed resources are
also detected and refreshed. A successful manifest fingerprint is cached after
extraction. Android upgrades must use the same keystore and preserve app data.
Linux AppRun uses writable XDG user data, independent of read-only AppImage mounts.

```sh
ctest --test-dir build --output-on-failure
python tools/verify_services.py
python tools/verify_runtime.py --exe build/hl5.exe
python tools/verify_aspect.py --exe build/hl5.exe
```

CTest covers SHA256 known vectors, binary archive roundtrips, invalid archives,
cross-game isolation, restore safety copies, mock Drive query/upload/restore,
mock release/download integrity, confirmation cancel, UI and Nintendo/# mapping.
It never accesses a real Google account. UI captures are saved under
`build/services-verification`. The packaging test uses disposable installations;
Windows tests replacement, rollback, path traversal and preserved saves/tokens.
All packagers are tested for private-data exclusion and Android manifest hashes.
CI runs them on Windows/Linux, alongside prior runtime/display checks.

The user confirmed actual Google backup and restore across devices on 1.1.0.
Pending device validation for 1.1.1: live language/mobile/Switch font fixes,
return-to-title restore, Android installer permissions and
signature-preserving upgrade; Switch HTTPS/NRO replacement under real homebrew
launch paths; AppImage replacement. Cross-compilation does not prove these flows.

Sources: [Google device authorization](https://developers.google.com/identity/protocols/oauth2/limited-input-device),
[GitHub release API](https://docs.github.com/en/rest/releases/releases),
[nlohmann/json v3.12.0](https://github.com/nlohmann/json/releases/tag/v3.12.0),
[curl CA bundle](https://curl.se/docs/caextract.html).


## 1.1.1 restore lifecycle and UI regression fixes

restoreBackup() validates download contents on the worker and publishes
Status::restoreReady; it MUST NOT write active save directories or request exit.
On the main thread, Game::returnToTitle() calls the opcode-checked native
BaseState::ChangeState at 0x142838 with (0,5,3). This factory destroys the old
state stack and creates StateMainMenu with title substate 3. Only afterward does
applyPendingRestore() perform transactional replacement. Call returnToTitle()
again so constructors reload all three SlotInfo records from the restored files,
then close the overlay/reset held controls and continue the same app loop.
The first title construction reads old slot data; the second is intentional.
Never substitute Java RMS reload code or resume the previous in-memory campaign.

resourcePath now joins romfs:/ without producing romfs://. The font atlas is
uploaded through SDL_CreateTextureFromSurface; a geometry-based emergency font
keeps Settings usable if font loading/upload fails. Android has proportional
panel/font/row sizing instead of desktop pixel caps. Root panel includes live
language selection and the gear replaces both old overlay buttons.

CTest stages a different cloud snapshot, asserts no worker-side save mutation,
applies the handoff, checks no process restart/quit, and captures portrait,
landscape, Switch-sized and emergency-font UI. HL5_UI_LAYOUT_ANDROID=1 and
HL5_UI_FONT_FALLBACK=1 are diagnostic switches, not player settings.
Version metadata is managed by tools/bump_version.py (1.1.1 / Android code 3).


## 1.1.2 independent Settings and gear

Removed the DrawOption hook and begin/end guest-frame auto-open bookkeeping.
There is no implicit native-options→host-panel transition, so closing the panel
cannot reopen it from a guest paint. Removed the misleading Game settings row
that only closed the panel. Gear geometry is shared by draw/hit testing through
settingsButtonRect: desktop/Switch 24–40 pixels, Android 40–72. Its small top
margin keeps it above the native back/menu button as the window grows.

User confirmed Android OTA, live language switching and restore without process
exit on 1.1.1. Switch OTA remains pending the user's test with 1.1.2.
Version metadata: 1.1.2 / Android code 4. CTest checks fullscreen geometry and
repeated panel open/close; tools/verify_ui.py exercises original title Options
and verifies it never opens the host panel.

## 1.1.3 — explicit native entry and safe Android resource upgrades

This supersedes the 1.1.2 gear instructions. There is no floating gear or gear hit
target. Original Options contains a fixed footer action, Port settings / Opções do
port, on both title and in-game screens. Painting never opens the host modal.
Only confirming/tapping the new row (or F10/R3) opens it. Back closes the modal
without closing the original menu. Native option values/counts remain intact.

The Android regression was an extraction source/destination alias: SDL 2.30.12
SDL_RWFromFile first opens relative names under internal storage, before trying
APK assets. A changed asset that already existed could therefore be opened as
the source and truncated when the destination was opened with wb. The published
1.1.2 ARM64 APK was inspected: its complete PT dictionary and SHA manifest were
present and valid. The fault was in refreshing the extracted copy.

ensureAndroidAssets now uses AAssetManager explicitly for the manifest and all
source streams. services::extractAsset stages, checks size/SHA-256, then replaces
with rollback; failed/truncated/wrong-digest reads preserve the previous file.
Only a completely successful pass updates the manifest fingerprint. Existing
empty/corrupt resources are repaired on next boot; saves/tokens are separate.
Reference: SDL release-2.30.12 src/file/SDL_rwops.c, SDL_RWFromFile Android branch:
https://github.com/libsdl-org/SDL/blob/release-2.30.12/src/file/SDL_rwops.c#L511-L560

CTest covers existing-file same-size replacement, empty reads, stale bytes with a
new digest, preservation on failure, R3/F10 and embedded PT without external assets.
verify_ui.py covers explicit title/game entry, close/reopen and original options
remaining independent. Keep actual device upgrade verification separate from CI.
Version 1.1.3, Android code 5, same package names/signing/save paths.

## 1.1.4 — startup notice, streaming progress and libnx installation

The screenshot from Switch 1.1.1 identified filesystem::copy_file returning ENOSYS
for the .download → .new stage. All three earlier ports use fread/fwrite, C rename
and envSetNextLoad. HL5 now uses the same supported copy primitives through
service_data::installPortableUpdate, retaining stronger staged verification and
rollback instead of initially truncating the running NRO.

The helper closes both streams, checks copied size and package magic/SHA-256,
then calls the Switch RomFS-release callback, renames the original to a unique
.old-* sibling and commits the staged NRO. Commit failure restores the previous
file; downloads/size/hash failures never unmount RomFS or touch the running NRO.
After unmounting, guest frames remain blocked, including on error. Success sets
envSetNextLoad for the exact launched NRO path; closing the app starts that path.
Linux AppImage reuses the portable staged swap. Windows/Android installers remain.

Startup requests already existed, but the visible badge was removed with the
floating gear. Status now exposes checkingUpdates/updateNotice; the shared main
loop polls retries and the host UI consumes a one-shot notice. Checking is shown
as a banner without pausing play; a discovered release opens Update once. Back
from that startup prompt dismisses it to the game. Explicit saved autoCheck=false
is respected. HTTP/offline/parse failures retry after 5 then 15 seconds, at most
three automatic attempts. No-update and offline startup do not show a modal.

Downloads stream to disk with byte/total snapshots under the services mutex:
WinHTTP reads, libcurl XFERINFO (Switch/Linux), and the Android synchronous JNI
download callback. UI draws a percentage/MB label and bar while downloading,
then verifies before enabling installation. Keep peer/host TLS verification and
the RomFS CA bundle; do not copy the earlier ports' disabled TLS policy.

CTest covers startup retry/notice/dismiss/preference, intermediate progress and
portable NRO backup, corrupt download rejection before unmount, and rollback.
verify_services.py runs the actual native transport against a local redirecting
HTTP server, compares 2 MiB of bytes and requires intermediate progress callbacks.
Only temporary files and mocked Google/GitHub are used. Other runtime/locale/UI
verifiers remain required. No local test substitutes for Switch hardware testing.

Migration: Switch versions with the broken installer need one manual NRO
replacement to receive 1.1.4. Publishing a fixed release cannot change the copy
routine in the already-running 1.1.1 executable. Preserve the data directory.
