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

WiFi credentials (for NTP time sync) are entered with an on-screen character picker driven by the
encoder, since there's no touchscreen keyboard.

## What's kept vs. removed from the original

Kept: ASCII fish, bubbles, seaweed, all background styles (dithered/smooth gradients, Auto Sky,
flowers), the octopus/seahorse/snail/jellyfish visitors, the optional clock (small text or
ASCII-art, 20 fonts), and WiFi + NTP sync.

Removed: XPT2046 touch input and calibration, SD-card BMP/sequence capture, and ambient RGB LED
control — none of that hardware exists on this board.

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
