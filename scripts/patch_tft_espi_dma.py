"""
Pre-build step: patch TFT_eSPI's vendored ESP32-S3 DMA callback.

PlatformIO downloads TFT_eSPI into .pio/libdeps fresh on first build (and on
any clean/reinstall), so a hand edit made straight to that copy doesn't
survive. This re-applies the fix every build; it's a no-op once the file is
already patched.

Bug: dma_end_callback (the DMA transaction's post_cb) is supposed to reset
SPI_DMA_CONF_REG after each transfer, but the stock library both writes the
wrong value (0, not 0b11) and indexes the register by `spi_host` instead of
`SPI_DMA_CH_AUTO` - two different index spaces, so it clears the wrong copy
of the register. Net effect: only the first DMA transaction after
spi_bus_add_device() actually reads fresh data; every one after that
re-sends whatever was last sitting in the SPI FIFO registers, which looks
like the screen freezing on frame 1 while the software reports every push as
successful. See Bodmer/TFT_eSPI discussions #2233, #3432 and issue #3414.
"""

import pathlib

Import("env")  # noqa: F821 - injected by PlatformIO

OLD_LINE = "WRITE_PERI_REG(SPI_DMA_CONF_REG(spi_host), 0);"
NEW_LINE = "WRITE_PERI_REG(SPI_DMA_CONF_REG(SPI_DMA_CH_AUTO), 0b11);"


def patch_tft_espi_dma():
    project_dir = pathlib.Path(env.subst("$PROJECT_DIR"))  # noqa: F821
    matches = list(project_dir.glob(".pio/libdeps/*/TFT_eSPI/Processors/TFT_eSPI_ESP32_S3.c"))
    if not matches:
        print("[patch_tft_espi_dma] TFT_eSPI_ESP32_S3.c not found yet (library not installed?) - skipping")
        return

    for path in matches:
        text = path.read_text()
        if NEW_LINE in text:
            print(f"[patch_tft_espi_dma] already patched: {path}")
            continue
        if OLD_LINE not in text:
            print(f"[patch_tft_espi_dma] WARNING: expected line not found in {path} - "
                  "library version may have changed, DMA fix not applied")
            continue
        path.write_text(text.replace(OLD_LINE, NEW_LINE))
        print(f"[patch_tft_espi_dma] patched: {path}")


patch_tft_espi_dma()
