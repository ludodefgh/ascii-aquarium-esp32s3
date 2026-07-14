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

Settings menu includes fish/bubble/seaweed/creature counts, background style, LCD brightness
(PWM on the backlight pin), the optional clock, and WiFi/OTA.

WiFi credentials (for NTP time sync and OTA updates) are entered with an on-screen character
picker driven by the encoder, since there's no touchscreen keyboard.

## What's kept vs. removed from the original

Kept: ASCII fish, bubbles, seaweed, all background styles (dithered/smooth gradients, Auto Sky,
flowers), the octopus/seahorse/snail/jellyfish visitors, the optional clock (small text or
ASCII-art, 20 fonts), and WiFi + NTP sync.

Removed: XPT2046 touch input and calibration, SD-card BMP/sequence capture, and ambient RGB LED
control — none of that hardware exists on this board.

## Performance

The scene renders into one of two sprite buffers (both allocated in PSRAM); a task pinned to core
0 pushes the finished buffer over SPI while core 1 (the normal `loop()`) draws the next frame into
the other buffer, so SPI transfer time overlaps with drawing/physics instead of blocking it.

## OTA updates

WiFi menu → "Check for Update" first does a cheap redirect-only request to learn the latest
release's tag and compares it against the running firmware's version (shown just below the WiFi
status line in that same menu) — if there's nothing newer, it says so and skips the download
entirely. Otherwise it downloads the `firmware.bin` asset from this repo's latest release and
flashes it to the inactive OTA partition (`Update.h`) before rebooting — no cable needed after the
first flash. It only touches the OTA app partition, never NVS, so WiFi credentials and tank
settings survive an update.

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
