// TFT_eSPI setup for a 2.4" ST7789 320x240 SPI (4-wire) display wired to an
// ESP32-S3-N8R2, replacing the original CYD/ILI9341+XPT2046 configuration.
//
// This file is force-included by platformio.ini via `-include` together with
// `-DUSER_SETUP_LOADED=1`, so TFT_eSPI's bundled User_Setup_Select.h is
// bypassed entirely - no need to touch anything inside the library.
//
// No MISO wired: we never read back from the panel, so TFT_MISO is left
// undefined on purpose.

#define USER_SETUP_INFO "ESP32-S3 ST7789 320x240 custom (no touch)"
#define USER_SETUP_ID 9001

// --- Display driver ---
#define ST7789_DRIVER

// ST7789 240x320 panels are natively portrait; the sketch runs in landscape
// via tft.setRotation(1) at runtime, so width/height stay in native order.
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Most 2.4" ST7789 modules need this to avoid inverted colours.
// Comment out if colours look wrong on first boot.
#define TFT_INVERSION_ON

// --- Pin mapping ---
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8
#define TFT_BL    7
#define TFT_BACKLIGHT_ON HIGH
// TFT_MISO intentionally not defined: MISO is not wired on this board.

// --- SPI speed ---
#define SPI_FREQUENCY  40000000

// --- Fonts ---
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
// SMOOTH_FONT (VLW anti-aliased fonts loaded from SPIFFS) is intentionally
// left off: this sketch only uses the built-in fixed fonts above, and the
// Arduino-ESP32 2.0.18 core build used here (see platformio.ini comment)
// doesn't ship a SPIFFS library, which TFT_eSPI would otherwise require.
