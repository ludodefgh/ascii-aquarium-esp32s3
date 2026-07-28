# ASCII Aquarium — CLAUDE.md

ESP32-S3-N8R2 port of ASCII-Aquarium (PlatformIO/Arduino). Custom board: ST7789 320x240 SPI TFT
(no MISO, write-only), EC11 rotary encoder + K0 push button, no touchscreen. See `README.md` for
wiring, features, and the performance/DMA notes — this file is about *how to build, flash, and test
it*, and about the separate `ludodefgh/launcher` integration.

GitHub repo: `ludodefgh/ascii-aquarium-esp32s3`.

## Build & flash (standalone)

```sh
pio run                 # build
pio run -t upload       # build + flash over USB (upload_speed=921600 already set in platformio.ini)
```

Manual esptool flash (routine reflash, preserves NVS/WiFi creds/settings — only `firmware.bin` at
`0x10000`, not the merged image):

```sh
esptool --chip esp32s3 --port <PORT> --baud 921600 write-flash -z 0x10000 firmware.bin
```

Only use the merged image / full four-file set (`0x0`, `0x8000`, `0xe000`, `0x10000`) for a first
flash on a blank chip — it spans the `nvs` partition and wipes saved settings every time otherwise.

**Bump `kFirmwareVersion` in `src/main.cpp`** before tagging a GitHub release — the OTA version
check compares against it.

## Testing

This is embedded firmware with a physical display and rotary-encoder input — there is no
simulator. Treat "it compiles" as necessary, not sufficient. Any UI/behavior change needs an actual
flash-and-observe cycle on real hardware before calling it done; ask the user to check the device
(or walk them through it) rather than inferring correctness from the code alone.

The serial port is the ESP32-S3's **native USB CDC** (typically `/dev/ttyACM0`, not an external
UART bridge) — it can reliably run much faster than its default baud. If a manual `esptool` flash
doesn't pass `--baud`, it silently falls back to a slow default (~180 kbit/s observed); always pass
`-b 921600` explicitly for any ad-hoc esptool invocation outside of `pio run -t upload` (which
already sets `upload_speed=921600`) — this alone took a combined-image flash from ~130s to ~21s.

If a flash fails with "port is busy / could not exclusively lock", something (often a leftover
`platformio device monitor` or a serial log capture) is holding the port open — check with
`fuser /dev/ttyACM0` before assuming a hardware problem.

## Running as a guest app under ludodefgh/launcher

`ludodefgh/launcher` is a separate multi-app ESP32 bootloader project (its own repo, its own
ESP-IDF/`idf.py` build). Aquarium can run as one of its guest apps via a second PlatformIO
environment here — see the "Running as a guest app" section in `README.md` for the mechanics
(`partitions_launcher_guest.csv`, `[env:esp32-s3-n8r2-launcher-guest]`, `LAUNCHER_GUEST_MODE`,
`scripts/build_launcher_guest.sh`). Two independent build systems by design — see
[ludodefgh/launcher#11](https://github.com/ludodefgh/launcher/issues/11).

### Critical: don't touch the live launcher checkout

`/home/ludovic/Documents/Projects/Bootloader` is a **separate, live checkout of `ludodefgh/launcher`
with its own active Claude Code session** — never read from or write to it directly; it may be
mid-edit. To inspect or build the launcher's source, clone it fresh into a scratchpad/temp
directory instead (a throwaway `git clone https://github.com/ludodefgh/launcher`, or use
`scripts/build_launcher_guest.sh` with no arguments, which clones into `.launcher-cache/`,
gitignored).

To report a launcher bug/design question, or to check on one already filed, use `gh` against
`ludodefgh/launcher` (issues, not direct edits) — that repo's own `CLAUDE.md` documents this as its
expected workflow for cross-session collaboration. Wait for the other session to push a fix, then
`git pull` the scratch clone, rebuild, re-flash, and re-test — don't attempt to fix launcher source
yourself and push it.

Sequence used repeatedly and reliably during initial integration testing:
1. Read the relevant launcher source in the scratch clone to root-cause a bug precisely (file/line
   references, not guesses).
2. File a GitHub issue on `ludodefgh/launcher` with the diagnosis and a concrete suggested fix.
3. Wait for the other session to push; `git pull` the scratch clone.
4. Rebuild the launcher (see below), re-merge with Aquarium's guest binary, re-flash, re-test.
5. Comment back on the issue confirming fixed/regressed, with fresh evidence (log excerpts,
   observed behavior) if it's not fixed.

### Building the launcher itself (Docker, no native ESP-IDF install needed)

```sh
docker run --rm -v "$SCRATCH":/workspaces/launcher -w /workspaces/launcher \
  espressif/idf:release-v5.5 bash -c '. $IDF_PATH/export.sh && idf.py build'
```

Gotchas hit repeatedly:
- **New Kconfig options added upstream aren't picked up by a plain `idf.py build`** on an existing
  `build/` dir (stale component-requirements resolution — documented in the launcher's own
  `CLAUDE.md`). Fix: `rm -rf build && idf.py build` — but run the `rm -rf build` **inside the same
  docker container**, not from the host; files created by a root-uid container are otherwise
  host-undeletable (`Permission denied`).
- Don't run `idf.py set-target` again after `rm -rf build` unless actually switching chips — it can
  reset `sdkconfig` to defaults, wiping manually-set values (WiFi creds, OTA URL, encoder invert,
  etc.). A plain `rm -rf build && idf.py build` (no `set-target`) preserves `sdkconfig` since that
  file lives at the project root, not inside `build/`.
- Don't bind-mount a host path that doesn't exist yet (e.g. `-v $AQUARIUM_BUILD:/workspaces/x:ro`
  before that dir exists) — Docker auto-creates it as `root:root`, breaking host-side writes/deletes
  to it afterward. Copy files into the (host-owned) scratch clone dir instead of mounting the
  Aquarium build dir directly.
- A Kconfig `bool` set to `n` is **absent** from `sdkconfig.h` entirely (not defined as 0) — matters
  if launcher-side C reads it as a plain expression rather than only inside `#if`.

After building, merge with Aquarium's own guest build and flash:

```sh
# from the launcher scratch clone, after copying Aquarium's
# .pio/build/esp32-s3-n8r2-launcher-guest/firmware.bin in as aquarium-guest.bin
docker run --rm -v "$SCRATCH":/workspaces/launcher -w /workspaces/launcher espressif/idf:release-v5.5 \
  bash -c '. $IDF_PATH/export.sh && python tools/merge_with_guest.py --slot app_slot1 \
           --guest-bin aquarium-guest.bin --skip-build -o combined.bin'

esptool --chip esp32s3 -p /dev/ttyACM0 -b 921600 --before default_reset --after hard_reset \
  write_flash 0x0 combined.bin
```

### OTA manifest — every release needs `launcher.manifest.json`

The launcher's OTA download flow resolves this repo's actual GitHub releases via
`"github_repo": "ludodefgh/ascii-aquarium-esp32s3"` in the launcher operator's own manifest (no more
hand-maintained per-version entries — that was a workaround, since obsoleted by
[launcher#29](https://github.com/ludodefgh/launcher/issues/29)). For the launcher to auto-pick the
right binary per chip target from any given release, **that release must carry an asset named
exactly `launcher.manifest.json`** — schema documented at
[`ludodefgh/launcher/docs/launcher-manifest.md`](https://github.com/ludodefgh/launcher/blob/main/docs/launcher-manifest.md).
Without it, the launcher falls back to showing the user a raw asset list to pick from manually —
not broken, just not automatic.

`scripts/build_launcher_guest.sh` writes a correct one to `firmware_output/launcher.manifest.json`
after building; `.github/workflows/launcher-sync-release.yml` generates and attaches it
automatically. **When cutting a release by hand (bumping `kFirmwareVersion`), don't forget to
attach it too** — minimal form if the release doesn't bundle the launcher's own binaries:

```json
{
  "targets": { "esp32s3": "aquarium-guest.bin" }
}
```

Add `"flash_images"` (see the schema doc) only if `launcher-bootloader.bin`,
`launcher-partition-table.bin`, `launcher-ota_data_initial.bin`, and `launcher-app.bin` are also
attached to that same release.
