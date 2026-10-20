# Platform services — version 1.1.0

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
  rasterized via stb_truetype. F10, R3 and a touch/mouse button expose it globally.
  The hook at guest `StateMainMenu::DrawOption` 0x21a1ec exposes the panel from the
  original options entry; closing returns control to the original options.
- `src/input_mapping.*`: keyboard and labeled Nintendo/controller policy.

The host freezes guest frames while Settings is open. A restore sets a mandatory
restart state: the guest must never resume and overwrite restored disk files with
old in-memory saves. Existing save formats remain raw guest bytes; no conversion
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
player confirms replacement, then closes and reopens the app on success.

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
release sets a badge in Settings; offline startup remains playable. A `main` push
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

Pending device validation: actual Google authorization, upload on one platform
and restore on another with a real character; Android installer permissions and
signature-preserving upgrade; Switch HTTPS/NRO replacement under real homebrew
launch paths; AppImage replacement. Cross-compilation does not prove these flows.

Sources: [Google device authorization](https://developers.google.com/identity/protocols/oauth2/limited-input-device),
[GitHub release API](https://docs.github.com/en/rest/releases/releases),
[nlohmann/json v3.12.0](https://github.com/nlohmann/json/releases/tag/v3.12.0),
[curl CA bundle](https://curl.se/docs/caextract.html).
