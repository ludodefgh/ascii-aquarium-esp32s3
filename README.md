# ASCII Aquarium — ESP32-S3-N8R2 / ST7789 / EC11 encoder

Port of [POWER-PILL/ASCII-Aquarium](https://github.com/POWER-PILL/ASCII-Aquarium) (originally
built for the ILI9341 "Cheap Yellow Display" with an XPT2046 resistive touchscreen) to a custom
ESP32-S3-N8R2 board with a 2.4" ST7789 320x240 SPI display and no touch panel — input is a single
EC11 rotary encoder (A/B/PUSH) plus a secondary push button (K0).

## Hardware / wiring

| Signal | GPIO |
|---|---|
| TFT SCLK | 12 |
| TFT MOSI | 11 |
| TFT RST | 8 |
| TFT DC | 9 |
| TFT CS | 10 |
| TFT BLK | 7 |
| EC11 A | 4 |
| EC11 B | 5 |
| EC11 PUSH | 6 |
| K0 | 15 |

No MISO wired (display is write-only).

## Controls

- **Rotate encoder**: open/navigate the on-screen settings menu.
- **Push encoder**: feed the fish (home screen) / select or confirm a menu item.
- **K0**: cycle background style (home screen) / back out of a menu screen.

Settings menu includes fish/bubble/seaweed/creature counts, background style, the optional clock,
and WiFi/OTA. There's a brightness item too, but it currently has no effect - see note below.

### Backlight brightness

Earlier builds drove the backlight with LEDC PWM so the menu's brightness slider actually worked.
On this breadboard build, that caused genuine screen corruption (columns of coloured noise) after
10-15s - confirmed by isolating variables one at a time (a real dual-core buffer-hand-off race got
fixed along the way too, but wasn't the cause of this) down to the PWM switching itself, most
likely injecting noise onto the shared breadboard power rail into the adjacent SPI lines. SPI at
80MHz was independently confirmed fine once PWM was off, so brightness was left disabled (flat
`digitalWrite(TFT_BL, HIGH)`) for several releases while other things were fixed.

Re-enabled in v1.0.38 (`ledcAttach(TFT_BL, 5000, 8)` + `applyLcdBrightness()` in `setup()`, after
`tft.init()` which otherwise resets the pin to a plain digital output) now that the board is
powered properly instead of drawing through a phone's USB port - the earlier corruption may well
have been that marginal supply rather than PWM itself. Revert to the plain `digitalWrite` if
corruption reappears.

WiFi credentials (for NTP time sync and OTA updates) are entered with an on-screen character
picker driven by the encoder, since there's no touchscreen keyboard.

## What's kept vs. removed from the original

Kept: ASCII fish, bubbles, seaweed, all background styles (dithered/smooth gradients, Auto Sky,
flowers), the octopus/seahorse/snail/jellyfish visitors, the optional clock (small text or
ASCII-art, 20 fonts), and WiFi + NTP sync.

Removed: XPT2046 touch input and calibration, SD-card BMP/sequence capture, and ambient RGB LED
control — none of that hardware exists on this board.

## Performance

A toggleable debug overlay (menu → "Debug Overlay") shows FPS, draw time, push
time, and free heap - useful for judging the effect of any of this.

Three layers of parallelism/optimisation:

- **DMA push in bands**: TFT_eSPI's non-DMA SPI write pokes the SPI
  peripheral's 32-pixel hardware FIFO directly; on the S3 each of the ~2400
  refills needed for a full frame requires an extra register "update"
  handshake the S3's SPI peripheral imposes, which measured ~2.6x the
  theoretical transfer time at 80MHz (40ms actual vs ~15ms theoretical for a
  320x240x16bpp frame). Pushing over DMA instead, in 48-row bands (kept under
  TFT_eSPI's 16384px per-call DMA threshold - `displayTaskFn` in
  `src/main.cpp`), hands each band to the SPI peripheral as one transfer
  instead of ~480 tiny CPU-driven ones.
  - Getting DMA working at all needed a **patch to the vendored TFT_eSPI
    copy** (`.pio/libdeps/*/TFT_eSPI/Processors/TFT_eSPI_ESP32_S3.c`, not
    part of this repo - PlatformIO re-fetches it fresh, so this patch has
    to be re-applied after any clean `lib_deps` reinstall): a known
    ESP32-S3 TFT_eSPI bug where the display freezes after the very first
    DMA transaction while software keeps reporting success on every one
    after that (see
    [Bodmer/TFT_eSPI#2233](https://github.com/Bodmer/TFT_eSPI/discussions/2233),
    [#3414](https://github.com/Bodmer/TFT_eSPI/issues/3414),
    [#3432](https://github.com/Bodmer/TFT_eSPI/discussions/3432)).
    `dma_end_callback` needs to write `0b11` (not `0`) to
    `SPI_DMA_CONF_REG`, indexed by `SPI_DMA_CH_AUTO` rather than
    `spi_host` - those are different index spaces, and the original code
    (indexed by `spi_host`) clears the wrong copy of the register. Briefly
    needed dropping `SPI_FREQUENCY` to 40MHz while a marginal power supply
    (USB current through a phone) made it flaky at 80MHz; back to 80MHz
    (see `User_Setup.h`) now that the board is powered properly.
  - Non-black backgrounds (gradient sky styles) used to cost noticeably
    more draw time than flat black - partly because every gradient row was
    being written twice (a full-width flat-colour fill, then immediately
    overwritten by the cached gradient). Each row-chunk now only fills the
    part *below* the gradient, and only pushes the cached gradient rows it
    actually overlaps - no more double-write. Tried also extending the
    cache to the *whole* screen (baking the flat colour in too, to avoid
    the separate fill entirely) but that was a net regression: it turned a
    cheap fillRect (writes one row, then memcpy's that small, cache-resident
    row repeatedly) into a genuine PSRAM read per row from a ~150KB cache
    that doesn't fit in CPU cache - reverted, gradientBandCache only holds
    the gradient band itself.

- **Double-buffered push**: the scene renders into one of two sprite buffers (both in PSRAM); a
  task on core 0 pushes the finished buffer over SPI while core 1 (`loop()`) draws the next frame
  into the other buffer, overlapping SPI transfer with drawing/physics instead of blocking on it.
- **Task-parallel drawing**: within a frame, drawing is split into small tasks pulled from a shared
  queue by core 1 (inline, between physics updates) and a second core-0 task. Tasks are grouped
  into phases matching the original fixed draw order (background → seaweed → bubbles → flakes →
  fish → visitors → clock/menu) with a barrier between phases, so layers still composite in the
  right order; within a phase, multi-entity layers (seaweed/bubbles/fish) are split into index
  ranges so both cores can drain them concurrently. Core 0's draw-helper task runs at lower
  priority than its SPI-push task, so a buffer that's ready to display always preempts it.
  Entities are *not* clustered by on-screen overlap - two same-phase entities that happen to
  overlap on screen could rarely have their draw order flip between cores, a harmless one-frame
  z-order flicker, not corruption.
  - Fish/bubbles/flakes are drawn with a custom thread-safe Font 2 character blitter
    (`directDrawCharFont2` in `src/main.cpp`) that writes straight into the sprite's raw pixel
    buffer with colour as an explicit argument, rather than TFT_eSprite's normal
    `setTextColor()`+`drawChar()`, which store the current colour as shared mutable state on the
    sprite object - unsafe if two cores are drawing different-coloured entities on it at once.
  - Seaweed uses TFT_eSprite's own `drawLine()` directly (already colour-explicit/stateless, no
    reimplementation needed).

## OTA updates

WiFi menu → "Check for Update" first does a cheap redirect-only request to learn the latest
release's tag and compares it against the running firmware's version (shown just below the WiFi
status line in that same menu) — if there's nothing newer, it shows both version numbers with an
"OK" / "Force Update" choice (rotate to pick, push to confirm) instead of just bailing out, so you
can re-flash the exact same version over the air (e.g. to recover from a bad flash) without a
cable. Otherwise it downloads the `firmware.bin` asset from this repo's latest release and flashes
it to the inactive OTA partition (`Update.h`) before rebooting — no cable needed after the first
flash. It only touches the OTA app partition, never NVS, so WiFi credentials and tank settings
survive an update.

**Remember to bump `kFirmwareVersion` in `src/main.cpp`** before tagging a new release — that's the
value the version check compares against.

**USB reflashes are different**: `ascii-aquarium-merged.bin` spans the whole flash range including
the gap where the `nvs` partition lives, so flashing it wipes saved WiFi credentials and settings
every time. For a routine reflash that preserves them, flash only `firmware.bin` at `0x10000`; only
use the merged image (or the full four-file set) for a first flash on a blank chip.

## Build

```sh
pio run
```

## Flash

```sh
pio run -t upload
```

Or manually with esptool (offsets used by the `default_8MB.csv` partition table):

```sh
esptool --chip esp32s3 --port <PORT> --baud 921600 write-flash -z \
  0x0000  bootloader.bin \
  0x8000  partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 firmware.bin
```

Release binaries (including a single merged image flashable at `0x0`) are attached to each
[GitHub release](../../releases).
