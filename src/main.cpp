
// ===== part: new_header =====
// ASCII Aquarium - ESP32-S3-N8R2 / ST7789 320x240 / EC11 encoder + K0 button
//
// Ported from POWER-PILL/ASCII-Aquarium (CYD: ILI9341 320x240 + XPT2046
// touchscreen). This build replaces the touchscreen UI with a single EC11
// rotary encoder (A/B/PUSH) and a secondary push button (K0); SD-card
// screenshot capture and ambient RGB LED control are removed since this
// board has none of that hardware. WiFi + NTP time sync are kept: the SSID
// and password are entered with an encoder-driven character picker instead
// of the original on-screen keyboard. The core simulation (fish, bubbles,
// seaweed, background styles, optional clock, and the small visitor
// creatures) is unchanged.
//
// Wiring:
//   TFT   SCLK=12 MOSI=11 RST=8 DC=9 CS=10 BLK=7   (no MISO)
//   EC11  A=4 B=5 PUSH=6
//   K0    15
#include <Arduino.h>
#include <cstring>
#include <SPI.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <time.h>
#include "Input.h"

// Temporary diagnostic: log free heap and the largest contiguous free block
// (what actually matters for a big mbedTLS/WiFi allocation) at each WiFi/OTA
// step, to check whether repeated WiFi connect/OTA attempts progressively
// fragment or leak heap.
static void logHeap(const char* where) {
  Serial.printf("[HEAP] %-24s free=%u minFree=%u largestBlock=%u\n", where, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMinFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

// Bump this on every release - it's what "Check for Update" compares
// against the latest GitHub release tag to decide whether there's actually
// anything newer to download.
static constexpr const char* kFirmwareVersion = "v1.0.13";
static constexpr const char* kSketchVersionLabel = kFirmwareVersion;

// GitHub's "latest/download/<asset>" URL always redirects to whatever
// release is currently marked latest, so this never needs updating when a
// new version ships - only the release itself needs a "firmware.bin" asset.
static constexpr const char* kOtaFirmwareUrl =
    "https://github.com/ludodefgh/ascii-aquarium-esp32s3/releases/latest/download/firmware.bin";
// Lightweight redirect (no body) instead of hitting the JSON API, just to
// learn the latest release's tag name before deciding whether to download
// the (much bigger) firmware.bin at all.
static constexpr const char* kOtaLatestReleaseUrl =
    "https://github.com/ludodefgh/ascii-aquarium-esp32s3/releases/latest";

// Root CAs needed to validate the OTA download's TLS chain without
// disabling certificate checking: github.com itself (Sectigo, chains to
// USERTrust ECC) redirects to a release-assets.githubusercontent.com CDN
// URL (Let's Encrypt, chains to ISRG Root X1) that serves the actual bytes.
// Both roots are long-lived (valid into 2035/2038) and self-signed, so this
// bundle only needs to change if GitHub switches CA providers.
static constexpr const char* kOtaTrustedRootCAs =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
    "TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
    "cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
    "WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
    "ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
    "MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
    "h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
    "0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
    "A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
    "T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
    "B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
    "B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
    "KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
    "OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
    "jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
    "qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
    "rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
    "HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
    "hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
    "ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
    "3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
    "NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
    "ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
    "TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
    "jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
    "oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
    "4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
    "mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
    "emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
    "-----END CERTIFICATE-----\n"
    "-----BEGIN CERTIFICATE-----\n"
    "MIICjzCCAhWgAwIBAgIQXIuZxVqUxdJxVt7NiYDMJjAKBggqhkjOPQQDAzCBiDEL\n"
    "MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl\n"
    "eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT\n"
    "JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMjAx\n"
    "MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNVBAgT\n"
    "Ck5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVUaGUg\n"
    "VVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBFQ0MgQ2VydGlm\n"
    "aWNhdGlvbiBBdXRob3JpdHkwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAQarFRaqflo\n"
    "I+d61SRvU8Za2EurxtW20eZzca7dnNYMYf3boIkDuAUU7FfO7l0/4iGzzvfUinng\n"
    "o4N+LZfQYcTxmdwlkWOrfzCjtHDix6EznPO/LlxTsV+zfTJ/ijTjeXmjQjBAMB0G\n"
    "A1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAOBgNVHQ8BAf8EBAMCAQYwDwYD\n"
    "VR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNoADBlAjA2Z6EWCNzklwBBHU6+4WMB\n"
    "zzuqQhFkoJ2UOQIReVx7Hfpkue4WQrO/isIJxOzksU0CMQDpKmFHjFJKS04YcPbW\n"
    "RNZu9YO6bVi9JNlWSOrvxKJGgYhqOkbRqZtNyWHa0V1Xahg=\n"
    "-----END CERTIFICATE-----\n";

// ------------------------------ Display Geometry -----------------------------
static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
static const uint16_t BG_COLOR = TFT_BLACK;
static const int SEA_LEVEL_Y = SCREEN_H - 36;
static const int BACKGROUND_GRADIENT_H = 80;
static const int BACKGROUND_DITHER_AMPLITUDE = 18;
static const int MAIN_SPRITE_COLOR_DEPTH = 16;

// ------------------------------ Settings enums --------------------------------
enum AutoSkySlot {
  AUTO_SKY_SUNRISE,
  AUTO_SKY_DAY,
  AUTO_SKY_SUNSET,
  AUTO_SKY_NIGHT,
  AUTO_SKY_SLOT_COUNT,
  AUTO_SKY_SLOT_NONE = 255
};

enum ClockField {
  CLOCK_FIELD_YEAR,
  CLOCK_FIELD_MONTH,
  CLOCK_FIELD_DAY,
  CLOCK_FIELD_HOUR,
  CLOCK_FIELD_MINUTE,
  CLOCK_FIELD_COUNT
};

enum ClockDisplayStyle {
  CLOCK_STYLE_SMALL_TEXT,
  CLOCK_STYLE_ASCII,
  CLOCK_STYLE_COUNT
};

enum ClockSmallPosition {
  CLOCK_SMALL_TOP,
  CLOCK_SMALL_BOTTOM,
  CLOCK_SMALL_POSITION_COUNT
};

static const int ASCII_CLOCK_ROWS = 11;
static const int ASCII_CLOCK_GLYPH_GAP = 1;
static const int ASCII_CLOCK_CHAR_W = 6;
static const int ASCII_CLOCK_ROW_H = 10;
static const int ASCII_CLOCK_Y = 82;

// Forward declaration: serviceWifi() (in the kept Utility functions) saves
// settings on a successful connection, but savePersistentState() itself is
// defined later, in the fresh Persistence section.
void savePersistentState();

// ===== part: aquarium_controls_1 =====
// ------------------------------ Aquarium Controls ----------------------------
static const int MIN_FISH = 6;
static const int MAX_FISH = 36;
static const int DEFAULT_FISH = 16;

static const int MIN_BUBBLES = 0;
static const int MAX_BUBBLES = 50;
static const int DEFAULT_BUBBLES = 10;

static const int DEFAULT_OCTOPUS_FREQUENCY = 12;
static const int OCTOPUS_FREQUENCY_OPTIONS[] = {1, 2, 4, 6, 12, 60};
static constexpr int OCTOPUS_FREQUENCY_OPTION_COUNT =
    sizeof(OCTOPUS_FREQUENCY_OPTIONS) / sizeof(OCTOPUS_FREQUENCY_OPTIONS[0]);
static const int DEFAULT_SEAHORSE_FREQUENCY = 2;
static const int SEAHORSE_FREQUENCY_OPTIONS[] = {1, 2, 4, 6, 12, 60};
static constexpr int SEAHORSE_FREQUENCY_OPTION_COUNT =
    sizeof(SEAHORSE_FREQUENCY_OPTIONS) / sizeof(SEAHORSE_FREQUENCY_OPTIONS[0]);
static const int DEFAULT_AUTO_FEED_FREQUENCY = 0;
static const int AUTO_FEED_FREQUENCY_OPTIONS[] = {0, 1, 2, 4, 6, 12, 60};
static constexpr int AUTO_FEED_FREQUENCY_OPTION_COUNT =
    sizeof(AUTO_FEED_FREQUENCY_OPTIONS) / sizeof(AUTO_FEED_FREQUENCY_OPTIONS[0]);
static const int DEFAULT_SNAIL_FREQUENCY = 2;
static const int DEFAULT_JELLYFISH_FREQUENCY = 1;
static const int CREATURE_FREQUENCY_OPTIONS[] = {0, 1, 2, 4, 6, 12, 60};
static constexpr int CREATURE_FREQUENCY_OPTION_COUNT =
    sizeof(CREATURE_FREQUENCY_OPTIONS) / sizeof(CREATURE_FREQUENCY_OPTIONS[0]);
static const int AUTO_FEED_SPRINKLE_COUNT = 9;
static const unsigned long AUTO_FEED_SPRINKLE_MIN_MS = 1200UL;
static const unsigned long AUTO_FEED_SPRINKLE_MAX_MS = 2800UL;
static const unsigned long AUTO_FEED_FIRST_DROP_MS = 2000UL;


// ===== part: aquarium_controls_2 =====
static const float MIN_SWAY = 0.25f;
static const float MAX_SWAY = 2.5f;
static const float DEFAULT_SWAY = 1.10f;

static const float MIN_SEAWEED_LENGTH = 0.80f;
static const float MAX_SEAWEED_LENGTH = 1.60f;
static const float DEFAULT_SEAWEED_LENGTH = 1.35f;

static const float MIN_SEAWEED_LENGTH_RANDOMNESS = 0.00f;
static const float MAX_SEAWEED_LENGTH_RANDOMNESS = 0.50f;
static const float DEFAULT_SEAWEED_LENGTH_RANDOMNESS = 0.35f;

static const int MAX_FLAKES = 16;
static const int MAX_FISH_POOL = 48;

static const float FISH_SWIM_WAVE_AMPLITUDE = 1.5f;
static const float FISH_SWIM_WAVE_SPEED = 5.6f;  // 30% slower than the first wave test
static const float FISH_SWIM_WAVE_SPACING = 0.85f;

static const float FISH_AVOID_RADIUS_X = 52.0f;
static const float FISH_AVOID_RADIUS_Y = 20.0f;
static const float FISH_AVOID_STRENGTH = 4.2f;
static const float FISH_CENTER_Y_OFFSET = 7.0f;

static const float OCTOPUS_EXIT_PAD = 42.0f;
static const float OCTOPUS_CENTER_Y_OFFSET = 8.0f;
static const float OCTOPUS_FISH_AVOID_RADIUS_X = 76.0f;
static const float OCTOPUS_FISH_AVOID_RADIUS_Y = 34.0f;
static const float OCTOPUS_FISH_AVOID_STRENGTH = 8.0f;
static const float OCTOPUS_FISH_CLEAR_RADIUS_X = 46.0f;
static const float OCTOPUS_FISH_CLEAR_RADIUS_Y = 22.0f;
static const int CTHULHU_SPAWN_CHANCE_PERCENT = 1;
static const unsigned long CTHULHU_FEED_PELLET_THRESHOLD = 1000UL;
static const float CTHULHU_FISH_AVOID_RADIUS_SCALE = 1.55f;
static const float CTHULHU_FISH_AVOID_STRENGTH_SCALE = 1.85f;
static const float CTHULHU_FISH_CLEAR_RADIUS_SCALE = 1.30f;

static const float SEAHORSE_EXIT_PAD = 48.0f;
static const float SEAHORSE_CENTER_X_OFFSET = 15.0f;
static const float SEAHORSE_CENTER_Y_OFFSET = 24.0f;
static const float SEAHORSE_FISH_AVOID_RADIUS_X = 58.0f;
static const float SEAHORSE_FISH_AVOID_RADIUS_Y = 38.0f;
static const float SEAHORSE_FISH_AVOID_STRENGTH = 6.0f;
static const float SEAHORSE_FISH_CLEAR_RADIUS_X = 34.0f;
static const float SEAHORSE_FISH_CLEAR_RADIUS_Y = 28.0f;
static const float SEAHORSE_SPEED_BOOST = 1.35f;
static const float VISITOR_CLEAR_RADIUS_X = 56.0f;
static const float VISITOR_CLEAR_RADIUS_Y = 38.0f;
static const float SNAIL_EXIT_PAD = 46.0f;
static const float SNAIL_CENTER_X_OFFSET = 9.0f;
static const float SNAIL_CENTER_Y_OFFSET = 7.0f;
static const float SNAIL_FISH_AVOID_RADIUS_X = 30.0f;
static const float SNAIL_FISH_AVOID_RADIUS_Y = 18.0f;
static const float SNAIL_FISH_AVOID_STRENGTH = 4.0f;
static const float SNAIL_FISH_CLEAR_RADIUS_X = 20.0f;
static const float SNAIL_FISH_CLEAR_RADIUS_Y = 12.0f;
static const unsigned long SNAIL_MIN_SPAWN_MS = 900000UL;   // 15 minutes
static const unsigned long SNAIL_MAX_SPAWN_MS = 2100000UL;  // 35 minutes
static const float JELLYFISH_CENTER_X_OFFSET = 12.0f;
static const float JELLYFISH_CENTER_Y_OFFSET = 15.0f;
static const float JELLYFISH_FISH_AVOID_RADIUS_X = 42.0f;
static const float JELLYFISH_FISH_AVOID_RADIUS_Y = 38.0f;
static const float JELLYFISH_FISH_AVOID_STRENGTH = 4.6f;
static const float JELLYFISH_FISH_CLEAR_RADIUS_X = 30.0f;
static const float JELLYFISH_FISH_CLEAR_RADIUS_Y = 26.0f;
static const float SQUID_EXIT_PAD = 34.0f;
static const float SQUID_CENTER_X_OFFSET = 20.0f;
static const float SQUID_CENTER_Y_OFFSET = 6.0f;
static const float SQUID_FISH_AVOID_RADIUS_X = 50.0f;
static const float SQUID_FISH_AVOID_RADIUS_Y = 24.0f;
static const float SQUID_FISH_AVOID_STRENGTH = 5.2f;
static const float SQUID_FISH_CLEAR_RADIUS_X = 32.0f;
static const float SQUID_FISH_CLEAR_RADIUS_Y = 16.0f;

// ===== part: background_clock_wifi_consts =====
enum BackgroundStyle {
  BACKGROUND_STYLE_BLACK,
  BACKGROUND_STYLE_DITHERED,
  BACKGROUND_STYLE_SMOOTH,
  BACKGROUND_STYLE_FLOWERS,
  BACKGROUND_STYLE_COUNT
};
static const BackgroundStyle DEFAULT_BACKGROUND_STYLE = BACKGROUND_STYLE_DITHERED;
static const BackgroundStyle kBackgroundCycleStyles[] = {
    BACKGROUND_STYLE_BLACK,
    BACKGROUND_STYLE_DITHERED,
    BACKGROUND_STYLE_SMOOTH,
    BACKGROUND_STYLE_FLOWERS,
};
static const int BACKGROUND_CYCLE_STYLE_COUNT = sizeof(kBackgroundCycleStyles) / sizeof(kBackgroundCycleStyles[0]);

static const int CLOCK_MIN_YEAR = 2024;
static const int CLOCK_MAX_YEAR = 2099;
static const int DEFAULT_CLOCK_YEAR = 2026;
static const int DEFAULT_CLOCK_MONTH = 1;
static const int DEFAULT_CLOCK_DAY = 1;
static const int DEFAULT_CLOCK_HOUR = 12;
static const int DEFAULT_CLOCK_MINUTE = 0;

static constexpr const char* CLOCK_NTP_1 = "pool.ntp.org";
static constexpr const char* CLOCK_NTP_2 = "time.nist.gov";
static constexpr const char* CLOCK_NTP_3 = "time.google.com";
static const int DEFAULT_TIMEZONE_INDEX = 5;  // Central time, matching the original hard-coded default.

static const int MAX_WIFI_NETWORKS = 12;
static const int WIFI_SSID_MAX_LEN = 32;
static const int WIFI_PASS_MAX_LEN = 64;
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 18000UL;
static const unsigned long WIFI_RECONNECT_DELAY_MS = 15000UL;
static const unsigned long WIFI_SERVICE_ACTIVE_MS = 250UL;
static const unsigned long WIFI_SERVICE_UNSYNCED_MS = 1000UL;
static const unsigned long WIFI_SERVICE_IDLE_MS = 5000UL;
static const unsigned long WIFI_SERVICE_SYNCED_MS = 10000UL;
static const unsigned long NTP_RETRY_MS = 5000UL;
static const unsigned long NTP_REFRESH_MS = 3600000UL;
static const unsigned long BACKGROUND_RAINBOW_CYCLE_MS = 120000UL;
static const unsigned long BACKGROUND_RAINBOW_UPDATE_MS = 1000UL;

// ===== part: objects_structs =====
// ------------------------------ Objects --------------------------------------
struct Flake {
  bool active;
  float x, y;
  float vy;
  uint16_t color;
};

struct Bubble {
  bool active;
  float x, y;
  float baseX;
  float vy;
  float phase;
  float swayAmp;
  uint16_t color;
};

struct FishSpecies {
  const char* right;  // ASCII-only, faces right; mirrored at boot for vx < 0 (Font 2 safe)
  uint16_t baseColor;
};

struct Fish {
  bool active;
  int type;
  float x, y;
  float vx, vy;
  float speed;
  float phase;
  float wanderBias;
  int visualWidth;
  uint16_t displayColor;
  uint16_t renderColor;
  float depthBrightness;
};

struct Octopus {
  bool active;
  bool cthulhu;
  float x;
  float y;
  float baseY;
  float vx;
  float phase;
  float colorPhase;
  unsigned long nextSpawnMs;
};

struct Seahorse {
  bool active;
  bool facingRight;
  float x;
  float y;
  float baseY;
  float vx;
  float phase;
  float finPhase;
  unsigned long nextSpawnMs;
};

struct Snail {
  bool active;
  bool facingRight;
  float x;
  float y;
  float vx;
  float phase;
  unsigned long nextSpawnMs;
};

struct Jellyfish {
  bool active;
  float x;
  float y;
  float baseY;
  float vx;
  float phase;
  unsigned long nextSpawnMs;
};


// ===== part: timezone_struct =====
struct TimezoneOption {
  const char* label;
  const char* posix;
};

// ===== part: clock_glyph_structs =====
struct AsciiClockGlyph {
  char c;
  const char* rows[ASCII_CLOCK_ROWS];
};

struct AsciiClockFont {
  const char* label;
  uint8_t rowCount;
  uint8_t glyphGap;
  const AsciiClockGlyph* glyphs;
  uint8_t glyphCount;
};

// ===== part: timezone_table =====
static const TimezoneOption timezoneOptions[] = {
    {"UTC", "UTC0"},
    {"Hawaii", "HST10"},
    {"Alaska", "AKST9AKDT,M3.2.0/2,M11.1.0/2"},
    {"Pacific", "PST8PDT,M3.2.0/2,M11.1.0/2"},
    {"Mountain", "MST7MDT,M3.2.0/2,M11.1.0/2"},
    {"Central", "CST6CDT,M3.2.0/2,M11.1.0/2"},
    {"Eastern", "EST5EDT,M3.2.0/2,M11.1.0/2"},
    {"Atlantic", "AST4ADT,M3.2.0/2,M11.1.0/2"},
    {"Newfound", "NST3:30NDT,M3.2.0/2,M11.1.0/2"},
    {"UTC-3", "UTC3"},
    {"UTC-2", "UTC2"},
    {"UTC-1", "UTC1"},
    {"UK", "GMT0BST,M3.5.0/1,M10.5.0/2"},
    {"Central EU", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {"Eastern EU", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"UTC+3", "UTC-3"},
    {"Iran", "IRST-3:30"},
    {"Gulf", "GST-4"},
    {"UTC+5", "UTC-5"},
    {"India", "IST-5:30"},
    {"UTC+6", "UTC-6"},
    {"UTC+7", "UTC-7"},
    {"China", "CST-8"},
    {"Japan", "JST-9"},
    {"Darwin", "ACST-9:30"},
    {"Sydney", "AEST-10AEDT,M10.1.0/2,M4.1.0/3"},
    {"UTC+11", "UTC-11"},
    {"New Zealand", "NZST-12NZDT,M9.5.0/2,M4.1.0/3"},
    {"UTC+13", "UTC-13"},
    {"UTC+14", "UTC-14"},
};
static constexpr int TIMEZONE_COUNT = sizeof(timezoneOptions) / sizeof(timezoneOptions[0]);

// ===== part: color_helpers =====
// RGB565 theme colours (names match your list)
static constexpr uint16_t RGB565(uint8_t r, uint8_t g, uint8_t b) {
  return uint16_t((r & 0xF8) << 8 | (g & 0xFC) << 3 | (b >> 3));
}

uint16_t scaleRgb565(uint16_t color, float brightness) {
  if (brightness < 0.0f) brightness = 0.0f;
  if (brightness > 1.0f) brightness = 1.0f;
  uint16_t r = (uint16_t)(((color >> 11) & 0x1F) * brightness);
  uint16_t g = (uint16_t)(((color >> 5) & 0x3F) * brightness);
  uint16_t b = (uint16_t)((color & 0x1F) * brightness);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

uint16_t randomBubbleColor() {
  return RGB565((uint8_t)random(0, 5), (uint8_t)random(8, 24), (uint8_t)random(45, 106));
}

uint16_t randomFoodColor() {
  // Keep food orange but vary brightness/tint slightly for a natural look.
  return RGB565((uint8_t)random(220, 256), (uint8_t)random(118, 166), (uint8_t)random(0, 34));
}

int rgb565R8(uint16_t color) { return ((color >> 11) & 0x1F) * 255 / 31; }
int rgb565G8(uint16_t color) { return ((color >> 5) & 0x3F) * 255 / 63; }
int rgb565B8(uint16_t color) { return (color & 0x1F) * 255 / 31; }

uint16_t colorWheel565(uint16_t phase) {
  phase %= 1536;
  uint8_t segment = phase / 256;
  uint8_t offset = phase & 0xFF;
  uint8_t inv = 255 - offset;

  switch (segment) {
    case 0:
      return RGB565(255, offset, 0);
    case 1:
      return RGB565(inv, 255, 0);
    case 2:
      return RGB565(0, 255, offset);
    case 3:
      return RGB565(0, inv, 255);
    case 4:
      return RGB565(offset, 0, 255);
    default:
      return RGB565(255, 0, inv);
  }
}

uint16_t rainbowColorAtMs(unsigned long now) {
  uint32_t phase = ((now % BACKGROUND_RAINBOW_CYCLE_MS) * 1536UL) / BACKGROUND_RAINBOW_CYCLE_MS;
  return colorWheel565((uint16_t)phase);
}

static const uint16_t DEFAULT_ASCII_CLOCK_COLOR = RGB565(0, 20, 95);
static const uint16_t DEFAULT_SMALL_CLOCK_COLOR = TFT_WHITE;
static const uint16_t DEFAULT_BACKGROUND_GRADIENT_COLOR = RGB565(0, 8, 255);
static const uint16_t DEFAULT_BACKGROUND_PURPLE_COLOR = RGB565(108, 6, 220);
static const uint16_t DEFAULT_AUTO_SKY_SUNRISE_COLOR = RGB565(255, 128, 20);
static const uint16_t DEFAULT_AUTO_SKY_DAY_COLOR = DEFAULT_BACKGROUND_GRADIENT_COLOR;
static const uint16_t DEFAULT_AUTO_SKY_SUNSET_COLOR = RGB565(255, 96, 16);
static const uint16_t DEFAULT_AUTO_SKY_NIGHT_COLOR = DEFAULT_BACKGROUND_PURPLE_COLOR;

static const uint16_t kBackgroundColorPalette[] = {
    DEFAULT_BACKGROUND_GRADIENT_COLOR,
    DEFAULT_BACKGROUND_PURPLE_COLOR,
    RGB565(0, 180, 255),
    RGB565(0, 220, 220),
    RGB565(0, 180, 120),
    RGB565(40, 220, 80),
    RGB565(160, 255, 40),
    RGB565(255, 220, 40),
    RGB565(255, 128, 20),
    RGB565(255, 40, 40),
    RGB565(255, 120, 200),
    RGB565(220, 60, 255),
    RGB565(120, 80, 255),
    RGB565(80, 180, 255),
    RGB565(40, 80, 255),
    RGB565(156, 120, 255),
    DEFAULT_ASCII_CLOCK_COLOR,
    RGB565(0, 76, 76),
    RGB565(116, 108, 18),
    RGB565(130, 56, 0),
    RGB565(100, 0, 0),
    RGB565(58, 20, 120),
    TFT_WHITE,
};
static constexpr int BACKGROUND_COLOR_COUNT = sizeof(kBackgroundColorPalette) / sizeof(kBackgroundColorPalette[0]);

// ===== part: ascii_clock_fonts_and_fish_species =====
static const AsciiClockGlyph kAsciiClockStandardGlyphs[] = {
    {'0', {"  ___", " / _ \\", "| | | |", "| |_| |", " \\___/", ""}},
    {'1', {" _", "/ |", "| |", "| |", "|_|", ""}},
    {'2', {" ____", "|___ \\", "  __) |", " / __/", "|_____|", ""}},
    {'3', {" _____", "|___ /", "  |_ \\", " ___) |", "|____/", ""}},
    {'4', {" _  _", "| || |", "| || |_", "|__   _|", "   |_|", ""}},
    {'5', {" ____", "| ___|", "|___ \\", " ___) |", "|____/", ""}},
    {'6', {"  __", " / /_", "| '_ \\", "| (_) |", " \\___/", ""}},
    {'7', {" _____", "|___  |", "   / /", "  / /", " /_/", ""}},
    {'8', {"  ___", " ( _ )", " / _ \\", "| (_) |", " \\___/", ""}},
    {'9', {"  ___", " / _ \\", "| (_) |", " \\__, |", "  /_/", ""}},
    {':', {" ", " _", "(_)", " _", "(_)", " "}},
    {' ', {" ", " ", " ", " ", " ", " "}},
};

static const AsciiClockGlyph kAsciiClockBulbheadGlyphs[] = {
    {'0', {"  ___", " / _ \\", "( (_) )", " \\___/"}},
    {'1', {" __", "/  )", " )(", "(__)"}},
    {'2', {" ___", "(__ \\", " / _/", "(____)"}},
    {'3', {" ___", "(__ )", " (_ \\", "(___/"}},
    {'4', {"  __", " /. |", "(_  _)", "  (_)"}},
    {'5', {" ___", "| __)", "|__ \\", "(___/"}},
    {'6', {"  _", " / )", "/ _ \\", "\\___/"}},
    {'7', {" ___", "(__ )", " / /", "(_/"}},
    {'8', {" ___", "( _ )", "/ _ \\", "\\___/"}},
    {'9', {" ___", "/ _ \\", "\\_  /", " (_/"}},
    {':', {"", "()", "", "()"}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockDoomGlyphs[] = {
    {'0', {" _____", "|  _  |", "| |/' |", "|  /| |", "\\ |_/ /", " \\___/", "", ""}},
    {'1', {" __", "/  |", "`| |", " | |", "_| |_", "\\___/", "", ""}},
    {'2', {" _____", "/ __  \\", "`' / /'", "  / /", "./ /___", "\\_____/", "", ""}},
    {'3', {" _____", "|____ |", "    / /", "    \\ \\", ".___/ /", "\\____/", "", ""}},
    {'4', {"   ___", "  /   |", " / /| |", "/ /_| |", "\\___  |", "    |_/", "", ""}},
    {'5', {" _____", "|  ___|", "|___ \\", "    \\ \\", "/\\__/ /", "\\____/", "", ""}},
    {'6', {"  ____", " / ___|", "/ /___", "| ___ \\", "| \\_/ |", "\\_____/", "", ""}},
    {'7', {" ______", "|___  /", "   / /", "  / /", "./ /", "\\_/", "", ""}},
    {'8', {" _____", "|  _  |", " \\ V /", " / _ \\", "| |_| |", "\\_____/", "", ""}},
    {'9', {" _____", "|  _  |", "| |_| |", "\\____ |", ".___/ /", "\\____/", "", ""}},
    {':', {"", " _", "(_)", "", " _", "(_)", "", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockGracefulGlyphs[] = {
    {'0', {"  __", " /  \\", "(  0 )", " \\__/"}},
    {'1', {"  __", " /  \\", "(_/ /", " (__)"}},
    {'2', {" ____", "(___ \\", " / __/", "(____)"}},
    {'3', {" ____", "( __ \\", " (__ (", "(____/"}},
    {'4', {"  ___", " / _ \\", "(__  (", "  (__/"}},
    {'5', {"  ___", " / __)", "(___ \\", "(____/"}},
    {'6', {"  ___", " / __)", "(  _ \\", " \\___/"}},
    {'7', {" ____", "(__  )", "  / /", " (_/"}},
    {'8', {" ____", "/ _  \\", ") _  (", "\\____/"}},
    {'9', {" ___", "/ _ \\", "\\__  )", "(___/"}},
    {':', {" _", "(_)", " _", "(_)"}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockOgreGlyphs[] = {
    {'0', {"  ___", " / _ \\", "| | | |", "| |_| |", " \\___/", ""}},
    {'1', {" _", "/ |", "| |", "| |", "|_|", ""}},
    {'2', {" ____", "|___ \\", "  __) |", " / __/", "|_____|", ""}},
    {'3', {" _____", "|___ /", "  |_ \\", " ___) |", "|____/", ""}},
    {'4', {" _  _", "| || |", "| || |_", "|__   _|", "   |_|", ""}},
    {'5', {" ____", "| ___|", "|___ \\", " ___) |", "|____/", ""}},
    {'6', {"  __", " / /_", "| '_ \\", "| (_) |", " \\___/", ""}},
    {'7', {" _____", "|___  |", "   / /", "  / /", " /_/", ""}},
    {'8', {"  ___", " ( _ )", " / _ \\", "| (_) |", " \\___/", ""}},
    {'9', {"  ___", " / _ \\", "| (_) |", " \\__, |", "   /_/", ""}},
    {':', {"", " _", "(_)", " _", "(_)", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockSmallGlyphs[] = {
    {'0', {"  __", " /  \\", "| () |", " \\__/", ""}},
    {'1', {" _", "/ |", "| |", "|_|", ""}},
    {'2', {" ___", "|_  )", " / /", "/___|", ""}},
    {'3', {" ____", "|__ /", " |_ \\", "|___/", ""}},
    {'4', {" _ _", "| | |", "|_  _|", "  |_|", ""}},
    {'5', {" ___", "| __|", "|__ \\", "|___/", ""}},
    {'6', {"  __", " / /", "/ _ \\", "\\___/", ""}},
    {'7', {" ____", "|__  |", "  / /", " /_/", ""}},
    {'8', {" ___", "( _ )", "/ _ \\", "\\___/", ""}},
    {'9', {" ___", "/ _ \\", "\\_, /", " /_/", ""}},
    {':', {" _", "(_)", " _", "(_)", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockSoftGlyphs[] = {
    {'0', {"", "  ,--.", " /    \\", "|  ()  |", " \\    /", "  `--'", ""}},
    {'1', {"", " ,--.", "/   |", "`|  |", " |  |", " `--'", ""}},
    {'2', {"", " ,---.", "'.-.  \\", " .-' .'", "/   '-.", "'-----'", ""}},
    {'3', {"", ",----.", "'.-.  |", "  .' <", "/'-'  |", "`----'", ""}},
    {'4', {"", "  ,---.", " /    |", "/  '  |", "'--|  |", "   `--'", ""}},
    {'5', {"", ",-----.", "|  .--'", "'--. `\\", ".--'  /", "`----'", ""}},
    {'6', {"", "  ,--.", " /  .'", "|  .-.", "\\   o |", " `---'", ""}},
    {'7', {"", ",-----.", "'--,  /", " .'  /", "/   /", "`--'", ""}},
    {'8', {"", " ,---.", "|  o  |", ".'   '.", "|  o  |", " `---'", ""}},
    {'9', {"", " ,---.", "| o   \\", "`..'  |", " .'  /", " `--'", ""}},
    {':', {"", "", ".--.", "'--'", ".--.", "'--'", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClock3DASCIIGlyphs[] = {
    {'0', {"  ****", " *///**", "/*  */*", "/* * /*", "/**  /*", "/*   /*", "/ ****", " ////"}},
    {'1', {"  **", " ***", "//**", " /**", " /**", " /**", " ****", "////"}},
    {'2', {"  ****", " */// *", "/    /*", "   ***", "  *//", " *", "/******", "//////"}},
    {'3', {"  ****", " */// *", "/    /*", "   ***", "  /// *", " *   /*", "/ ****", " ////"}},
    {'4', {"    **", "   */*", "  * /*", " ******", "/////*", "    /*", "    /*", "    /"}},
    {'5', {" ******", "/*////", "/*****", "///// *", "     /*", " *   /*", "/ ****", " ////"}},
    {'6', {"  ****", " */// *", "/*   /", "/*****", "/*/// *", "/*   /*", "/ ****", " ////"}},
    {'7', {" ******", "//////*", "     /*", "     *", "    *", "   *", "  *", " /"}},
    {'8', {"  ****", " */// *", "/*   /*", "/ ****", " */// *", "/*   /*", "/ ****", " ////"}},
    {'9', {"  ****", " */// *", "/*   /*", "/ ****", " ///*", "   *", "  *", " /"}},
    {':', {"", "", "", "", " **", "//", " **", "//"}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockChunkyGlyphs[] = {
    {'0', {" ______", "|      |", "|  --  |", "|______|", ""}},
    {'1', {" ____", "|_   |", " _|  |_", "|______|", ""}},
    {'2', {" ______", "|__    |", "|    __|", "|______|", ""}},
    {'3', {" ______", "|__    |", "|__    |", "|______|", ""}},
    {'4', {" _____", "|  |  |", "|__    |", "   |__|", ""}},
    {'5', {" ______", "|    __|", "|__    |", "|______|", ""}},
    {'6', {" ______", "|    __|", "|  __  |", "|______|", ""}},
    {'7', {" ______", "|      |", "|_     |", "  |____|", ""}},
    {'8', {" ______", "|  __  |", "|  __  |", "|______|", ""}},
    {'9', {" ______", "|  __  |", "|__    |", "|______|", ""}},
    {':', {" __", "|__|", " __", "|__|", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockCricketGlyphs[] = {
    {'0', {" _______", "|   _   |", "|.  |   |", "|.  |   |", "|:  1   |", "|::.. . |", "`-------'", ""}},
    {'1', {" _____", "| _   |", "|.|   |", "`-|.  |", "  |:  |", "  |::.|", "  `---'", ""}},
    {'2', {" _______", "|       |", "|___|   |", " /  ___/", "|:  1  \\", "|::.. . |", "`-------'", ""}},
    {'3', {" _______", "|   _   |", "|___|   |", " _(__   |", "|:  1   |", "|::.. . |", "`-------'", ""}},
    {'4', {" ___ ___", "|   Y   |", "|   |   |", "|____   |", "    |:  |", "    |::.|", "    `---'", ""}},
    {'5', {" _______", "|   _   |", "|   1___|", "|____   |", "|:  1   |", "|::.. . |", "`-------'", ""}},
    {'6', {" _______", "|   _   |", "|   1___|", "|.     \\", "|:  1   |", "|::.. . |", "`-------'", ""}},
    {'7', {" _______", "|   _   |", "|___|   |", "   /   /", "  |   |", "  |   |", "  `---'", ""}},
    {'8', {" _______", "|   _   |", "|.  |   |", "|.  _   |", "|:  1   |", "|::.. . |", "`-------'", ""}},
    {'9', {" _______", "|   _   |", "|   |   |", " \\___   |", "|:  1   |", "|::.. . |", "`-------'", ""}},
    {':', {" __", "|__|", " __", "|__|", "", "", "", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockFuzzyGlyphs[] = {
    {'0', {" .--.", ": ,. :", ": :: :", ": :; :", "`.__.'", "", ""}},
    {'1', {"  ,-.", ".'  :", " `: :", "  : :", "  :_;", "", ""}},
    {'2', {".---.", "`--. :", "  ,','", ".'.'_", ":____;", "", ""}},
    {'3', {".----.", "`--  ;", " .' '", " _`,`.", "`.__.'", "", ""}},
    {'4', {"  .-.", " .'.'", ".'.'_", ":_ ` :", "  :_:", "", ""}},
    {'5', {".----.", ": .--'", "`. `.", ".-`, :", "`.__.'", "", ""}},
    {'6', {"  .-.", " .'.'", ".' '.", ": .; :", "`.__.'", "", ""}},
    {'7', {".----.", "`--  ;", " ,','", " : :", " :_:", "", ""}},
    {'8', {" .--.", ": .; :", "`.  .'", ": .; :", "`.__.'", "", ""}},
    {'9', {" .--.", ": .; :", "`._, :", "   : :", "   :_:", "", ""}},
    {':', {"", " _", ":_:", " _", ":_;", "", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockGreekGlyphs[] = {
    {'0', {"", "  ___", " / _ \\", "| | | |", "| | | |", "| |_| |", " \\___/", "", ""}},
    {'1', {"", " _", "/ |", "- |", "| |", "| |", "|_|", "", ""}},
    {'2', {"", " ____", "(___ \\", "  __) )", " / __/", "| |___", "|_____)", "", ""}},
    {'3', {"", " _____", "(__  /", "  / /", " (__ \\", " ___) )", "(____/", "", ""}},
    {'4', {"", "    _", "  /  |", " / o |_", "/__   _)", "   | |", "   |_|", "", ""}},
    {'5', {"", " ____", "|  __)", "| |__", "|___ \\", " ___) )", "(____/", "", ""}},
    {'6', {"", "   __", "  / /", " / /_", "| '_ \\", "| (_) )", " \\___/", "", ""}},
    {'7', {"", " _____", "(___  )", "  _/ /", " (  _)", " / /", "/_/", "", ""}},
    {'8', {"", "  ___", " /   \\", " \\ O /", " / _ \\", "( (_) )", " \\___/", "", ""}},
    {'9', {"", "  ___", " / _ \\", "( (_) |", " \\__, |", "   / /", "  /_/", "", ""}},
    {':', {"", "", "", " _", "(_)", "", "", "", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockLarry3DGlyphs[] = {
    {'0', {"   __", " /'__`\\", "/\\ \\/\\ \\", "\\ \\ \\ \\ \\", " \\ \\ \\_\\ \\", "  \\ \\____/", "   \\/___/", "", ""}},
    {'1', {"   _", " /' \\", "/\\_, \\", "\\/_/\\ \\", "   \\ \\ \\", "    \\ \\_\\", "     \\/_/", "", ""}},
    {'2', {"   ___", " /'___`\\", "/\\_\\ /\\ \\", "\\/_/// /__", "   // /_\\ \\", "  /\\______/", "  \\/_____/", "", ""}},
    {'3', {"   __", " /'__`\\", "/\\_\\L\\ \\", "\\/_/_\\_<_", "  /\\ \\L\\ \\", "  \\ \\____/", "   \\/___/", "", ""}},
    {'4', {" __ __", "/\\ \\\\ \\", "\\ \\ \\\\ \\", " \\ \\ \\\\ \\_", "  \\ \\__ ,__\\", "   \\/_/\\_\\_/", "      \\/_/", "", ""}},
    {'5', {" ______", "/\\  ___\\", "\\ \\ \\__/", " \\ \\___``\\", "  \\/\\ \\L\\ \\", "   \\ \\____/", "    \\/___/", "", ""}},
    {'6', {"  ____", " /'___\\", "/\\ \\__/", "\\ \\  _``\\", " \\ \\ \\L\\ \\", "  \\ \\____/", "   \\/___/", "", ""}},
    {'7', {" ________", "/\\_____  \\", "\\/___//'/'", "    /' /'", "  /' /'", " /\\_/", " \\//", "", ""}},
    {'8', {"   __", " /'_ `\\", "/\\ \\L\\ \\", "\\/_> _ <_", "  /\\ \\L\\ \\", "  \\ \\____/", "   \\/___/", "", ""}},
    {'9', {"   __", " /'_ `\\", "/\\ \\L\\ \\", "\\ \\___, \\", " \\/__,/\\ \\", "      \\ \\_\\", "       \\/_/", "", ""}},
    {':', {"", "", " __", "/\\_\\", "\\/_/_", "  /\\_\\", "  \\/_/", "", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockLCDGlyphs[] = {
    {'0', {" ___", "|  /|", "| + |", "|/  |", " ---", ""}},
    {'1', {" _", "  |", "  +", "  |", " ---", ""}},
    {'2', {" ___", "    |", " -+-", "|", " ---", ""}},
    {'3', {" ___", "    |", " -+-", "    |", " ---", ""}},
    {'4', {"", "| |", " -+-", "  |", "", ""}},
    {'5', {" ___", "|", " -+-", "    |", " ---", ""}},
    {'6', {" ___", "|", "|-+-", "|   |", " ---", ""}},
    {'7', {" ___", "   /", "  +", " /", "", ""}},
    {'8', {" ___", "|   |", " -+-", "|   |", " ---", ""}},
    {'9', {" ___", "|   |", " -+-|", "    |", " ---", ""}},
    {':', {"", "  |", "", "  |", "", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockLeanGlyphs[] = {
    {'0', {"", "     _/", "  _/  _/", " _/  _/", "_/  _/", " _/", "", ""}},
    {'1', {"", "    _/", " _/_/", "  _/", " _/", "_/", "", ""}},
    {'2', {"", "      _/_/", "   _/    _/", "      _/", "   _/", "_/_/_/_/", "", ""}},
    {'3', {"", "    _/_/_/", "         _/", "    _/_/", "       _/", "_/_/_/", "", ""}},
    {'4', {"", "  _/  _/", " _/  _/", "_/_/_/_/", "   _/", "  _/", "", ""}},
    {'5', {"", "    _/_/_/_/", "   _/", "  _/_/_/", "       _/", "_/_/_/", "", ""}},
    {'6', {"", "     _/_/_/", "  _/", " _/_/_/", "_/    _/", " _/_/", "", ""}},
    {'7', {"", "  _/_/_/_/_/", "         _/", "      _/", "   _/", "_/", "", ""}},
    {'8', {"", "     _/_/", "  _/    _/", "   _/_/", "_/    _/", " _/_/", "", ""}},
    {'9', {"", "      _/_/", "   _/    _/", "    _/_/_/", "       _/", "_/_/_/", "", ""}},
    {':', {"", "", "   _/", "", "", "_/", "", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockNTGreekGlyphs[] = {
    {'0', {"", "  ___", " / _ \\", "| | | |", "| | | |", "| |_| |", " \\___/", "", ""}},
    {'1', {"", " _", "/ |", "- |", "| |", "| |", "|_|", "", ""}},
    {'2', {"", " ____", "(___ \\", "  __) )", " / __/", "| |___", "|_____)", "", ""}},
    {'3', {"", " _____", "(__  /", "  / /", " (__ \\", " ___) )", "(____/", "", ""}},
    {'4', {"", "    _", "  /  |", " / o |_", "/__   _)", "   | |", "   |_|", "", ""}},
    {'5', {"", " ____", "|  __)", "| |__", "|___ \\", " ___) )", "(____/", "", ""}},
    {'6', {"", "   __", "  / /", " / /_", "| '_ \\", "| (_) )", " \\___/", "", ""}},
    {'7', {"", " _____", "(___  )", "  _/ /", " (  _)", " / /", "/_/", "", ""}},
    {'8', {"", "  ___", " /   \\", " \\ O /", " / _ \\", "( (_) )", " \\___/", "", ""}},
    {'9', {"", "  ___", " / _ \\", "( (_) |", " \\__, |", "   / /", "  /_/", "", ""}},
    {':', {"", "", "", " _", "(_)", "", "", "", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockPuffyGlyphs[] = {
    {'0', {"  __", "/' _`\\", "| ( ) |", "| | | |", "| (_) |", "`\\___/'", "", ""}},
    {'1', {"   _", " /' )", "(_, |", "  | |", "  | |", "  (_)", "", ""}},
    {'2', {"   __", " /'__`\\", "(_)  ) )", "   /' /", " /' /( )", "(_____/'", "", ""}},
    {'3', {"   ___", " /'_  )", "(_)_) |", " _(_ <", "( )_) |", "`\\____)", "", ""}},
    {'4', {" _  _", "( )( )", "| || |", "| || |_", "(__ ,__)", "   (_)", "", ""}},
    {'5', {" _____", "(  ___)", "| (__", "|___ `\\", "( )_) |", "`\\___/'", "", ""}},
    {'6', {" _____", "(  ___)", "| (__", "|  _ `\\", "| (_) |", "`\\___/'", "", ""}},
    {'7', {" _______", "(_____  )", "     /'/'", "   /'/'", " /'/'", "(_/", "", ""}},
    {'8', {"   _", " /'_`\\", "( (_) )", " > _ <'", "( (_) )", "`\\___/'", "", ""}},
    {'9', {"   __", " /'_ `\\", "( (_) |", " \\__, |", "    | |", "    (_)", "", ""}},
    {':', {"", "", " _", "(_)", " _", "(_)", "", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockRammsteinGlyphs[] = {
    {'0', {"", " _____", "/     |", "|  /  |", "|_____/", "", ""}},
    {'1', {"", " _____", "|_    |", " |    |", " |____|", "", ""}},
    {'2', {"", " ______", "|____  |", "|    --|", "|______|", "", ""}},
    {'3', {"", " ______", "|___   |", "|___   |", "|______|", "", ""}},
    {'4', {"", " __   _", "|  | | |", "|  |_| |", "'----__|", "", ""}},
    {'5', {"", " ______", "|  ____|", "|___   \\", "|______/", "", ""}},
    {'6', {"", "  ____", " /   /_", "|   _  |", "|______|", "", ""}},
    {'7', {"", " ______", "|___   |", "  /   /", " |___|", "", ""}},
    {'8', {"", " _____", "<  -  >", "/  _  \\", "\\_____/", "", ""}},
    {'9', {"", " ______", "|   _  |", "|____  |", "    |__|", "", ""}},
    {':', {"", " _", "|_|", " _", "|_|", "", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockStopGlyphs[] = {
    {'0', {"  ______", " / __   |", "| | //| |", "| |// | |", "|  /__| |", " \\_____/", ""}},
    {'1', {"  __", " /  |", "/_/ |", "  | |", "  | |", "  |_|", ""}},
    {'2', {" ______", "(_____ \\", "  ____) )", " /_____/", " _______", "(_______)", ""}},
    {'3', {" ________", "(_______/", "   ____", "  (___ \\", " _____) )", "(______/", ""}},
    {'4', {"   __", "  / /", " / /____", "|___   _)", "    | |", "    |_|", ""}},
    {'5', {" _______", "(_______)", " ______", "(_____ \\", " _____) )", "(______/", ""}},
    {'6', {"    __", "   / /", "  / /_", " / __ \\", "( (__) )", " \\____/", ""}},
    {'7', {" _______", "(_______)", "      _", "     / )", "    / /", "   (_/", ""}},
    {'8', {"  _____", " / ___ \\", "( (   ) )", " > > < <", "( (___) )", " \\_____/", ""}},
    {'9', {"  ____", " / __ \\", "( (__) )", " \\__  /", "   / /", "  /_/", ""}},
    {':', {"", "", " _", "(_)", " _", "(_)", ""}},
    {' ', {" "}},
};

static const AsciiClockGlyph kAsciiClockSwanGlyphs[] = {
    {'0', {"", "", " .-.", ":   :", "|   |", ":   ;", " `-'", "", ""}},
    {'1', {"", "", "  .", ".'|", "  |", "  |", "'---'", "", ""}},
    {'2', {"", "", " .-.", "(   )", "  .'", " /", "'---'", "", ""}},
    {'3', {"", "", ".--.", "    )", " --:", "    )", "`--'", "", ""}},
    {'4', {"", "", ".  .", "|  |", "'--|-", "   |", "   '", "", ""}},
    {'5', {"", "", ".---.", "|", "'--.", ".   )", " `-'", "", ""}},
    {'6', {"", "", "   ,", "  /", " /-.", "(   )", " `-'", "", ""}},
    {'7', {"", "", ".---.", "    /", "   /", "  /", " '", "", ""}},
    {'8', {"", "", " .-.", "(   )", " >-<", "(   )", " `-'", "", ""}},
    {'9', {"", "", " .-.", "(   )", " `-/", "  /", " '", "", ""}},
    {':', {"", "", "", "", "o", "", "o", "", ""}},
    {' ', {" "}},
};

static const AsciiClockFont kAsciiClockFonts[] = {
    {"Standard", 6, 1, kAsciiClockStandardGlyphs, (uint8_t)(sizeof(kAsciiClockStandardGlyphs) / sizeof(kAsciiClockStandardGlyphs[0]))},
    {"Bulbhead", 4, 1, kAsciiClockBulbheadGlyphs, (uint8_t)(sizeof(kAsciiClockBulbheadGlyphs) / sizeof(kAsciiClockBulbheadGlyphs[0]))},
    {"Doom", 8, 1, kAsciiClockDoomGlyphs, (uint8_t)(sizeof(kAsciiClockDoomGlyphs) / sizeof(kAsciiClockDoomGlyphs[0]))},
    {"Graceful", 4, 1, kAsciiClockGracefulGlyphs, (uint8_t)(sizeof(kAsciiClockGracefulGlyphs) / sizeof(kAsciiClockGracefulGlyphs[0]))},
    {"Ogre", 6, 1, kAsciiClockOgreGlyphs, (uint8_t)(sizeof(kAsciiClockOgreGlyphs) / sizeof(kAsciiClockOgreGlyphs[0]))},
    {"Small", 5, 1, kAsciiClockSmallGlyphs, (uint8_t)(sizeof(kAsciiClockSmallGlyphs) / sizeof(kAsciiClockSmallGlyphs[0]))},
    {"Soft", 7, 1, kAsciiClockSoftGlyphs, (uint8_t)(sizeof(kAsciiClockSoftGlyphs) / sizeof(kAsciiClockSoftGlyphs[0]))},
    {"3D-ASCII", 8, 1, kAsciiClock3DASCIIGlyphs, (uint8_t)(sizeof(kAsciiClock3DASCIIGlyphs) / sizeof(kAsciiClock3DASCIIGlyphs[0]))},
    {"Chunky", 5, 1, kAsciiClockChunkyGlyphs, (uint8_t)(sizeof(kAsciiClockChunkyGlyphs) / sizeof(kAsciiClockChunkyGlyphs[0]))},
    {"Cricket", 8, 1, kAsciiClockCricketGlyphs, (uint8_t)(sizeof(kAsciiClockCricketGlyphs) / sizeof(kAsciiClockCricketGlyphs[0]))},
    {"Fuzzy", 7, 1, kAsciiClockFuzzyGlyphs, (uint8_t)(sizeof(kAsciiClockFuzzyGlyphs) / sizeof(kAsciiClockFuzzyGlyphs[0]))},
    {"Greek", 9, 1, kAsciiClockGreekGlyphs, (uint8_t)(sizeof(kAsciiClockGreekGlyphs) / sizeof(kAsciiClockGreekGlyphs[0]))},
    {"Larry 3D", 9, 0, kAsciiClockLarry3DGlyphs, (uint8_t)(sizeof(kAsciiClockLarry3DGlyphs) / sizeof(kAsciiClockLarry3DGlyphs[0]))},
    {"LCD", 6, 1, kAsciiClockLCDGlyphs, (uint8_t)(sizeof(kAsciiClockLCDGlyphs) / sizeof(kAsciiClockLCDGlyphs[0]))},
    {"Lean", 8, 0, kAsciiClockLeanGlyphs, (uint8_t)(sizeof(kAsciiClockLeanGlyphs) / sizeof(kAsciiClockLeanGlyphs[0]))},
    {"NT Greek", 9, 1, kAsciiClockNTGreekGlyphs, (uint8_t)(sizeof(kAsciiClockNTGreekGlyphs) / sizeof(kAsciiClockNTGreekGlyphs[0]))},
    {"Puffy", 8, 1, kAsciiClockPuffyGlyphs, (uint8_t)(sizeof(kAsciiClockPuffyGlyphs) / sizeof(kAsciiClockPuffyGlyphs[0]))},
    {"Rammstein", 7, 1, kAsciiClockRammsteinGlyphs, (uint8_t)(sizeof(kAsciiClockRammsteinGlyphs) / sizeof(kAsciiClockRammsteinGlyphs[0]))},
    {"Stop", 7, 1, kAsciiClockStopGlyphs, (uint8_t)(sizeof(kAsciiClockStopGlyphs) / sizeof(kAsciiClockStopGlyphs[0]))},
    {"Swan", 9, 1, kAsciiClockSwanGlyphs, (uint8_t)(sizeof(kAsciiClockSwanGlyphs) / sizeof(kAsciiClockSwanGlyphs[0]))},
};
static constexpr int ASCII_CLOCK_FONT_COUNT = sizeof(kAsciiClockFonts) / sizeof(kAsciiClockFonts[0]);
static constexpr int DEFAULT_ASCII_CLOCK_FONT_INDEX = 0;
static const size_t kFishGlyphBuf = 28;

// Printable ASCII only (Font 2). Use ' instead of °, * instead of bullets, etc.
FishSpecies fishSpecies[] = {
    {/* Small Green */ "><>", RGB565(80, 200, 120)},                         // Emerald
    {/* Blue Dart */ ">)))'>", RGB565(0, 150, 255)},                         // Azure
    {/* Pink Bubble */ "oO0", TFT_PINK},                                      // Pink
    {/* Golden Emperor */ "><((( '>", RGB565(255, 184, 0)},                   // Amber
    {/* Purple Jellyfish */ "~~{o}", TFT_VIOLET},                              // Violet
    {/* Red Snapper */ "><(((o>", TFT_RED},                                   // Red
    {/* Orange Wrasse */ "><((((>`", TFT_ORANGE},
    {/* Teal Glider */ "><((( '>", RGB565(0, 180, 170)},
    {/* Royal Indigo */ "}>{{{{* >", RGB565(75, 0, 156)},                    // Indigo
    {/* Lilac Starfish */ "><((( *>", RGB565(200, 120, 255)},
    {/* Pink Tetra */ ">(')>", RGB565(255, 158, 200)},
    {/* Yellow Minnow */ ">'>", TFT_YELLOW},
};
static constexpr int GLYPH_COUNT = sizeof(fishSpecies) / sizeof(fishSpecies[0]);

static char fishMirroredLeft[GLYPH_COUNT][kFishGlyphBuf];
static uint8_t fishGlyphLenRight[GLYPH_COUNT];
static uint8_t fishGlyphLenLeft[GLYPH_COUNT];
static int16_t fishGlyphWidthRight[GLYPH_COUNT];
static int16_t fishGlyphWidthLeft[GLYPH_COUNT];
static int16_t fishGlyphOffsetRight[GLYPH_COUNT][kFishGlyphBuf];
static int16_t fishGlyphOffsetLeft[GLYPH_COUNT][kFishGlyphBuf];

// ===== part: mirror_funcs =====
static char mirrorAsciiBracket(char c) {
  switch (c) {
    case '>':
      return '<';
    case '<':
      return '>';
    case '(':
      return ')';
    case ')':
      return '(';
    case '{':
      return '}';
    case '}':
      return '{';
    case '[':
      return ']';
    case ']':
      return '[';
    default:
      return c;
  }
}

static bool buildMirroredGlyph(const char* right, char* leftOut, size_t outCap) {
  size_t n = strlen(right);
  if (n == 0 || n + 1 > outCap) return false;
  for (size_t i = 0; i < n; ++i) {
    unsigned char u = static_cast<unsigned char>(right[i]);
    if (u < 32 || u > 126) return false;
    leftOut[i] = mirrorAsciiBracket(right[n - 1 - i]);
  }
  leftOut[n] = '\0';
  return true;
}

static void initFishMirrors() {
  for (int i = 0; i < GLYPH_COUNT; ++i) {
    if (!buildMirroredGlyph(fishSpecies[i].right, fishMirroredLeft[i], kFishGlyphBuf)) {
      strncpy(fishMirroredLeft[i], fishSpecies[i].right, kFishGlyphBuf - 1);
      fishMirroredLeft[i][kFishGlyphBuf - 1] = '\0';
    }
  }
}

inline const char* fishGlyphDrawing(const Fish& f) {
  return (f.vx >= 0.0f) ? fishSpecies[f.type].right : fishMirroredLeft[f.type];
}

// Occasional non-canonical hue for variety (~1 in 5 fish)
static const uint16_t kAltFishColors[] = {
    TFT_CYAN, TFT_MAGENTA, TFT_WHITE, TFT_SKYBLUE, TFT_GOLD,
    TFT_ORANGE, TFT_GREENYELLOW, TFT_DARKGREY};
static const int kAltFishColorCount = sizeof(kAltFishColors) / sizeof(kAltFishColors[0]);


// ===== part: new_globals =====
// ------------------------------ Globals --------------------------------------
TFT_eSPI tft = TFT_eSPI();

// Double-buffered rendering: the main loop (core 1) draws a frame into one
// sprite while a dedicated task on core 0 concurrently pushes the other
// sprite out over SPI, so SPI transfer time overlaps with the next frame's
// physics/drawing instead of blocking it. Both land in PSRAM automatically
// (TFT_eSprite prefers PSRAM whenever psramFound() is true), so this costs
// PSRAM, not the scarce internal SRAM.
TFT_eSprite canvas = TFT_eSprite(&tft);
TFT_eSprite canvas2 = TFT_eSprite(&tft);
static TFT_eSprite* const frameBuffers[2] = {&canvas, &canvas2};
static SemaphoreHandle_t bufferFreeSem[2] = {nullptr, nullptr};
static SemaphoreHandle_t pushRequestSem = nullptr;
static volatile int pushBufferIndex = 0;
static int drawBufferIndex = 0;
static TaskHandle_t displayTaskHandle = nullptr;

Preferences prefs;

Fish fishPool[MAX_FISH_POOL];
Flake flakes[MAX_FLAKES];
Bubble bubbles[MAX_BUBBLES];
Octopus octopus;
Seahorse seahorse;
Snail snail;
Jellyfish jellyfish;

int fishTargetCount = DEFAULT_FISH;
int bubbleTargetCount = DEFAULT_BUBBLES;
int octopusFrequency = DEFAULT_OCTOPUS_FREQUENCY;
int seahorseFrequency = DEFAULT_SEAHORSE_FREQUENCY;
int autoFeedFrequency = DEFAULT_AUTO_FEED_FREQUENCY;
int snailFrequency = DEFAULT_SNAIL_FREQUENCY;
int jellyfishFrequency = DEFAULT_JELLYFISH_FREQUENCY;
unsigned long cthulhuFeedPelletCount = 0;
unsigned long nextAutoFeedMs = 0;
bool autoFeedSprinkleActive = false;
bool autoFeedSprinkleLeftToRight = true;
int autoFeedSprinkleDropped = 0;
unsigned long autoFeedSprinkleNextMs = 0;
unsigned long autoFeedSprinkleIntervalMs = 220UL;
float seaweedSwaySpeed = DEFAULT_SWAY;
float seaweedLength = DEFAULT_SEAWEED_LENGTH;
float seaweedLengthRandomness = DEFAULT_SEAWEED_LENGTH_RANDOMNESS;

BackgroundStyle backgroundStyle = DEFAULT_BACKGROUND_STYLE;
uint16_t backgroundGradientColor = DEFAULT_BACKGROUND_GRADIENT_COLOR;
bool backgroundRainbowEnabled = false;
uint16_t backgroundRainbowColor = DEFAULT_BACKGROUND_GRADIENT_COLOR;
bool autoSkyEnabled = false;
uint16_t autoSkySunriseColor = DEFAULT_AUTO_SKY_SUNRISE_COLOR;
uint16_t autoSkyDayColor = DEFAULT_AUTO_SKY_DAY_COLOR;
uint16_t autoSkySunsetColor = DEFAULT_AUTO_SKY_SUNSET_COLOR;
uint16_t autoSkyNightColor = DEFAULT_AUTO_SKY_NIGHT_COLOR;
uint16_t autoSkyGradientColor = DEFAULT_AUTO_SKY_DAY_COLOR;
unsigned long lastBackgroundRainbowUpdateMs = 0;
AutoSkySlot activeAutoSkyColorSlot = AUTO_SKY_SLOT_NONE;

static const int MIN_LCD_BRIGHTNESS = 10;
static const int MAX_LCD_BRIGHTNESS = 100;
static const int DEFAULT_LCD_BRIGHTNESS = 100;
static const int LCD_BRIGHTNESS_STEP = 5;
int lcdBrightness = DEFAULT_LCD_BRIGHTNESS;

void applyLcdBrightness() {
  ledcWrite(TFT_BL, (uint32_t)((lcdBrightness * 255) / 100));
}

bool clockVisible = false;
bool clockUse24Hour = false;
bool clockUseInternetTime = false;
ClockDisplayStyle clockDisplayStyle = CLOCK_STYLE_SMALL_TEXT;
ClockSmallPosition clockSmallPosition = CLOCK_SMALL_TOP;
int asciiClockFontIndex = DEFAULT_ASCII_CLOCK_FONT_INDEX;
uint16_t clockSmallTextColor = DEFAULT_SMALL_CLOCK_COLOR;
uint16_t clockAsciiTextColor = DEFAULT_ASCII_CLOCK_COLOR;
bool clockSmallRainbowEnabled = false;
bool clockAsciiRainbowEnabled = false;
int timezoneIndex = DEFAULT_TIMEZONE_INDEX;
int clockYear = DEFAULT_CLOCK_YEAR;
int clockMonth = DEFAULT_CLOCK_MONTH;
int clockDay = DEFAULT_CLOCK_DAY;
int clockHour = DEFAULT_CLOCK_HOUR;
int clockMinute = DEFAULT_CLOCK_MINUTE;
unsigned long clockLastMinuteMs = 0;
ClockField activeClockField = CLOCK_FIELD_HOUR;

bool wifiPanelOpen = false;  // true while a menu WiFi screen is on-screen; throttles serviceWifi()
bool wifiEnabled = false;
bool wifiRadioStarted = false;
bool wifiScanInProgress = false;
bool wifiConnecting = false;
bool wifiConnected = false;
bool wifiConnectionFailed = false;
bool wifiSavePendingCredentials = false;
bool wifiTimeConfigured = false;
bool wifiTimeSynced = false;
unsigned long lastWifiServiceMs = 0;
char wifiStatusText[40] = "Off";
char wifiSsid[WIFI_SSID_MAX_LEN + 1] = "";
char wifiPass[WIFI_PASS_MAX_LEN + 1] = "";
char pendingWifiSsid[WIFI_SSID_MAX_LEN + 1] = "";
char pendingWifiPass[WIFI_PASS_MAX_LEN + 1] = "";
char wifiNetworkNames[MAX_WIFI_NETWORKS][WIFI_SSID_MAX_LEN + 1];
int wifiNetworkRssi[MAX_WIFI_NETWORKS];
bool wifiNetworkOpen[MAX_WIFI_NETWORKS];
int wifiNetworkCount = 0;
int wifiNetworkPage = 0;
unsigned long wifiConnectStartMs = 0;
unsigned long wifiLastReconnectMs = 0;
unsigned long wifiLastNtpAttemptMs = 0;
unsigned long wifiLastNtpSyncMs = 0;

unsigned long lastMs = 0;
unsigned long aquariumNowMs = 0;
unsigned long fpsTimer = 0;
unsigned long frameCount = 0;
float fps = 0.0f;

bool spriteReady = false;
int mainCanvasActualColorDepth = 0;
int mainCanvasRenderHeight = SCREEN_H;
uint16_t* gradientBandCache = nullptr;
BackgroundStyle gradientBandCacheStyle = BACKGROUND_STYLE_COUNT;
uint16_t gradientBandCacheColor = 0;

bool settingsDirty = false;
unsigned long settingsDirtyMs = 0;
unsigned long lastSettingsSaveMs = 0;
static const unsigned long SETTINGS_SAVE_DELAY_MS = 1200UL;
static const unsigned long CLOCK_AUTOSAVE_INTERVAL_MS = 300000UL;

// ===== part: glyph_metrics_funcs =====
void cacheGlyphMetrics(const char* txt, uint8_t& lenOut, int16_t& widthOut, int16_t offsetsOut[]) {
  char prefix[kFishGlyphBuf];
  size_t len = strlen(txt);
  if (len >= kFishGlyphBuf) len = kFishGlyphBuf - 1;

  for (size_t c = 0; c < len; ++c) {
    memcpy(prefix, txt, c);
    prefix[c] = '\0';
    offsetsOut[c] = (int16_t)tft.textWidth(prefix);
  }

  lenOut = (uint8_t)len;
  widthOut = (int16_t)tft.textWidth(txt);
}

void initFishGlyphMetrics() {
  for (int i = 0; i < GLYPH_COUNT; ++i) {
    cacheGlyphMetrics(fishSpecies[i].right, fishGlyphLenRight[i], fishGlyphWidthRight[i], fishGlyphOffsetRight[i]);
    cacheGlyphMetrics(fishMirroredLeft[i], fishGlyphLenLeft[i], fishGlyphWidthLeft[i], fishGlyphOffsetLeft[i]);
  }
}

// ===== part: gradient_dither_consts =====
enum GradientDitherPattern {
  GRADIENT_DITHER_ORDERED,
  GRADIENT_DITHER_CHECKER,
  GRADIENT_DITHER_CHECKER_LOCKED,
  GRADIENT_DITHER_NOISE,
  GRADIENT_DITHER_NONE
};

static const uint8_t kGradientStops[] = {0, 18, 42, 74, 112, 156, 204, 232, 255};
static const uint8_t kGradientBrightness[] = {255, 228, 198, 164, 126, 90, 58, 30, 0};
static constexpr int kGradientStopCount = sizeof(kGradientStops) / sizeof(kGradientStops[0]);
static uint16_t activeGradientColors[kGradientStopCount];

// ===== part: pixelflower_decls =====
struct PixelFlowerSpec {
  int cx;
  int cy;
  int radius;
  float rotation;
  uint16_t color;
};

struct PixelFlowerPoint {
  int16_t x;
  int16_t y;
};

static const PixelFlowerSpec kDefaultPixelFlowers[] = {
    {70, 70, 58, 6.28318f, RGB565(0, 26, 76)},
    {248, 72, 58, 6.82318f, RGB565(0, 22, 66)},
    {202, 156, 28, 3.14159f, RGB565(116, 108, 18)},
};
static PixelFlowerSpec pixelFlowers[sizeof(kDefaultPixelFlowers) / sizeof(kDefaultPixelFlowers[0])];
static constexpr int kPixelFlowerCount = sizeof(pixelFlowers) / sizeof(pixelFlowers[0]);
static const int PIXEL_FLOWER_SEGMENTS = 80;
static PixelFlowerPoint pixelFlowerPoints[kPixelFlowerCount][PIXEL_FLOWER_SEGMENTS + 1];
static bool pixelFlowerGeometryDirty = true;

// ===== part: new_render_helpers =====
// ------------------------------ Render helpers --------------------------------
// The ESP32-S3-N8R2 always has PSRAM, so (unlike the original CYD build) the
// full 320x240x16bpp canvas always fits in one allocation - no strip-render
// fallback is needed.
void applyRenderViewport(TFT_eSprite& s) {
  s.resetViewport();
}

void clearRenderSurface(TFT_eSprite& s) {
  s.fillSprite(BG_COLOR);
}

bool allocateMainCanvas() {
  canvas.setColorDepth(MAIN_SPRITE_COLOR_DEPTH);
  if (canvas.createSprite(SCREEN_W, SCREEN_H) == nullptr) return false;
  canvas2.setColorDepth(MAIN_SPRITE_COLOR_DEPTH);
  if (canvas2.createSprite(SCREEN_W, SCREEN_H) == nullptr) return false;
  mainCanvasActualColorDepth = MAIN_SPRITE_COLOR_DEPTH;
  mainCanvasRenderHeight = SCREEN_H;
  return true;
}

// ===== part: utility_kept =====
template <typename T>
T clampVal(T v, T lo, T hi) {
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}

void copySafe(char* out, size_t outCap, const char* src) {
  if (outCap == 0) return;
  if (!src) src = "";
  strncpy(out, src, outCap - 1);
  out[outCap - 1] = '\0';
}

void appendCharSafe(char* out, size_t outCap, char c) {
  size_t len = strlen(out);
  if (len + 1 >= outCap) return;
  out[len] = c;
  out[len + 1] = '\0';
}

void removeLastChar(char* out) {
  size_t len = strlen(out);
  if (len > 0) out[len - 1] = '\0';
}

void formatShortText(char* out, size_t outCap, const char* src, size_t maxChars) {
  if (outCap == 0) return;
  if (!src) src = "";
  if (outCap < 4) {
    copySafe(out, outCap, src);
    return;
  }
  size_t len = strlen(src);
  if (len <= maxChars || maxChars < 4) {
    copySafe(out, outCap, src);
    return;
  }

  size_t copyLen = maxChars - 3;
  if (copyLen > outCap - 4) copyLen = outCap - 4;
  strncpy(out, src, copyLen);
  out[copyLen] = '\0';
  strncat(out, "...", outCap - strlen(out) - 1);
}

void markSettingsDirty() {
  settingsDirty = true;
  settingsDirtyMs = millis();
}

void invalidateBackgroundGradientCache() {
  gradientBandCacheStyle = BACKGROUND_STYLE_COUNT;
}

void releaseGradientBandCache() {
  if (gradientBandCache == nullptr) return;
  free(gradientBandCache);
  gradientBandCache = nullptr;
  invalidateBackgroundGradientCache();
}

float aquariumTimeSec() {
  return aquariumNowMs * 0.001f;
}

static inline float wrappedSinf(float radians) {
  static const float TWO_PI_F = 6.28318530718f;
  // Keep long-running wave animations fast: large sine inputs get slower on ESP32 math libs.
  if (radians > TWO_PI_F || radians < -TWO_PI_F) {
    radians = fmodf(radians, TWO_PI_F);
  }
  return sinf(radians);
}

float frand(float a, float b) {
  return a + (b - a) * (float)random(0, 10000) / 9999.0f;
}

int fishVisualWidth(const Fish& f) {
  if (f.visualWidth > 0) return f.visualWidth;
  return (int)strlen(fishGlyphDrawing(f)) * 12;  // Font 2 fallback before metrics init
}

int normalizeOctopusFrequency(int value) {
  int best = OCTOPUS_FREQUENCY_OPTIONS[0];
  int bestDiff = abs(value - best);
  for (int i = 1; i < OCTOPUS_FREQUENCY_OPTION_COUNT; ++i) {
    int diff = abs(value - OCTOPUS_FREQUENCY_OPTIONS[i]);
    if (diff < bestDiff) {
      best = OCTOPUS_FREQUENCY_OPTIONS[i];
      bestDiff = diff;
    }
  }
  return best;
}

int normalizeSeahorseFrequency(int value) {
  int best = SEAHORSE_FREQUENCY_OPTIONS[0];
  int bestDiff = abs(value - best);
  for (int i = 1; i < SEAHORSE_FREQUENCY_OPTION_COUNT; ++i) {
    int diff = abs(value - SEAHORSE_FREQUENCY_OPTIONS[i]);
    if (diff < bestDiff) {
      best = SEAHORSE_FREQUENCY_OPTIONS[i];
      bestDiff = diff;
    }
  }
  return best;
}

int normalizeAutoFeedFrequency(int value) {
  int best = AUTO_FEED_FREQUENCY_OPTIONS[0];
  int bestDiff = abs(value - best);
  for (int i = 1; i < AUTO_FEED_FREQUENCY_OPTION_COUNT; ++i) {
    int diff = abs(value - AUTO_FEED_FREQUENCY_OPTIONS[i]);
    if (diff < bestDiff) {
      best = AUTO_FEED_FREQUENCY_OPTIONS[i];
      bestDiff = diff;
    }
  }
  return best;
}

int normalizeCreatureFrequency(int value) {
  int best = CREATURE_FREQUENCY_OPTIONS[0];
  int bestDiff = abs(value - best);
  for (int i = 1; i < CREATURE_FREQUENCY_OPTION_COUNT; ++i) {
    int diff = abs(value - CREATURE_FREQUENCY_OPTIONS[i]);
    if (diff < bestDiff) {
      best = CREATURE_FREQUENCY_OPTIONS[i];
      bestDiff = diff;
    }
  }
  return best;
}

void cycleOctopusFrequency(int delta) {
  int current = normalizeOctopusFrequency(octopusFrequency);
  int index = 0;
  for (int i = 0; i < OCTOPUS_FREQUENCY_OPTION_COUNT; ++i) {
    if (OCTOPUS_FREQUENCY_OPTIONS[i] == current) {
      index = i;
      break;
    }
  }

  index += delta;
  if (index < 0) index = OCTOPUS_FREQUENCY_OPTION_COUNT - 1;
  if (index >= OCTOPUS_FREQUENCY_OPTION_COUNT) index = 0;
  octopusFrequency = OCTOPUS_FREQUENCY_OPTIONS[index];
  if (!octopus.active) octopus.nextSpawnMs = 0;
  markSettingsDirty();
}

void cycleSeahorseFrequency(int delta) {
  int current = normalizeSeahorseFrequency(seahorseFrequency);
  int index = 0;
  for (int i = 0; i < SEAHORSE_FREQUENCY_OPTION_COUNT; ++i) {
    if (SEAHORSE_FREQUENCY_OPTIONS[i] == current) {
      index = i;
      break;
    }
  }

  index += delta;
  if (index < 0) index = SEAHORSE_FREQUENCY_OPTION_COUNT - 1;
  if (index >= SEAHORSE_FREQUENCY_OPTION_COUNT) index = 0;
  seahorseFrequency = SEAHORSE_FREQUENCY_OPTIONS[index];
  if (!seahorse.active) seahorse.nextSpawnMs = 0;
  markSettingsDirty();
}

void cycleAutoFeedFrequency(int delta) {
  int current = normalizeAutoFeedFrequency(autoFeedFrequency);
  int index = 0;
  for (int i = 0; i < AUTO_FEED_FREQUENCY_OPTION_COUNT; ++i) {
    if (AUTO_FEED_FREQUENCY_OPTIONS[i] == current) {
      index = i;
      break;
    }
  }

  index += delta;
  if (index < 0) index = AUTO_FEED_FREQUENCY_OPTION_COUNT - 1;
  if (index >= AUTO_FEED_FREQUENCY_OPTION_COUNT) index = 0;
  autoFeedFrequency = AUTO_FEED_FREQUENCY_OPTIONS[index];
  if (autoFeedFrequency <= 0) {
    nextAutoFeedMs = 0;
    autoFeedSprinkleActive = false;
  } else {
    nextAutoFeedMs = aquariumNowMs + AUTO_FEED_FIRST_DROP_MS;
    autoFeedSprinkleActive = false;
  }
  markSettingsDirty();
}

void cycleSnailFrequency(int delta) {
  int current = normalizeCreatureFrequency(snailFrequency);
  int index = 0;
  for (int i = 0; i < CREATURE_FREQUENCY_OPTION_COUNT; ++i) {
    if (CREATURE_FREQUENCY_OPTIONS[i] == current) {
      index = i;
      break;
    }
  }

  index += delta;
  if (index < 0) index = CREATURE_FREQUENCY_OPTION_COUNT - 1;
  if (index >= CREATURE_FREQUENCY_OPTION_COUNT) index = 0;
  snailFrequency = CREATURE_FREQUENCY_OPTIONS[index];
  if (!snail.active) snail.nextSpawnMs = 0;
  markSettingsDirty();
}

void cycleJellyfishFrequency(int delta) {
  int current = normalizeCreatureFrequency(jellyfishFrequency);
  int index = 0;
  for (int i = 0; i < CREATURE_FREQUENCY_OPTION_COUNT; ++i) {
    if (CREATURE_FREQUENCY_OPTIONS[i] == current) {
      index = i;
      break;
    }
  }

  index += delta;
  if (index < 0) index = CREATURE_FREQUENCY_OPTION_COUNT - 1;
  if (index >= CREATURE_FREQUENCY_OPTION_COUNT) index = 0;
  jellyfishFrequency = CREATURE_FREQUENCY_OPTIONS[index];
  if (!jellyfish.active) jellyfish.nextSpawnMs = 0;
  markSettingsDirty();
}

bool isLeapYear(int year) {
  return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
  static const uint8_t daysByMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  month = clampVal(month, 1, 12);
  if (month == 2 && isLeapYear(year)) return 29;
  return daysByMonth[month - 1];
}

void normalizeClockDate() {
  clockYear = clampVal(clockYear, CLOCK_MIN_YEAR, CLOCK_MAX_YEAR);
  clockMonth = clampVal(clockMonth, 1, 12);
  clockDay = clampVal(clockDay, 1, daysInMonth(clockYear, clockMonth));
  clockHour = clampVal(clockHour, 0, 23);
  clockMinute = clampVal(clockMinute, 0, 59);
}

void incrementClockDay() {
  clockDay++;
  if (clockDay > daysInMonth(clockYear, clockMonth)) {
    clockDay = 1;
    clockMonth++;
    if (clockMonth > 12) {
      clockMonth = 1;
      if (clockYear < CLOCK_MAX_YEAR) clockYear++;
    }
  }
}

void decrementClockDay() {
  clockDay--;
  if (clockDay < 1) {
    clockMonth--;
    if (clockMonth < 1) {
      clockMonth = 12;
      if (clockYear > CLOCK_MIN_YEAR) clockYear--;
    }
    clockDay = daysInMonth(clockYear, clockMonth);
  }
}

void addClockMinute(int delta) {
  if (delta > 0) {
    clockMinute++;
    if (clockMinute >= 60) {
      clockMinute = 0;
      clockHour++;
      if (clockHour >= 24) {
        clockHour = 0;
        incrementClockDay();
      }
    }
  } else if (delta < 0) {
    clockMinute--;
    if (clockMinute < 0) {
      clockMinute = 59;
      clockHour--;
      if (clockHour < 0) {
        clockHour = 23;
        decrementClockDay();
      }
    }
  }
}

void updateClock(unsigned long now) {
  while (now - clockLastMinuteMs >= 60000UL) {
    clockLastMinuteMs += 60000UL;
    addClockMinute(1);
  }
}

void resetClockTick() {
  clockLastMinuteMs = millis();
}

void selectClockField(int delta) {
  int next = (int)activeClockField + delta;
  if (next < 0) next = CLOCK_FIELD_COUNT - 1;
  if (next >= CLOCK_FIELD_COUNT) next = 0;
  activeClockField = (ClockField)next;
}

void adjustClockField(int delta) {
  switch (activeClockField) {
    case CLOCK_FIELD_YEAR:
      clockYear = clampVal(clockYear + delta, CLOCK_MIN_YEAR, CLOCK_MAX_YEAR);
      break;
    case CLOCK_FIELD_MONTH:
      clockMonth += delta;
      if (clockMonth < 1) clockMonth = 12;
      if (clockMonth > 12) clockMonth = 1;
      break;
    case CLOCK_FIELD_DAY:
      clockDay += delta;
      if (clockDay < 1) clockDay = daysInMonth(clockYear, clockMonth);
      if (clockDay > daysInMonth(clockYear, clockMonth)) clockDay = 1;
      break;
    case CLOCK_FIELD_HOUR:
      clockHour += delta;
      if (clockHour < 0) clockHour = 23;
      if (clockHour > 23) clockHour = 0;
      break;
    case CLOCK_FIELD_MINUTE:
      clockMinute += delta;
      if (clockMinute < 0) clockMinute = 59;
      if (clockMinute > 59) clockMinute = 0;
      break;
    default:
      break;
  }
  normalizeClockDate();
  resetClockTick();
  markSettingsDirty();
}

void formatClockFieldValue(char* out, size_t outCap) {
  switch (activeClockField) {
    case CLOCK_FIELD_YEAR:
      snprintf(out, outCap, "%d", clockYear);
      break;
    case CLOCK_FIELD_MONTH:
      snprintf(out, outCap, "%02d", clockMonth);
      break;
    case CLOCK_FIELD_DAY:
      snprintf(out, outCap, "%02d", clockDay);
      break;
    case CLOCK_FIELD_HOUR:
      if (clockUse24Hour) {
        snprintf(out, outCap, "%02d", clockHour);
      } else {
        int h = clockHour % 12;
        if (h == 0) h = 12;
        snprintf(out, outCap, "%d %s", h, clockHour < 12 ? "AM" : "PM");
      }
      break;
    case CLOCK_FIELD_MINUTE:
      snprintf(out, outCap, "%02d", clockMinute);
      break;
    default:
      snprintf(out, outCap, "--");
      break;
  }
}

void formatClockDisplay(char* out, size_t outCap) {
  static const char* monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (clockUse24Hour) {
    snprintf(out, outCap, "%s %02d  %02d:%02d", monthNames[clockMonth - 1], clockDay, clockHour, clockMinute);
  } else {
    int h = clockHour % 12;
    if (h == 0) h = 12;
    snprintf(out, outCap, "%s %02d  %d:%02d %s", monthNames[clockMonth - 1], clockDay, h, clockMinute,
             clockHour < 12 ? "AM" : "PM");
  }
}

void formatClockTimeOnly(char* out, size_t outCap, bool includeMeridiem) {
  if (clockUse24Hour) {
    snprintf(out, outCap, "%02d:%02d", clockHour, clockMinute);
  } else {
    int h = clockHour % 12;
    if (h == 0) h = 12;
    if (includeMeridiem) {
      snprintf(out, outCap, "%d:%02d %s", h, clockMinute, clockHour < 12 ? "am" : "pm");
    } else {
      snprintf(out, outCap, "%d:%02d", h, clockMinute);
    }
  }
}

void adjustAsciiClockFont(int delta) {
  asciiClockFontIndex += delta;
  while (asciiClockFontIndex < 0) asciiClockFontIndex += ASCII_CLOCK_FONT_COUNT;
  while (asciiClockFontIndex >= ASCII_CLOCK_FONT_COUNT) asciiClockFontIndex -= ASCII_CLOCK_FONT_COUNT;
  markSettingsDirty();
}

const AsciiClockGlyph& asciiClockGlyphFor(const AsciiClockFont& font, char c) {
  for (int i = 0; i < font.glyphCount; ++i) {
    if (font.glyphs[i].c == c) return font.glyphs[i];
  }
  return font.glyphs[font.glyphCount - 1];  // Space
}

int asciiClockGlyphWidth(const AsciiClockFont& font, const AsciiClockGlyph& glyph) {
  int width = 1;
  for (int row = 0; row < font.rowCount; ++row) {
    const char* glyphRow = glyph.rows[row] ? glyph.rows[row] : "";
    int rowWidth = strlen(glyphRow);
    if (rowWidth > width) width = rowWidth;
  }
  return width;
}

int asciiClockTextCols(const char* text, const AsciiClockFont& font) {
  int cols = 0;
  for (size_t i = 0; text[i] != '\0'; ++i) {
    if (i > 0) cols += font.glyphGap;
    cols += asciiClockGlyphWidth(font, asciiClockGlyphFor(font, text[i]));
  }
  return cols;
}

void appendAsciiClockGlyphRow(char* out, size_t outCap, const AsciiClockFont& font, const AsciiClockGlyph& glyph,
                              int row) {
  int width = asciiClockGlyphWidth(font, glyph);
  const char* glyphRow = glyph.rows[row] ? glyph.rows[row] : "";
  int rowLen = strlen(glyphRow);
  for (int col = 0; col < width; ++col) {
    appendCharSafe(out, outCap, col < rowLen ? glyphRow[col] : ' ');
  }
}

void trimTrailingSpaces(char* out) {
  int len = strlen(out);
  while (len > 0 && out[len - 1] == ' ') {
    out[--len] = '\0';
  }
}

const AsciiClockFont& currentAsciiClockFont() {
  asciiClockFontIndex = clampVal(asciiClockFontIndex, 0, ASCII_CLOCK_FONT_COUNT - 1);
  return kAsciiClockFonts[asciiClockFontIndex];
}

const char* clockFieldName() {
  switch (activeClockField) {
    case CLOCK_FIELD_YEAR:
      return "Year";
    case CLOCK_FIELD_MONTH:
      return "Month";
    case CLOCK_FIELD_DAY:
      return "Day";
    case CLOCK_FIELD_HOUR:
      return "Hour";
    case CLOCK_FIELD_MINUTE:
      return "Minute";
    default:
      return "Clock";
  }
}

uint16_t activeRainbowColor() {
  unsigned long now = aquariumNowMs != 0 ? aquariumNowMs : millis();
  return rainbowColorAtMs(now);
}

bool activeClockTextRainbowEnabled() {
  return (clockDisplayStyle == CLOCK_STYLE_ASCII) ? clockAsciiRainbowEnabled : clockSmallRainbowEnabled;
}

uint16_t storedActiveClockTextColor() {
  return (clockDisplayStyle == CLOCK_STYLE_ASCII) ? clockAsciiTextColor : clockSmallTextColor;
}

uint16_t activeClockTextColor() {
  return activeClockTextRainbowEnabled() ? activeRainbowColor() : storedActiveClockTextColor();
}

uint16_t currentAsciiClockTextColor() {
  return clockAsciiRainbowEnabled ? activeRainbowColor() : clockAsciiTextColor;
}

uint16_t currentSmallClockTextColor() {
  return clockSmallRainbowEnabled ? activeRainbowColor() : clockSmallTextColor;
}

void setActiveClockTextColor(uint16_t color) {
  if (clockDisplayStyle == CLOCK_STYLE_ASCII) {
    clockAsciiTextColor = color;
    clockAsciiRainbowEnabled = false;
  } else {
    clockSmallTextColor = color;
    clockSmallRainbowEnabled = false;
  }
  markSettingsDirty();
}

void setActiveClockTextRainbowEnabled(bool enabled) {
  if (clockDisplayStyle == CLOCK_STYLE_ASCII) {
    clockAsciiRainbowEnabled = enabled;
  } else {
    clockSmallRainbowEnabled = enabled;
  }
  markSettingsDirty();
}

bool backgroundUsesGradientColor() {
  return backgroundStyle == BACKGROUND_STYLE_DITHERED || backgroundStyle == BACKGROUND_STYLE_SMOOTH;
}

uint16_t autoSkySlotColor(AutoSkySlot slot) {
  switch (slot) {
    case AUTO_SKY_SUNRISE:
      return autoSkySunriseColor;
    case AUTO_SKY_DAY:
      return autoSkyDayColor;
    case AUTO_SKY_SUNSET:
      return autoSkySunsetColor;
    case AUTO_SKY_NIGHT:
      return autoSkyNightColor;
    default:
      return autoSkyDayColor;
  }
}

int color5To8(int v5) { return (v5 << 3) | (v5 >> 2); }

int color6To8(int v6) { return (v6 << 2) | (v6 >> 4); }

int gradientNoiseThreshold(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393UL + (uint32_t)y * 668265263UL + 0x9E3779B9UL;
  h = (h ^ (h >> 13)) * 1274126177UL;
  h ^= (h >> 16);
  return (int)(h & 0xFF);
}

int gradientBayerThreshold(int x, int y, int scale) {
  static const uint8_t kBayer8x8[64] = {
      0, 48, 12, 60, 3, 51, 15, 63,
      32, 16, 44, 28, 35, 19, 47, 31,
      8, 56, 4, 52, 11, 59, 7, 55,
      40, 24, 36, 20, 43, 27, 39, 23,
      2, 50, 14, 62, 1, 49, 13, 61,
      34, 18, 46, 30, 33, 17, 45, 29,
      10, 58, 6, 54, 9, 57, 5, 53,
      42, 26, 38, 22, 41, 25, 37, 21};
  int sx = x / max(1, scale);
  int sy = y / max(1, scale);
  return kBayer8x8[(sy & 7) * 8 + (sx & 7)] << 2;
}

int gradientDitherThreshold(int x, int y, int pattern, int scale) {
  switch (pattern) {
    case GRADIENT_DITHER_ORDERED:
      return gradientBayerThreshold(x, y, scale);
    case GRADIENT_DITHER_CHECKER:
    case GRADIENT_DITHER_CHECKER_LOCKED:
      return (((x / max(1, scale)) + (y / max(1, scale))) & 1) ? 224 : 32;
    case GRADIENT_DITHER_NOISE:
      return gradientNoiseThreshold(x, y);
    default:
      return 128;
  }
}

void gradientRgbAtT(const uint16_t* colors, const uint8_t* stops, int count, int t255, int& r8, int& g8, int& b8) {
  if (count <= 0) {
    r8 = g8 = b8 = 0;
    return;
  }
  if (t255 <= stops[0]) {
    r8 = color5To8((colors[0] >> 11) & 0x1F);
    g8 = color6To8((colors[0] >> 5) & 0x3F);
    b8 = color5To8(colors[0] & 0x1F);
    return;
  }
  if (t255 >= stops[count - 1]) {
    r8 = color5To8((colors[count - 1] >> 11) & 0x1F);
    g8 = color6To8((colors[count - 1] >> 5) & 0x3F);
    b8 = color5To8(colors[count - 1] & 0x1F);
    return;
  }

  for (int i = 1; i < count; ++i) {
    if (t255 <= stops[i]) {
      int t0 = stops[i - 1];
      int t1 = stops[i];
      int seg = t1 - t0;
      int blend = (seg > 0) ? ((t255 - t0) * 255) / seg : 255;
      int inv = 255 - blend;
      int c0r = color5To8((colors[i - 1] >> 11) & 0x1F);
      int c0g = color6To8((colors[i - 1] >> 5) & 0x3F);
      int c0b = color5To8(colors[i - 1] & 0x1F);
      int c1r = color5To8((colors[i] >> 11) & 0x1F);
      int c1g = color6To8((colors[i] >> 5) & 0x3F);
      int c1b = color5To8(colors[i] & 0x1F);
      r8 = (c0r * inv + c1r * blend) / 255;
      g8 = (c0g * inv + c1g * blend) / 255;
      b8 = (c0b * inv + c1b * blend) / 255;
      return;
    }
  }
}

uint16_t rgb565From888(int r8, int g8, int b8) {
  return RGB565((uint8_t)clampVal(r8, 0, 255), (uint8_t)clampVal(g8, 0, 255), (uint8_t)clampVal(b8, 0, 255));
}

uint16_t blendRgb565(uint16_t a, uint16_t b, int t, int span) {
  if (span <= 0) return b;
  t = clampVal(t, 0, span);
  int r = rgb565R8(a) + ((rgb565R8(b) - rgb565R8(a)) * t) / span;
  int g = rgb565G8(a) + ((rgb565G8(b) - rgb565G8(a)) * t) / span;
  int bl = rgb565B8(a) + ((rgb565B8(b) - rgb565B8(a)) * t) / span;
  return rgb565From888(r, g, bl);
}

uint16_t autoSkyColorForSecond(int secondOfDay) {
  static const int kSunriseSecond = 6 * 3600;
  static const int kDaySecond = 9 * 3600;
  static const int kSunsetSecond = 18 * 3600;
  static const int kNightSecond = 21 * 3600;
  static const int kDaySeconds = 24 * 3600;

  secondOfDay = clampVal(secondOfDay, 0, kDaySeconds - 1);
  if (secondOfDay < kSunriseSecond) {
    return blendRgb565(autoSkyNightColor, autoSkySunriseColor,
                       secondOfDay + kDaySeconds - kNightSecond,
                       kDaySeconds - kNightSecond + kSunriseSecond);
  }
  if (secondOfDay < kDaySecond) {
    return blendRgb565(autoSkySunriseColor, autoSkyDayColor,
                       secondOfDay - kSunriseSecond, kDaySecond - kSunriseSecond);
  }
  if (secondOfDay < kSunsetSecond) {
    return blendRgb565(autoSkyDayColor, autoSkySunsetColor,
                       secondOfDay - kDaySecond, kSunsetSecond - kDaySecond);
  }
  if (secondOfDay < kNightSecond) {
    return blendRgb565(autoSkySunsetColor, autoSkyNightColor,
                       secondOfDay - kSunsetSecond, kNightSecond - kSunsetSecond);
  }
  return blendRgb565(autoSkyNightColor, autoSkySunriseColor,
                     secondOfDay - kNightSecond, kDaySeconds - kNightSecond + kSunriseSecond);
}

uint16_t currentAutoSkyColor() {
  unsigned long now = millis();
  unsigned long secondsIntoMinute = (now >= clockLastMinuteMs) ? ((now - clockLastMinuteMs) / 1000UL) : 0UL;
  if (secondsIntoMinute > 59UL) secondsIntoMinute = 59UL;
  return autoSkyColorForSecond(clockHour * 3600 + clockMinute * 60 + (int)secondsIntoMinute);
}

uint16_t activeBackgroundGradientColor() {
  if (autoSkyEnabled) return autoSkyGradientColor;
  return backgroundRainbowEnabled ? backgroundRainbowColor : backgroundGradientColor;
}

int backgroundCycleIndex(BackgroundStyle style) {
  for (int i = 0; i < BACKGROUND_CYCLE_STYLE_COUNT; ++i) {
    if (kBackgroundCycleStyles[i] == style) return i;
  }
  return 1;  // Dithered
}

void cycleBackgroundStyle(int delta) {
  int next = backgroundCycleIndex(backgroundStyle) + delta;
  while (next < 0) next += BACKGROUND_CYCLE_STYLE_COUNT;
  while (next >= BACKGROUND_CYCLE_STYLE_COUNT) next -= BACKGROUND_CYCLE_STYLE_COUNT;
  backgroundStyle = kBackgroundCycleStyles[next];
  if (autoSkyEnabled && !backgroundUsesGradientColor()) {
    autoSkyEnabled = false;
    activeAutoSkyColorSlot = AUTO_SKY_SLOT_NONE;
  }
  invalidateBackgroundGradientCache();
  markSettingsDirty();
}

void setBackgroundGradientColor(uint16_t color) {
  backgroundGradientColor = color;
  backgroundRainbowEnabled = false;
  autoSkyEnabled = false;
  invalidateBackgroundGradientCache();
  markSettingsDirty();
}

void setBackgroundRainbowEnabled(bool enabled) {
  if (backgroundRainbowEnabled == enabled) return;
  backgroundRainbowEnabled = enabled;
  if (enabled) {
    autoSkyEnabled = false;
    unsigned long now = aquariumNowMs != 0 ? aquariumNowMs : millis();
    backgroundRainbowColor = rainbowColorAtMs(now);
    lastBackgroundRainbowUpdateMs = now;
  }
  invalidateBackgroundGradientCache();
  markSettingsDirty();
}

void setAutoSkyEnabled(bool enabled) {
  if (autoSkyEnabled == enabled) return;
  autoSkyEnabled = enabled;
  if (enabled) {
    backgroundRainbowEnabled = false;
    if (!backgroundUsesGradientColor()) backgroundStyle = BACKGROUND_STYLE_DITHERED;
    autoSkyGradientColor = currentAutoSkyColor();
  }
  invalidateBackgroundGradientCache();
  markSettingsDirty();
}

void setAutoSkySlotColor(AutoSkySlot slot, uint16_t color) {
  switch (slot) {
    case AUTO_SKY_SUNRISE:
      autoSkySunriseColor = color;
      break;
    case AUTO_SKY_DAY:
      autoSkyDayColor = color;
      break;
    case AUTO_SKY_SUNSET:
      autoSkySunsetColor = color;
      break;
    case AUTO_SKY_NIGHT:
      autoSkyNightColor = color;
      break;
    default:
      return;
  }
  if (autoSkyEnabled) autoSkyGradientColor = currentAutoSkyColor();
  invalidateBackgroundGradientCache();
  markSettingsDirty();
}

void serviceBackgroundRainbow(unsigned long now) {
  if (!backgroundRainbowEnabled) return;
  if (backgroundRainbowColor != 0 && now - lastBackgroundRainbowUpdateMs < BACKGROUND_RAINBOW_UPDATE_MS) return;

  uint16_t nextColor = rainbowColorAtMs(now);
  lastBackgroundRainbowUpdateMs = now;
  if (nextColor == backgroundRainbowColor) return;
  backgroundRainbowColor = nextColor;
  invalidateBackgroundGradientCache();
}

void serviceAutoSky() {
  if (!autoSkyEnabled) return;
  uint16_t nextColor = currentAutoSkyColor();
  if (nextColor == autoSkyGradientColor) return;
  autoSkyGradientColor = nextColor;
  invalidateBackgroundGradientCache();
}

const char* backgroundStyleName() {
  switch (backgroundStyle) {
    case BACKGROUND_STYLE_BLACK:
      return "Black";
    case BACKGROUND_STYLE_DITHERED:
      return "Dithered";
    case BACKGROUND_STYLE_SMOOTH:
      return "Smooth";
    case BACKGROUND_STYLE_FLOWERS:
      return "Flowers";
    default:
      return "Background";
  }
}

const char* autoSkySlotName(AutoSkySlot slot) {
  switch (slot) {
    case AUTO_SKY_SUNRISE:
      return "Sunrise";
    case AUTO_SKY_DAY:
      return "Day";
    case AUTO_SKY_SUNSET:
      return "Sunset";
    case AUTO_SKY_NIGHT:
      return "Night";
    default:
      return "Sky";
  }
}

BackgroundStyle normalizeBackgroundStyle(uint8_t savedStyle) {
  if (savedStyle < BACKGROUND_STYLE_COUNT) return (BackgroundStyle)savedStyle;
  return DEFAULT_BACKGROUND_STYLE;
}

BackgroundStyle legacyBackgroundStyle(uint8_t savedMode, uint8_t savedVersion) {
  // v1.96 and older saved colour-specific background IDs. Migrate those into
  // the new style+colour model without surprising existing installs.
  if (savedVersion < 4) {
    switch (savedMode) {
      case 0:
        return BACKGROUND_STYLE_BLACK;
      case 7:
        return BACKGROUND_STYLE_SMOOTH;
      case 9:
        return BACKGROUND_STYLE_DITHERED;
      case 10:
        return BACKGROUND_STYLE_FLOWERS;
      default:
        return BACKGROUND_STYLE_DITHERED;
    }
  }

  switch (savedMode) {
    case 0:
      return BACKGROUND_STYLE_BLACK;
    case 1:
      return BACKGROUND_STYLE_DITHERED;
    case 2:
      return BACKGROUND_STYLE_SMOOTH;
    case 3:
      return BACKGROUND_STYLE_DITHERED;
    case 4:
      return BACKGROUND_STYLE_FLOWERS;
    default:
      return BACKGROUND_STYLE_DITHERED;
  }
}

uint16_t legacyBackgroundColor(uint8_t savedMode, uint8_t savedVersion) {
  if ((savedVersion < 4 && savedMode == 9) || (savedVersion >= 4 && savedMode == 3)) {
    return DEFAULT_BACKGROUND_PURPLE_COLOR;
  }
  return DEFAULT_BACKGROUND_GRADIENT_COLOR;
}

void buildGradientColorsFromTop(uint16_t topColor, uint16_t* colorsOut, int count) {
  int rTop = color5To8((topColor >> 11) & 0x1F);
  int gTop = color6To8((topColor >> 5) & 0x3F);
  int bTop = color5To8(topColor & 0x1F);
  int brightnessCount = sizeof(kGradientBrightness) / sizeof(kGradientBrightness[0]);
  for (int i = 0; i < count; ++i) {
    int brightness = (i < brightnessCount) ? kGradientBrightness[i] : 0;
    colorsOut[i] = rgb565From888((rTop * brightness) / 255, (gTop * brightness) / 255,
                                 (bTop * brightness) / 255);
  }
  if (count > 0) colorsOut[count - 1] = BG_COLOR;
}

uint16_t swap565(uint16_t color) {
  return (uint16_t)((color << 8) | (color >> 8));
}

void drawVerticalGradientStops(TFT_eSprite& s, const uint16_t* colors, const uint8_t* stops, int stopCount, int gradientHeight,
                               int ditherPattern, int ditherScale, int ditherAmplitude) {
  int drawH = clampVal(gradientHeight, 2, SCREEN_H);
  int yMax = drawH - 1;
  int cellSize = max(1, ditherScale);
  bool lockToDitherCells = (ditherPattern == GRADIENT_DITHER_CHECKER_LOCKED);
  for (int y = 0; y < drawH; ++y) {
    int sampleY = lockToDitherCells ? ((y / cellSize) * cellSize) : y;
    int baseT255 = (sampleY * 255) / yMax;

    for (int x = 0; x < SCREEN_W; ++x) {
      int threshold = gradientDitherThreshold(x, y, ditherPattern, ditherScale) - 128;
      int sampleT255 = clampVal(baseT255 + (threshold * ditherAmplitude) / 128, 0, 255);
      int r8, g8, b8;
      gradientRgbAtT(colors, stops, stopCount, sampleT255, r8, g8, b8);
      s.drawPixel(x, y, rgb565From888(r8, g8, b8));
    }
  }
}

void buildGradientBandCache(const uint16_t* colors, const uint8_t* stops, int stopCount, int ditherPattern, int ditherScale,
                            int ditherAmplitude) {
  if (gradientBandCache == nullptr) return;

  int cellSize = max(1, ditherScale);
  bool lockToDitherCells = (ditherPattern == GRADIENT_DITHER_CHECKER_LOCKED);
  for (int y = 0; y < BACKGROUND_GRADIENT_H; ++y) {
    int sampleY = lockToDitherCells ? ((y / cellSize) * cellSize) : y;
    int baseT255 = (sampleY * 255) / (BACKGROUND_GRADIENT_H - 1);
    for (int x = 0; x < SCREEN_W; ++x) {
      int threshold = gradientDitherThreshold(x, y, ditherPattern, ditherScale) - 128;
      int sampleT255 = clampVal(baseT255 + (threshold * ditherAmplitude) / 128, 0, 255);
      int r8, g8, b8;
      gradientRgbAtT(colors, stops, stopCount, sampleT255, r8, g8, b8);
      gradientBandCache[y * SCREEN_W + x] = swap565(rgb565From888(r8, g8, b8));
    }
  }
}

void drawTopGradientBackground(TFT_eSprite& s, uint16_t topColor, int ditherPattern, int ditherScale, int ditherAmplitude) {
  clearRenderSurface(s);
  buildGradientColorsFromTop(topColor, activeGradientColors, kGradientStopCount);
  drawVerticalGradientStops(s, activeGradientColors, kGradientStops, kGradientStopCount, BACKGROUND_GRADIENT_H, ditherPattern, ditherScale,
                            ditherAmplitude);
}

void drawCachedTopGradientBackground(TFT_eSprite& s, BackgroundStyle style, uint16_t topColor, int ditherPattern,
                                     int ditherScale, int ditherAmplitude) {
  if (gradientBandCache == nullptr) {
    drawTopGradientBackground(s, topColor, ditherPattern, ditherScale, ditherAmplitude);
    return;
  }

  if (gradientBandCacheStyle != style || gradientBandCacheColor != topColor) {
    buildGradientColorsFromTop(topColor, activeGradientColors, kGradientStopCount);
    buildGradientBandCache(activeGradientColors, kGradientStops, kGradientStopCount, ditherPattern, ditherScale, ditherAmplitude);
    gradientBandCacheStyle = style;
    gradientBandCacheColor = topColor;
  }
  clearRenderSurface(s);
  s.pushImage(0, 0, SCREEN_W, BACKGROUND_GRADIENT_H, gradientBandCache);
}

void allocateGradientBandCache() {
  size_t pixelCount = (size_t)SCREEN_W * (size_t)BACKGROUND_GRADIENT_H;
  gradientBandCache = (uint16_t*)malloc(pixelCount * sizeof(uint16_t));
  invalidateBackgroundGradientCache();
}

uint16_t randomFlowerColor(int index) {
  if (index == kPixelFlowerCount - 1) {
    return RGB565((uint8_t)random(92, 146), (uint8_t)random(84, 126), (uint8_t)random(8, 28));
  }
  return RGB565((uint8_t)random(0, 10), (uint8_t)random(18, 38), (uint8_t)random(46, 96));
}

void randomizeFlowers() {
  pixelFlowers[0].cx = random(42, 98);
  pixelFlowers[0].cy = random(50, 86);
  pixelFlowers[0].radius = random(50, 63);
  pixelFlowers[0].rotation = frand(0.0f, 6.28318f);
  pixelFlowers[0].color = randomFlowerColor(0);

  pixelFlowers[1].cx = random(214, 278);
  pixelFlowers[1].cy = random(48, 90);
  pixelFlowers[1].radius = random(50, 63);
  pixelFlowers[1].rotation = frand(0.0f, 6.28318f);
  pixelFlowers[1].color = randomFlowerColor(1);

  pixelFlowers[2].cx = random(162, 248);
  pixelFlowers[2].cy = random(124, SEA_LEVEL_Y - 18);
  pixelFlowers[2].radius = random(20, 34);
  pixelFlowers[2].rotation = frand(0.0f, 6.28318f);
  pixelFlowers[2].color = randomFlowerColor(2);
  pixelFlowerGeometryDirty = true;
  markSettingsDirty();
}

void drawThickLine(TFT_eSprite& s, int x0, int y0, int x1, int y1, uint16_t color) {
  s.drawLine(x0, y0, x1, y1, color);
  s.drawLine(x0 + 1, y0, x1 + 1, y1, color);
  s.drawLine(x0, y0 + 1, x1, y1 + 1, color);
  s.drawLine(x0 - 1, y0, x1 - 1, y1, color);
  s.drawLine(x0, y0 - 1, x1, y1 - 1, color);
}

void rebuildPixelFlowerGeometry() {
  if (!pixelFlowerGeometryDirty) return;

  for (int flowerIndex = 0; flowerIndex < kPixelFlowerCount; ++flowerIndex) {
    const PixelFlowerSpec& flower = pixelFlowers[flowerIndex];
    for (int i = 0; i <= PIXEL_FLOWER_SEGMENTS; ++i) {
      float theta = flower.rotation + (6.28318f * i) / PIXEL_FLOWER_SEGMENTS;
      float petal = 0.5f + 0.5f * sinf(theta * 5.0f);
      float r = flower.radius * (0.58f + 0.42f * petal);
      pixelFlowerPoints[flowerIndex][i].x = (int16_t)(flower.cx + (int)(cosf(theta) * r));
      pixelFlowerPoints[flowerIndex][i].y = (int16_t)(flower.cy + (int)(sinf(theta) * r));
    }
  }

  pixelFlowerGeometryDirty = false;
}

void drawPixelFlower(TFT_eSprite& s, int flowerIndex, uint16_t color) {
  for (int i = 1; i <= PIXEL_FLOWER_SEGMENTS; ++i) {
    const PixelFlowerPoint& prev = pixelFlowerPoints[flowerIndex][i - 1];
    const PixelFlowerPoint& point = pixelFlowerPoints[flowerIndex][i];
    drawThickLine(s, prev.x, prev.y, point.x, point.y, color);
  }
}

void drawFlowerBackground(TFT_eSprite& s) {
  clearRenderSurface(s);
  rebuildPixelFlowerGeometry();
  for (int i = 0; i < kPixelFlowerCount; ++i) {
    drawPixelFlower(s, i, pixelFlowers[i].color);
  }
}

void drawBackground(TFT_eSprite& s, float tSec) {
  (void)tSec;
  switch (backgroundStyle) {
    case BACKGROUND_STYLE_BLACK:
      clearRenderSurface(s);
      break;
    case BACKGROUND_STYLE_DITHERED:
      drawCachedTopGradientBackground(s, BACKGROUND_STYLE_DITHERED, activeBackgroundGradientColor(),
                                      GRADIENT_DITHER_ORDERED, 4, 28);
      break;
    case BACKGROUND_STYLE_SMOOTH:
      drawCachedTopGradientBackground(s, BACKGROUND_STYLE_SMOOTH, activeBackgroundGradientColor(),
                                      GRADIENT_DITHER_NONE, 1, 0);
      break;
    case BACKGROUND_STYLE_FLOWERS:
      drawFlowerBackground(s);
      break;
    default:
      clearRenderSurface(s);
      break;
  }
}

inline uint8_t fishGlyphLength(const Fish& f) {
  return (f.vx >= 0.0f) ? fishGlyphLenRight[f.type] : fishGlyphLenLeft[f.type];
}

inline const int16_t* fishGlyphOffsets(const Fish& f) {
  return (f.vx >= 0.0f) ? fishGlyphOffsetRight[f.type] : fishGlyphOffsetLeft[f.type];
}

inline int activeFishLimit() {
  return clampVal(fishTargetCount, 0, MAX_FISH_POOL);
}

inline int activeBubbleLimit() {
  return clampVal(bubbleTargetCount, 0, MAX_BUBBLES);
}

inline bool fishAvoidanceEnabled() {
  return true;
}

unsigned long octopusSpawnIntervalMs() {
  int frequency = normalizeOctopusFrequency(octopusFrequency);
  return 3600000UL / (unsigned long)frequency;
}

unsigned long seahorseSpawnIntervalMs() {
  int frequency = normalizeSeahorseFrequency(seahorseFrequency);
  return 3600000UL / (unsigned long)frequency;
}

unsigned long autoFeedIntervalMs() {
  int frequency = normalizeAutoFeedFrequency(autoFeedFrequency);
  if (frequency <= 0) return 0;
  return 3600000UL / (unsigned long)frequency;
}

unsigned long snailSpawnIntervalMs() {
  int frequency = normalizeCreatureFrequency(snailFrequency);
  if (frequency <= 0) return 0;
  return 3600000UL / (unsigned long)frequency;
}

unsigned long jellyfishSpawnIntervalMs() {
  int frequency = normalizeCreatureFrequency(jellyfishFrequency);
  if (frequency <= 0) return 0;
  return 3600000UL / (unsigned long)frequency;
}

const TimezoneOption& currentTimezone() {
  timezoneIndex = clampVal(timezoneIndex, 0, TIMEZONE_COUNT - 1);
  return timezoneOptions[timezoneIndex];
}

void setWifiStatus(const char* text) {
  copySafe(wifiStatusText, sizeof(wifiStatusText), text);
}

const char* internetTimeStatus() {
  if (!clockUseInternetTime) return "Manual";
  if (!wifiEnabled) return "WiFi Off";
  if (!wifiConnected) return wifiConnecting ? "Connecting" : "Waiting WiFi";
  return wifiTimeSynced ? "Synced" : "Syncing";
}

void ensureWifiRadioStarted() {
  if (wifiRadioStarted) return;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  wifiRadioStarted = true;
}

void clearWifiScanResults() {
  wifiNetworkCount = 0;
  wifiNetworkPage = 0;
  for (int i = 0; i < MAX_WIFI_NETWORKS; ++i) {
    wifiNetworkNames[i][0] = '\0';
    wifiNetworkRssi[i] = 0;
    wifiNetworkOpen[i] = false;
  }
}

bool wifiSsidAlreadyListed(const char* ssid) {
  for (int i = 0; i < wifiNetworkCount; ++i) {
    if (strncmp(wifiNetworkNames[i], ssid, WIFI_SSID_MAX_LEN) == 0) return true;
  }
  return false;
}

void startWifiScan() {
  if (!wifiEnabled) return;
  ensureWifiRadioStarted();
  if (wifiConnecting) {
    wifiConnecting = false;
    wifiSavePendingCredentials = false;
    WiFi.disconnect(false);
  }
  clearWifiScanResults();
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true);
  wifiScanInProgress = true;
  lastWifiServiceMs = 0;
  setWifiStatus("Scanning...");
}

void finishWifiScanIfReady() {
  if (!wifiScanInProgress) return;
  int scanResult = WiFi.scanComplete();
  if (scanResult == WIFI_SCAN_RUNNING) return;

  wifiScanInProgress = false;
  clearWifiScanResults();
  if (scanResult < 0) {
    setWifiStatus("Scan failed");
    WiFi.scanDelete();
    return;
  }

  for (int i = 0; i < scanResult && wifiNetworkCount < MAX_WIFI_NETWORKS; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    if (wifiSsidAlreadyListed(ssid.c_str())) continue;

    copySafe(wifiNetworkNames[wifiNetworkCount], sizeof(wifiNetworkNames[wifiNetworkCount]), ssid.c_str());
    wifiNetworkRssi[wifiNetworkCount] = WiFi.RSSI(i);
    wifiNetworkOpen[wifiNetworkCount] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    wifiNetworkCount++;
  }

  WiFi.scanDelete();
  setWifiStatus(wifiNetworkCount > 0 ? "Select network" : "No networks found");
}

void beginInternetTimeSync() {
  if (!wifiConnected) return;
  const TimezoneOption& tz = currentTimezone();
  configTzTime(tz.posix, CLOCK_NTP_1, CLOCK_NTP_2, CLOCK_NTP_3);
  wifiTimeConfigured = true;
  wifiLastNtpAttemptMs = 0;
  wifiTimeSynced = false;
}

void cycleTimezone(int delta) {
  timezoneIndex += delta;
  if (timezoneIndex < 0) timezoneIndex = TIMEZONE_COUNT - 1;
  if (timezoneIndex >= TIMEZONE_COUNT) timezoneIndex = 0;
  wifiTimeConfigured = false;
  wifiTimeSynced = false;
  if (clockUseInternetTime && wifiConnected) {
    beginInternetTimeSync();
  }
  markSettingsDirty();
}

bool syncClockFromSystemTime(bool markDirty) {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 10)) return false;

  clockYear = timeInfo.tm_year + 1900;
  clockMonth = timeInfo.tm_mon + 1;
  clockDay = timeInfo.tm_mday;
  clockHour = timeInfo.tm_hour;
  clockMinute = timeInfo.tm_min;
  normalizeClockDate();
  unsigned long now = millis();
  unsigned long secondOffsetMs = (unsigned long)clampVal(timeInfo.tm_sec, 0, 59) * 1000UL;
  clockLastMinuteMs = (now > secondOffsetMs) ? (now - secondOffsetMs) : now;
  wifiTimeSynced = true;
  wifiLastNtpSyncMs = now;
  if (markDirty) markSettingsDirty();
  return true;
}

void startWifiConnect(const char* ssid, const char* pass, bool savePendingCredentials) {
  if (!wifiEnabled || !ssid || ssid[0] == '\0') return;
  ensureWifiRadioStarted();
  copySafe(pendingWifiSsid, sizeof(pendingWifiSsid), ssid);
  copySafe(pendingWifiPass, sizeof(pendingWifiPass), pass ? pass : "");
  wifiSavePendingCredentials = savePendingCredentials;
  wifiConnecting = true;
  wifiConnected = false;
  wifiConnectionFailed = false;
  wifiTimeSynced = false;
  wifiConnectStartMs = millis();
  wifiLastReconnectMs = wifiConnectStartMs;
  WiFi.disconnect(false);
  WiFi.begin(pendingWifiSsid, pendingWifiPass);
  lastWifiServiceMs = 0;
  setWifiStatus("Connecting...");
}

void setWifiEnabled(bool enabled) {
  if (wifiEnabled == enabled && enabled) {
    if (wifiSsid[0] != '\0' && !wifiConnected && !wifiConnecting) {
      startWifiConnect(wifiSsid, wifiPass, false);
    } else if (wifiSsid[0] == '\0' && !wifiScanInProgress) {
      startWifiScan();
    }
    return;
  }

  wifiEnabled = enabled;
  if (!wifiEnabled) {
    wifiScanInProgress = false;
    wifiConnecting = false;
    wifiConnected = false;
    wifiConnectionFailed = false;
    wifiSavePendingCredentials = false;
    wifiTimeConfigured = false;
    wifiTimeSynced = false;
    clockUseInternetTime = false;
    pendingWifiSsid[0] = '\0';
    pendingWifiPass[0] = '\0';
    clearWifiScanResults();
    WiFi.scanDelete();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiRadioStarted = false;
    lastWifiServiceMs = 0;
    setWifiStatus("Off");
    markSettingsDirty();
    return;
  }

  ensureWifiRadioStarted();
  setWifiStatus("Starting...");
  markSettingsDirty();
  if (wifiSsid[0] != '\0') {
    startWifiConnect(wifiSsid, wifiPass, false);
  } else {
    startWifiScan();
  }
}

void serviceInternetTime(unsigned long now) {
  if (!clockUseInternetTime || !wifiConnected) return;
  if (!wifiTimeConfigured) beginInternetTimeSync();

  bool needsRetry = !wifiTimeSynced && (wifiLastNtpAttemptMs == 0 || now - wifiLastNtpAttemptMs >= NTP_RETRY_MS);
  bool needsRefresh = wifiTimeSynced && (now - wifiLastNtpSyncMs >= NTP_REFRESH_MS);
  if (!needsRetry && !needsRefresh) return;

  wifiLastNtpAttemptMs = now;
  if (syncClockFromSystemTime(true)) {
    setWifiStatus("Connected");
  }
}

unsigned long wifiServiceIntervalMs() {
  if (wifiPanelOpen || wifiScanInProgress || wifiConnecting) return WIFI_SERVICE_ACTIVE_MS;
  if (wifiConnected && wifiTimeSynced) return WIFI_SERVICE_SYNCED_MS;
  if (wifiConnected) return WIFI_SERVICE_UNSYNCED_MS;
  return WIFI_SERVICE_IDLE_MS;
}

void serviceWifi(unsigned long now) {
  if (!wifiEnabled) {
    lastWifiServiceMs = 0;
    return;
  }

  unsigned long interval = wifiServiceIntervalMs();
  if (lastWifiServiceMs != 0 && now - lastWifiServiceMs < interval) return;
  lastWifiServiceMs = now;

  ensureWifiRadioStarted();
  finishWifiScanIfReady();

  bool connectedNow = (WiFi.status() == WL_CONNECTED);
  if (connectedNow) {
    if (!wifiConnected) {
      wifiConnected = true;
      wifiConnecting = false;
      wifiConnectionFailed = false;
      if (wifiSavePendingCredentials) {
        copySafe(wifiSsid, sizeof(wifiSsid), pendingWifiSsid);
        copySafe(wifiPass, sizeof(wifiPass), pendingWifiPass);
        wifiSavePendingCredentials = false;
      }
      clockUseInternetTime = true;
      beginInternetTimeSync();
      setWifiStatus("Connected");
      savePersistentState();
    }
    serviceInternetTime(now);
    return;
  }

  if (wifiConnected) {
    wifiConnected = false;
    wifiTimeConfigured = false;
    wifiTimeSynced = false;
    setWifiStatus("Disconnected");
  }

  if (wifiConnecting) {
    if (now - wifiConnectStartMs >= WIFI_CONNECT_TIMEOUT_MS) {
      wifiConnecting = false;
      wifiConnectionFailed = true;
      wifiSavePendingCredentials = false;
      WiFi.disconnect(false);
      setWifiStatus("Connect failed");
    }
    return;
  }

  if (wifiSsid[0] != '\0' && (wifiLastReconnectMs == 0 || now - wifiLastReconnectMs >= WIFI_RECONNECT_DELAY_MS)) {
    startWifiConnect(wifiSsid, wifiPass, false);
  } else if (wifiSsid[0] == '\0' && !wifiScanInProgress && wifiNetworkCount == 0) {
    startWifiScan();
  }
}

// ===== part: new_persistence =====
// ------------------------------ Persistence -----------------------------------
void savePersistentState() {
  prefs.begin("ascii-aq", false);
  prefs.putUChar("ver", 1);
  prefs.putInt("fish", fishTargetCount);
  prefs.putInt("bubbles", bubbleTargetCount);
  prefs.putInt("oct_freq", octopusFrequency);
  prefs.putInt("seah_freq", seahorseFrequency);
  prefs.putInt("auto_feed", autoFeedFrequency);
  prefs.putInt("snail_fr", snailFrequency);
  prefs.putInt("jelly_fr", jellyfishFrequency);
  prefs.putFloat("sway", seaweedSwaySpeed);
  prefs.putFloat("sea_len", seaweedLength);
  prefs.putFloat("sea_rand", seaweedLengthRandomness);
  prefs.putUChar("bg_style", (uint8_t)backgroundStyle);
  prefs.putUShort("bg_color", backgroundGradientColor);
  prefs.putBool("bg_rain", backgroundRainbowEnabled);
  prefs.putBool("sky_on", autoSkyEnabled);
  prefs.putUShort("sky_sr", autoSkySunriseColor);
  prefs.putUShort("sky_day", autoSkyDayColor);
  prefs.putUShort("sky_ss", autoSkySunsetColor);
  prefs.putUShort("sky_nt", autoSkyNightColor);
  prefs.putBool("clock_on", clockVisible);
  prefs.putBool("clock_24h", clockUse24Hour);
  prefs.putBool("clock_net", clockUseInternetTime);
  prefs.putUChar("clk_style", (uint8_t)clockDisplayStyle);
  prefs.putUChar("clk_pos", (uint8_t)clockSmallPosition);
  prefs.putUChar("clk_font", (uint8_t)asciiClockFontIndex);
  prefs.putUShort("clk_s_col", clockSmallTextColor);
  prefs.putUShort("clk_a_col", clockAsciiTextColor);
  prefs.putBool("clk_s_rn", clockSmallRainbowEnabled);
  prefs.putBool("clk_a_rn", clockAsciiRainbowEnabled);
  prefs.putInt("tz_idx", timezoneIndex);
  prefs.putInt("clk_year", clockYear);
  prefs.putInt("clk_month", clockMonth);
  prefs.putInt("clk_day", clockDay);
  prefs.putInt("clk_hour", clockHour);
  prefs.putInt("clk_min", clockMinute);
  prefs.putBool("wifi_on", wifiEnabled);
  prefs.putString("wifi_ssid", wifiSsid);
  prefs.putString("wifi_pass", wifiPass);
  prefs.putInt("lcd_brite", lcdBrightness);
  prefs.putBytes("flowers", pixelFlowers, sizeof(pixelFlowers));
  prefs.end();
  settingsDirty = false;
  lastSettingsSaveMs = millis();
}

void loadPersistentState() {
  memcpy(pixelFlowers, kDefaultPixelFlowers, sizeof(pixelFlowers));
  wifiSsid[0] = '\0';
  wifiPass[0] = '\0';

  prefs.begin("ascii-aq", true);
  uint8_t version = prefs.getUChar("ver", 0);
  if (version != 0) {
    fishTargetCount = prefs.getInt("fish", DEFAULT_FISH);
    bubbleTargetCount = prefs.getInt("bubbles", DEFAULT_BUBBLES);
    octopusFrequency = prefs.getInt("oct_freq", DEFAULT_OCTOPUS_FREQUENCY);
    seahorseFrequency = prefs.getInt("seah_freq", DEFAULT_SEAHORSE_FREQUENCY);
    autoFeedFrequency = prefs.getInt("auto_feed", DEFAULT_AUTO_FEED_FREQUENCY);
    snailFrequency = prefs.getInt("snail_fr", DEFAULT_SNAIL_FREQUENCY);
    jellyfishFrequency = prefs.getInt("jelly_fr", DEFAULT_JELLYFISH_FREQUENCY);
    seaweedSwaySpeed = prefs.getFloat("sway", DEFAULT_SWAY);
    seaweedLength = prefs.getFloat("sea_len", DEFAULT_SEAWEED_LENGTH);
    seaweedLengthRandomness = prefs.getFloat("sea_rand", DEFAULT_SEAWEED_LENGTH_RANDOMNESS);
    backgroundStyle = normalizeBackgroundStyle(prefs.getUChar("bg_style", (uint8_t)DEFAULT_BACKGROUND_STYLE));
    backgroundGradientColor = prefs.getUShort("bg_color", DEFAULT_BACKGROUND_GRADIENT_COLOR);
    backgroundRainbowEnabled = prefs.getBool("bg_rain", false);
    autoSkyEnabled = prefs.getBool("sky_on", false);
    autoSkySunriseColor = prefs.getUShort("sky_sr", DEFAULT_AUTO_SKY_SUNRISE_COLOR);
    autoSkyDayColor = prefs.getUShort("sky_day", DEFAULT_AUTO_SKY_DAY_COLOR);
    autoSkySunsetColor = prefs.getUShort("sky_ss", DEFAULT_AUTO_SKY_SUNSET_COLOR);
    autoSkyNightColor = prefs.getUShort("sky_nt", DEFAULT_AUTO_SKY_NIGHT_COLOR);
    clockVisible = prefs.getBool("clock_on", false);
    clockUse24Hour = prefs.getBool("clock_24h", false);
    clockUseInternetTime = prefs.getBool("clock_net", false);
    clockDisplayStyle = (ClockDisplayStyle)prefs.getUChar("clk_style", (uint8_t)CLOCK_STYLE_SMALL_TEXT);
    clockSmallPosition = (ClockSmallPosition)prefs.getUChar("clk_pos", (uint8_t)CLOCK_SMALL_TOP);
    asciiClockFontIndex = prefs.getUChar("clk_font", DEFAULT_ASCII_CLOCK_FONT_INDEX);
    clockSmallTextColor = prefs.getUShort("clk_s_col", DEFAULT_SMALL_CLOCK_COLOR);
    clockAsciiTextColor = prefs.getUShort("clk_a_col", DEFAULT_ASCII_CLOCK_COLOR);
    clockSmallRainbowEnabled = prefs.getBool("clk_s_rn", false);
    clockAsciiRainbowEnabled = prefs.getBool("clk_a_rn", false);
    timezoneIndex = prefs.getInt("tz_idx", DEFAULT_TIMEZONE_INDEX);
    clockYear = prefs.getInt("clk_year", DEFAULT_CLOCK_YEAR);
    clockMonth = prefs.getInt("clk_month", DEFAULT_CLOCK_MONTH);
    clockDay = prefs.getInt("clk_day", DEFAULT_CLOCK_DAY);
    clockHour = prefs.getInt("clk_hour", DEFAULT_CLOCK_HOUR);
    clockMinute = prefs.getInt("clk_min", DEFAULT_CLOCK_MINUTE);
    wifiEnabled = prefs.getBool("wifi_on", false);
    prefs.getString("wifi_ssid", wifiSsid, sizeof(wifiSsid));
    prefs.getString("wifi_pass", wifiPass, sizeof(wifiPass));
    lcdBrightness = prefs.getInt("lcd_brite", DEFAULT_LCD_BRIGHTNESS);
    size_t flowerBytes = prefs.getBytesLength("flowers");
    if (flowerBytes == sizeof(pixelFlowers)) {
      prefs.getBytes("flowers", pixelFlowers, sizeof(pixelFlowers));
    }
  }
  prefs.end();

  fishTargetCount = clampVal(fishTargetCount, MIN_FISH, MAX_FISH);
  bubbleTargetCount = clampVal(bubbleTargetCount, MIN_BUBBLES, MAX_BUBBLES);
  octopusFrequency = normalizeOctopusFrequency(octopusFrequency);
  seahorseFrequency = normalizeSeahorseFrequency(seahorseFrequency);
  autoFeedFrequency = normalizeAutoFeedFrequency(autoFeedFrequency);
  snailFrequency = normalizeCreatureFrequency(snailFrequency);
  jellyfishFrequency = normalizeCreatureFrequency(jellyfishFrequency);
  lcdBrightness = clampVal(lcdBrightness, MIN_LCD_BRIGHTNESS, MAX_LCD_BRIGHTNESS);
  seaweedSwaySpeed = clampVal(seaweedSwaySpeed, MIN_SWAY, MAX_SWAY);
  seaweedLength = clampVal(seaweedLength, MIN_SEAWEED_LENGTH, MAX_SEAWEED_LENGTH);
  seaweedLengthRandomness = clampVal(seaweedLengthRandomness, MIN_SEAWEED_LENGTH_RANDOMNESS, MAX_SEAWEED_LENGTH_RANDOMNESS);
  backgroundStyle = normalizeBackgroundStyle((uint8_t)backgroundStyle);
  if (autoSkyEnabled) {
    backgroundRainbowEnabled = false;
    if (!backgroundUsesGradientColor()) backgroundStyle = BACKGROUND_STYLE_DITHERED;
  }
  backgroundRainbowColor = backgroundRainbowEnabled ? rainbowColorAtMs(millis()) : backgroundGradientColor;
  autoSkyGradientColor = currentAutoSkyColor();
  if ((int)clockDisplayStyle < 0 || clockDisplayStyle >= CLOCK_STYLE_COUNT) clockDisplayStyle = CLOCK_STYLE_SMALL_TEXT;
  if ((int)clockSmallPosition < 0 || clockSmallPosition >= CLOCK_SMALL_POSITION_COUNT) clockSmallPosition = CLOCK_SMALL_TOP;
  asciiClockFontIndex = clampVal(asciiClockFontIndex, 0, ASCII_CLOCK_FONT_COUNT - 1);
  timezoneIndex = clampVal(timezoneIndex, 0, TIMEZONE_COUNT - 1);
  if (!wifiEnabled) clockUseInternetTime = false;
  normalizeClockDate();
  invalidateBackgroundGradientCache();
  pixelFlowerGeometryDirty = true;
  settingsDirty = false;
}

void serviceSettingsPersistence(unsigned long now) {
  if (settingsDirty && now - settingsDirtyMs >= SETTINGS_SAVE_DELAY_MS) {
    savePersistentState();
    return;
  }
  if (now - lastSettingsSaveMs >= CLOCK_AUTOSAVE_INTERVAL_MS) {
    savePersistentState();
  }
}

// ===== part: aqlogic_kept =====
void noteFeedPelletSpawned() {
  bool crossedThreshold = false;
  if (cthulhuFeedPelletCount < CTHULHU_FEED_PELLET_THRESHOLD) {
    cthulhuFeedPelletCount++;
    crossedThreshold = (cthulhuFeedPelletCount >= CTHULHU_FEED_PELLET_THRESHOLD);
  }

  if (crossedThreshold) {
    markSettingsDirty();
  }

  if (cthulhuFeedPelletCount >= CTHULHU_FEED_PELLET_THRESHOLD && !octopus.active) {
    unsigned long soon = aquariumNowMs + (unsigned long)random(6000, 18000);
    if (octopus.nextSpawnMs == 0 || (long)(soon - octopus.nextSpawnMs) < 0) {
      octopus.nextSpawnMs = soon;
    }
  }
}

void spawnFlake(float x, float y) {
  for (int i = 0; i < MAX_FLAKES; i++) {
    if (!flakes[i].active) {
      flakes[i].active = true;
      flakes[i].x = x;
      flakes[i].y = y;
      flakes[i].vy = frand(22.0f, 48.0f);
      flakes[i].color = randomFoodColor();
      noteFeedPelletSpawned();
      return;
    }
  }
}

float randomFishDepthBrightness() {
  int roll = random(100);
  if (roll < 28) return frand(0.48f, 0.64f);
  if (roll < 70) return frand(0.66f, 0.84f);
  return frand(0.88f, 1.0f);
}

void refreshFishRenderColor(Fish& f) {
  f.renderColor = scaleRgb565(f.displayColor, f.depthBrightness);
}

void refreshFishDepth(Fish& f) {
  f.depthBrightness = randomFishDepthBrightness();
  refreshFishRenderColor(f);
}

void activateFish(Fish& f, bool activeNow) {
  f.active = activeNow;
  if (!activeNow) return;
  f.type = random(0, GLYPH_COUNT);
  int rightWidth = fishGlyphWidthRight[f.type];
  int leftWidth = fishGlyphWidthLeft[f.type];
  f.visualWidth = (rightWidth > leftWidth) ? rightWidth : leftWidth;
  if (f.visualWidth <= 0) f.visualWidth = (int)strlen(fishSpecies[f.type].right) * 12;
  f.displayColor = fishSpecies[f.type].baseColor;
  if (random(100) < 20) {
    f.displayColor = kAltFishColors[random(0, kAltFishColorCount)];
  }
  refreshFishDepth(f);
  f.x = frand(-42, SCREEN_W + 12);  // allow natural side entry
  f.y = frand(20, SEA_LEVEL_Y - 10);
  f.vx = frand(-1.0f, 1.0f);
  f.vy = frand(-0.5f, 0.5f);
  f.speed = frand(14.0f, 30.0f);
  f.phase = frand(0.0f, 6.28318f);
  f.wanderBias = frand(0.4f, 1.3f);
}

void applyFishPopulation() {
  fishTargetCount = clampVal(fishTargetCount, MIN_FISH, MAX_FISH);
  for (int i = 0; i < MAX_FISH_POOL; i++) {
    bool shouldBeActive = (i < fishTargetCount);
    if (shouldBeActive && !fishPool[i].active) activateFish(fishPool[i], true);
    if (!shouldBeActive && fishPool[i].active) fishPool[i].active = false;
  }
}

bool fishSpawnClear(int fishIndex, float x, float y, float minGapX, float minGapY) {
  Fish& f = fishPool[fishIndex];
  float centerX = x + f.visualWidth * 0.5f;
  float centerY = y + FISH_CENTER_Y_OFFSET;
  int fishCount = activeFishLimit();
  for (int i = 0; i < fishCount; ++i) {
    if (i == fishIndex || !fishPool[i].active) continue;
    Fish& other = fishPool[i];
    float otherCenterX = other.x + other.visualWidth * 0.5f;
    float otherCenterY = other.y + FISH_CENTER_Y_OFFSET;
    if (fabsf(otherCenterX - centerX) < minGapX && fabsf(otherCenterY - centerY) < minGapY) return false;
  }
  return true;
}

void spreadInitialFishLayout() {
  int fishCount = activeFishLimit();
  float minGapX = FISH_AVOID_RADIUS_X * 0.92f;
  float minGapY = FISH_AVOID_RADIUS_Y * 1.05f;
  for (int i = 0; i < fishCount; ++i) {
    Fish& f = fishPool[i];
    if (!f.active) continue;

    float bestX = f.x;
    float bestY = f.y;
    bool placed = false;
    for (int attempt = 0; attempt < 80; ++attempt) {
      float candidateX = frand(10.0f, SCREEN_W - f.visualWidth - 10.0f);
      float candidateY = frand(18.0f, SEA_LEVEL_Y - 18.0f);
      bestX = candidateX;
      bestY = candidateY;
      if (fishSpawnClear(i, candidateX, candidateY, minGapX, minGapY)) {
        placed = true;
        break;
      }
    }

    f.x = bestX;
    f.y = bestY;
    if (!placed) {
      f.y = clampVal(f.y + frand(-8.0f, 8.0f), 18.0f, (float)SEA_LEVEL_Y - 18.0f);
    }
    f.vx = (random(100) < 50) ? -1.0f : 1.0f;
    f.vy = frand(-0.22f, 0.22f);
  }
}

void respawnFishPopulation() {
  fishTargetCount = clampVal(fishTargetCount, MIN_FISH, MAX_FISH);
  int fishCount = activeFishLimit();
  for (int i = 0; i < MAX_FISH_POOL; ++i) {
    activateFish(fishPool[i], i < fishCount);
  }
  spreadInitialFishLayout();
}

void resetBubble(Bubble& b, bool spreadOut) {
  b.active = true;
  b.baseX = frand(8.0f, SCREEN_W - 8.0f);
  b.x = b.baseX;
  b.y = spreadOut ? frand(4.0f, SCREEN_H + 48.0f) : frand(SCREEN_H - 4.0f, SCREEN_H + 48.0f);
  b.vy = frand(12.0f, 28.0f);
  b.phase = frand(0.0f, 6.28318f);
  b.swayAmp = frand(2.0f, 7.0f);
  b.color = randomBubbleColor();
}

void applyBubblePopulation(bool spreadNew = false) {
  bubbleTargetCount = clampVal(bubbleTargetCount, MIN_BUBBLES, MAX_BUBBLES);
  for (int i = 0; i < MAX_BUBBLES; i++) {
    bool shouldBeActive = (i < bubbleTargetCount);
    if (shouldBeActive && !bubbles[i].active) resetBubble(bubbles[i], spreadNew);
    if (!shouldBeActive && bubbles[i].active) bubbles[i].active = false;
  }
}

void updateFlakes(float dt) {
  float t = aquariumTimeSec();
  for (int i = 0; i < MAX_FLAKES; i++) {
    if (!flakes[i].active) continue;
    flakes[i].y += flakes[i].vy * dt;
    flakes[i].x += sinf(t * 1.2f + i) * 8.0f * dt;
    if (flakes[i].y > SEA_LEVEL_Y) flakes[i].active = false;
  }
}

void updateBubbles(float dt) {
  float t = aquariumTimeSec();
  int bubbleCount = activeBubbleLimit();
  for (int i = 0; i < bubbleCount; i++) {
    if (!bubbles[i].active) continue;
    bubbles[i].y -= bubbles[i].vy * dt;
    bubbles[i].x = bubbles[i].baseX + sinf(t * 1.8f + bubbles[i].phase) * bubbles[i].swayAmp;
    if (bubbles[i].y < -10.0f) resetBubble(bubbles[i], false);
  }
}

int closestFlakeForFish(const Fish& f, float maxDist) {
  int best = -1;
  float bestD2 = maxDist * maxDist;
  for (int i = 0; i < MAX_FLAKES; i++) {
    if (!flakes[i].active) continue;
    float dx = flakes[i].x - f.x;
    float dy = flakes[i].y - f.y;
    float d2 = dx * dx + dy * dy;
    if (d2 < bestD2) {
      bestD2 = d2;
      best = i;
    }
  }
  return best;
}

void steerFishAwayFromOctopus(Fish& f, float fishCenterX, float fishCenterY, float dt) {
  if (!octopus.active) return;

  float dx = fishCenterX - octopus.x;
  float dy = fishCenterY - (octopus.y + OCTOPUS_CENTER_Y_OFFSET);
  float radiusX = octopus.cthulhu ? OCTOPUS_FISH_AVOID_RADIUS_X * CTHULHU_FISH_AVOID_RADIUS_SCALE
                                  : OCTOPUS_FISH_AVOID_RADIUS_X;
  float radiusY = octopus.cthulhu ? OCTOPUS_FISH_AVOID_RADIUS_Y * CTHULHU_FISH_AVOID_RADIUS_SCALE
                                  : OCTOPUS_FISH_AVOID_RADIUS_Y;
  float strength = octopus.cthulhu ? OCTOPUS_FISH_AVOID_STRENGTH * CTHULHU_FISH_AVOID_STRENGTH_SCALE
                                   : OCTOPUS_FISH_AVOID_STRENGTH;
  float sx = dx / radiusX;
  float sy = dy / radiusY;
  float scaledD2 = sx * sx + sy * sy;
  if (scaledD2 <= 0.0001f || scaledD2 >= 1.0f) return;

  float dist = sqrtf(dx * dx + dy * dy) + 0.0001f;
  float push = 1.0f - scaledD2;
  push *= push;
  f.vx += (dx / dist) * push * strength * dt;
  f.vy += (dy / dist) * push * strength * dt;
}

void steerFishAwayFromSeahorse(Fish& f, float fishCenterX, float fishCenterY, float dt) {
  if (!seahorse.active) return;

  float horseCenterX = seahorse.x + SEAHORSE_CENTER_X_OFFSET;
  float horseCenterY = seahorse.y + SEAHORSE_CENTER_Y_OFFSET;
  float dx = fishCenterX - horseCenterX;
  float dy = fishCenterY - horseCenterY;
  float sx = dx / SEAHORSE_FISH_AVOID_RADIUS_X;
  float sy = dy / SEAHORSE_FISH_AVOID_RADIUS_Y;
  float scaledD2 = sx * sx + sy * sy;
  if (scaledD2 <= 0.0001f || scaledD2 >= 1.0f) return;

  float dist = sqrtf(dx * dx + dy * dy) + 0.0001f;
  float push = 1.0f - scaledD2;
  push *= push;
  f.vx += (dx / dist) * push * SEAHORSE_FISH_AVOID_STRENGTH * dt;
  f.vy += (dy / dist) * push * SEAHORSE_FISH_AVOID_STRENGTH * dt;
}

void steerFishAwayFromSnail(Fish& f, float fishCenterX, float fishCenterY, float dt) {
  if (!snail.active) return;

  float snailCenterX = snail.x + SNAIL_CENTER_X_OFFSET;
  float snailCenterY = snail.y + SNAIL_CENTER_Y_OFFSET;
  float dx = fishCenterX - snailCenterX;
  float dy = fishCenterY - snailCenterY;
  float sx = dx / SNAIL_FISH_AVOID_RADIUS_X;
  float sy = dy / SNAIL_FISH_AVOID_RADIUS_Y;
  float scaledD2 = sx * sx + sy * sy;
  if (scaledD2 <= 0.0001f || scaledD2 >= 1.0f) return;

  float dist = sqrtf(dx * dx + dy * dy) + 0.0001f;
  float push = 1.0f - scaledD2;
  push *= push;
  f.vx += (dx / dist) * push * SNAIL_FISH_AVOID_STRENGTH * dt;
  f.vy += (dy / dist) * push * SNAIL_FISH_AVOID_STRENGTH * dt;
}

void steerFishAwayFromJellyfish(Fish& f, float fishCenterX, float fishCenterY, float dt) {
  if (!jellyfish.active) return;

  float jellyCenterX = jellyfish.x + JELLYFISH_CENTER_X_OFFSET;
  float jellyCenterY = jellyfish.y + JELLYFISH_CENTER_Y_OFFSET;
  float dx = fishCenterX - jellyCenterX;
  float dy = fishCenterY - jellyCenterY;
  float sx = dx / JELLYFISH_FISH_AVOID_RADIUS_X;
  float sy = dy / JELLYFISH_FISH_AVOID_RADIUS_Y;
  float scaledD2 = sx * sx + sy * sy;
  if (scaledD2 <= 0.0001f || scaledD2 >= 1.0f) return;

  float dist = sqrtf(dx * dx + dy * dy) + 0.0001f;
  float push = 1.0f - scaledD2;
  push *= push;
  f.vx += (dx / dist) * push * JELLYFISH_FISH_AVOID_STRENGTH * dt;
  f.vy += (dy / dist) * push * JELLYFISH_FISH_AVOID_STRENGTH * dt;
}

void keepFishOutsideOctopus(Fish& f) {
  if (!octopus.active) return;

  float fishCenterX = f.x + f.visualWidth * 0.5f;
  float fishCenterY = f.y + FISH_CENTER_Y_OFFSET;
  float octoCenterY = octopus.y + OCTOPUS_CENTER_Y_OFFSET;
  float dx = fishCenterX - octopus.x;
  float dy = fishCenterY - octoCenterY;
  float radiusX = octopus.cthulhu ? OCTOPUS_FISH_CLEAR_RADIUS_X * CTHULHU_FISH_CLEAR_RADIUS_SCALE
                                  : OCTOPUS_FISH_CLEAR_RADIUS_X;
  float radiusY = octopus.cthulhu ? OCTOPUS_FISH_CLEAR_RADIUS_Y * CTHULHU_FISH_CLEAR_RADIUS_SCALE
                                  : OCTOPUS_FISH_CLEAR_RADIUS_Y;
  float sx = dx / radiusX;
  float sy = dy / radiusY;
  float scaledD2 = sx * sx + sy * sy;
  if (scaledD2 >= 1.0f) return;

  if (scaledD2 <= 0.0001f) {
    dx = (f.vx >= 0.0f) ? 1.0f : -1.0f;
    dy = (f.vy >= 0.0f) ? 0.35f : -0.35f;
    scaledD2 = (dx / radiusX) * (dx / radiusX) + (dy / radiusY) * (dy / radiusY);
  }

  float scale = 1.0f / sqrtf(scaledD2);
  float targetCenterX = octopus.x + dx * scale;
  float targetCenterY = octoCenterY + dy * scale;
  f.x += (targetCenterX - fishCenterX) * 0.55f;
  f.y += (targetCenterY - fishCenterY) * 0.55f;
}

void keepFishOutsideSeahorse(Fish& f) {
  if (!seahorse.active) return;

  float fishCenterX = f.x + f.visualWidth * 0.5f;
  float fishCenterY = f.y + FISH_CENTER_Y_OFFSET;
  float horseCenterX = seahorse.x + SEAHORSE_CENTER_X_OFFSET;
  float horseCenterY = seahorse.y + SEAHORSE_CENTER_Y_OFFSET;
  float dx = fishCenterX - horseCenterX;
  float dy = fishCenterY - horseCenterY;
  float sx = dx / SEAHORSE_FISH_CLEAR_RADIUS_X;
  float sy = dy / SEAHORSE_FISH_CLEAR_RADIUS_Y;
  float scaledD2 = sx * sx + sy * sy;
  if (scaledD2 >= 1.0f) return;

  if (scaledD2 <= 0.0001f) {
    dx = (f.vx >= 0.0f) ? 1.0f : -1.0f;
    dy = (f.vy >= 0.0f) ? 0.35f : -0.35f;
    scaledD2 = (dx / SEAHORSE_FISH_CLEAR_RADIUS_X) * (dx / SEAHORSE_FISH_CLEAR_RADIUS_X) +
               (dy / SEAHORSE_FISH_CLEAR_RADIUS_Y) * (dy / SEAHORSE_FISH_CLEAR_RADIUS_Y);
  }

  float scale = 1.0f / sqrtf(scaledD2);
  float targetCenterX = horseCenterX + dx * scale;
  float targetCenterY = horseCenterY + dy * scale;
  f.x += (targetCenterX - fishCenterX) * 0.45f;
  f.y += (targetCenterY - fishCenterY) * 0.45f;
}

void keepFishOutsideSnail(Fish& f) {
  if (!snail.active) return;

  float fishCenterX = f.x + f.visualWidth * 0.5f;
  float fishCenterY = f.y + FISH_CENTER_Y_OFFSET;
  float snailCenterX = snail.x + SNAIL_CENTER_X_OFFSET;
  float snailCenterY = snail.y + SNAIL_CENTER_Y_OFFSET;
  float dx = fishCenterX - snailCenterX;
  float dy = fishCenterY - snailCenterY;
  float sx = dx / SNAIL_FISH_CLEAR_RADIUS_X;
  float sy = dy / SNAIL_FISH_CLEAR_RADIUS_Y;
  float scaledD2 = sx * sx + sy * sy;
  if (scaledD2 >= 1.0f) return;

  if (scaledD2 <= 0.0001f) {
    dx = (f.vx >= 0.0f) ? 1.0f : -1.0f;
    dy = -0.45f;
    scaledD2 = (dx / SNAIL_FISH_CLEAR_RADIUS_X) * (dx / SNAIL_FISH_CLEAR_RADIUS_X) +
               (dy / SNAIL_FISH_CLEAR_RADIUS_Y) * (dy / SNAIL_FISH_CLEAR_RADIUS_Y);
  }

  float scale = 1.0f / sqrtf(scaledD2);
  float targetCenterX = snailCenterX + dx * scale;
  float targetCenterY = snailCenterY + dy * scale;
  f.x += (targetCenterX - fishCenterX) * 0.35f;
  f.y += (targetCenterY - fishCenterY) * 0.55f;
}

void keepFishOutsideJellyfish(Fish& f) {
  if (!jellyfish.active) return;

  float fishCenterX = f.x + f.visualWidth * 0.5f;
  float fishCenterY = f.y + FISH_CENTER_Y_OFFSET;
  float jellyCenterX = jellyfish.x + JELLYFISH_CENTER_X_OFFSET;
  float jellyCenterY = jellyfish.y + JELLYFISH_CENTER_Y_OFFSET;
  float dx = fishCenterX - jellyCenterX;
  float dy = fishCenterY - jellyCenterY;
  float sx = dx / JELLYFISH_FISH_CLEAR_RADIUS_X;
  float sy = dy / JELLYFISH_FISH_CLEAR_RADIUS_Y;
  float scaledD2 = sx * sx + sy * sy;
  if (scaledD2 >= 1.0f) return;

  if (scaledD2 <= 0.0001f) {
    dx = (f.vx >= 0.0f) ? 1.0f : -1.0f;
    dy = (f.vy >= 0.0f) ? 0.55f : -0.55f;
    scaledD2 = (dx / JELLYFISH_FISH_CLEAR_RADIUS_X) * (dx / JELLYFISH_FISH_CLEAR_RADIUS_X) +
               (dy / JELLYFISH_FISH_CLEAR_RADIUS_Y) * (dy / JELLYFISH_FISH_CLEAR_RADIUS_Y);
  }

  float scale = 1.0f / sqrtf(scaledD2);
  float targetCenterX = jellyCenterX + dx * scale;
  float targetCenterY = jellyCenterY + dy * scale;
  f.x += (targetCenterX - fishCenterX) * 0.38f;
  f.y += (targetCenterY - fishCenterY) * 0.38f;
}

void updateFish(float dt) {
  const float t = aquariumTimeSec();
  int fishCount = activeFishLimit();
  float centerX[MAX_FISH_POOL];
  float centerY[MAX_FISH_POOL];
  for (int i = 0; i < fishCount; ++i) {
    Fish& f = fishPool[i];
    if (!f.active) continue;
    centerX[i] = f.x + f.visualWidth * 0.5f;
    centerY[i] = f.y + FISH_CENTER_Y_OFFSET;
  }

  for (int i = 0; i < fishCount; i++) {
    Fish& f = fishPool[i];
    if (!f.active) continue;

    // Wander behavior
    float wanderX = cosf(f.phase + t * 0.9f) * 0.45f * f.wanderBias;
    float wanderY = sinf(f.phase * 1.7f + t * 0.7f) * 0.22f;
    f.vx += wanderX * dt;
    f.vy += wanderY * dt;

    // Schooling with same type (alignment + cohesion)
    float avgVX = 0, avgVY = 0, cx = 0, cy = 0;
    int nearCount = 0;
    float repelX = 0.0f;
    float repelY = 0.0f;
    int repelCount = 0;
    float fCenterX = centerX[i];
    float fCenterY = centerY[i];
    if (fishAvoidanceEnabled()) {
      for (int j = 0; j < fishCount; j++) {
        if (i == j || !fishPool[j].active) continue;
        Fish& n = fishPool[j];

        float dx = n.x - f.x;
        float dy = n.y - f.y;
        if (n.type == f.type) {
          float d2 = dx * dx + dy * dy;
          if (d2 < 3600.0f) {  // within ~60px
            avgVX += n.vx;
            avgVY += n.vy;
            cx += n.x;
            cy += n.y;
            nearCount++;
          }
        }

        float sdx = centerX[j] - fCenterX;
        float sdy = centerY[j] - fCenterY;
        if (sdx > SCREEN_W * 0.5f) sdx -= SCREEN_W;
        if (sdx < -SCREEN_W * 0.5f) sdx += SCREEN_W;

        float sx = sdx / FISH_AVOID_RADIUS_X;
        float sy = sdy / FISH_AVOID_RADIUS_Y;
        float scaledD2 = sx * sx + sy * sy;
        if (scaledD2 > 0.0001f && scaledD2 < 1.0f) {
          float dist = sqrtf(sdx * sdx + sdy * sdy) + 0.0001f;
          float push = 1.0f - scaledD2;
          push *= push;
          repelX -= (sdx / dist) * push;
          repelY -= (sdy / dist) * push;
          repelCount++;
        }
      }
    }
    if (nearCount > 0) {
      avgVX /= nearCount;
      avgVY /= nearCount;
      cx /= nearCount;
      cy /= nearCount;
      f.vx += (avgVX - f.vx) * 0.45f * dt;
      f.vy += (avgVY - f.vy) * 0.25f * dt;
      f.vx += (cx - f.x) * 0.0018f;
      f.vy += (cy - f.y) * 0.0012f;
    }

    // Feed-seeking behavior
    int fi = closestFlakeForFish(f, 140.0f);
    if (fi >= 0) {
      float dx = flakes[fi].x - f.x;
      float dy = flakes[fi].y - f.y;
      float d = sqrtf(dx * dx + dy * dy) + 0.0001f;
      f.vx += (dx / d) * 0.95f * dt;
      f.vy += (dy / d) * 0.95f * dt;
      if (d < 8.0f) flakes[fi].active = false;  // "eat"
    }

    if (fishAvoidanceEnabled()) {
      steerFishAwayFromOctopus(f, fCenterX, fCenterY, dt);
      steerFishAwayFromSeahorse(f, fCenterX, fCenterY, dt);
      steerFishAwayFromSnail(f, fCenterX, fCenterY, dt);
      steerFishAwayFromJellyfish(f, fCenterX, fCenterY, dt);
    }

    // Gentle steering separation, not hard collision.
    if (repelCount > 0) {
      f.vx += (repelX / repelCount) * FISH_AVOID_STRENGTH * dt;
      f.vy += (repelY / repelCount) * FISH_AVOID_STRENGTH * dt;
    }

    // Vertical edge avoidance only (horizontal uses wraparound)
    if (f.y < 18) f.vy += 0.8f * dt;
    if (f.y > SEA_LEVEL_Y - 8) f.vy -= 0.8f * dt;

    // Normalize velocity and apply speed
    float mag = sqrtf(f.vx * f.vx + f.vy * f.vy);
    if (mag < 0.0001f) {
      f.vx = 1.0f;
      f.vy = 0.0f;
      mag = 1.0f;
    }
    f.vx /= mag;
    f.vy /= mag;

    float fishSpeed = f.speed + sinf(t * 3.2f + f.phase) * 4.0f;
    f.x += f.vx * fishSpeed * dt;
    f.y += f.vy * fishSpeed * dt;
    if (fishAvoidanceEnabled()) {
      keepFishOutsideOctopus(f);
      keepFishOutsideSeahorse(f);
      keepFishOutsideSnail(f);
      keepFishOutsideJellyfish(f);
    }

    // Horizontal wrap keeps fish flowing off-screen and re-entering smoothly
    int w = fishVisualWidth(f);
    float wrapPad = (float)w + 10.0f;
    if (f.x > SCREEN_W + wrapPad) {
      f.x = -wrapPad;
      refreshFishDepth(f);
    }
    if (f.x < -wrapPad) {
      f.x = SCREEN_W + wrapPad;
      refreshFishDepth(f);
    }
    f.y = clampVal(f.y, 14.0f, (float)SEA_LEVEL_Y - 6.0f);
  }
}

bool timeReached(unsigned long now, unsigned long target) {
  return (long)(now - target) >= 0;
}

void startAutoFeedSprinkle(unsigned long now) {
  autoFeedSprinkleActive = true;
  autoFeedSprinkleLeftToRight = (random(100) < 50);
  autoFeedSprinkleDropped = 0;
  autoFeedSprinkleNextMs = now;

  unsigned long duration = random(AUTO_FEED_SPRINKLE_MIN_MS, AUTO_FEED_SPRINKLE_MAX_MS + 1);
  autoFeedSprinkleIntervalMs = duration / (AUTO_FEED_SPRINKLE_COUNT - 1);
  if (autoFeedSprinkleIntervalMs < 120UL) autoFeedSprinkleIntervalMs = 120UL;
}

void dropNextAutoFeedFlake() {
  float progress = (float)autoFeedSprinkleDropped / (float)(AUTO_FEED_SPRINKLE_COUNT - 1);
  if (!autoFeedSprinkleLeftToRight) progress = 1.0f - progress;

  float x = 16.0f + progress * ((float)SCREEN_W - 32.0f) + frand(-5.0f, 5.0f);
  float y = frand(8.0f, 22.0f);
  spawnFlake(clampVal(x, 10.0f, (float)SCREEN_W - 10.0f), y);
}

void serviceAutoFeedSprinkle(unsigned long now) {
  if (!autoFeedSprinkleActive || !timeReached(now, autoFeedSprinkleNextMs)) return;

  dropNextAutoFeedFlake();
  autoFeedSprinkleDropped++;
  if (autoFeedSprinkleDropped >= AUTO_FEED_SPRINKLE_COUNT) {
    autoFeedSprinkleActive = false;
    return;
  }

  autoFeedSprinkleNextMs += autoFeedSprinkleIntervalMs;
  if (timeReached(now, autoFeedSprinkleNextMs)) {
    autoFeedSprinkleNextMs = now + autoFeedSprinkleIntervalMs;
  }
}

void serviceAutoFeed(unsigned long now) {
  int frequency = normalizeAutoFeedFrequency(autoFeedFrequency);
  if (frequency <= 0) {
    nextAutoFeedMs = 0;
    autoFeedSprinkleActive = false;
    return;
  }

  serviceAutoFeedSprinkle(now);

  if (nextAutoFeedMs == 0) {
    nextAutoFeedMs = now + autoFeedIntervalMs();
    return;
  }

  if (!timeReached(now, nextAutoFeedMs)) return;
  startAutoFeedSprinkle(now);
  serviceAutoFeedSprinkle(now);

  unsigned long interval = autoFeedIntervalMs();
  nextAutoFeedMs += interval;
  if (timeReached(now, nextAutoFeedMs)) nextAutoFeedMs = now + interval;
}

void scheduleOctopusSpawn(unsigned long now) {
  octopus.nextSpawnMs = now + octopusSpawnIntervalMs();
}

bool shouldSpawnCthulhu() {
  if (cthulhuFeedPelletCount >= CTHULHU_FEED_PELLET_THRESHOLD) {
    cthulhuFeedPelletCount = 0;
    markSettingsDirty();
    return true;
  }

  if (random(100) < CTHULHU_SPAWN_CHANCE_PERCENT) {
    cthulhuFeedPelletCount = 0;
    markSettingsDirty();
    return true;
  }

  return false;
}

void spawnOctopus(unsigned long now) {
  bool fromLeft = (random(100) < 50);
  octopus.active = true;
  octopus.cthulhu = shouldSpawnCthulhu();
  octopus.vx = fromLeft ? frand(4.5f, 8.0f) : -frand(4.5f, 8.0f);
  octopus.x = fromLeft ? -OCTOPUS_EXIT_PAD : (SCREEN_W + OCTOPUS_EXIT_PAD);
  octopus.baseY = frand(36.0f, (float)SEA_LEVEL_Y - 48.0f);
  octopus.y = octopus.baseY;
  octopus.phase = frand(0.0f, 6.28318f);
  octopus.colorPhase = frand(0.0f, 6.28318f);
  scheduleOctopusSpawn(now);
}

void spawnOctopusAtCenter(unsigned long now) {
  octopus.active = true;
  octopus.cthulhu = false;
  octopus.x = SCREEN_W * 0.5f;
  octopus.baseY = SEA_LEVEL_Y * 0.55f;
  octopus.y = octopus.baseY;
  octopus.vx = (random(100) < 50) ? -frand(3.8f, 6.5f) : frand(3.8f, 6.5f);
  octopus.phase = frand(0.0f, 6.28318f);
  octopus.colorPhase = frand(0.0f, 6.28318f);
  scheduleOctopusSpawn(now);
}

void spawnCthulhuAtCenter(unsigned long now) {
  octopus.active = true;
  octopus.cthulhu = true;
  octopus.x = SCREEN_W * 0.5f;
  octopus.baseY = SEA_LEVEL_Y * 0.52f;
  octopus.y = octopus.baseY;
  octopus.vx = (random(100) < 50) ? -frand(3.4f, 5.8f) : frand(3.4f, 5.8f);
  octopus.phase = frand(0.0f, 6.28318f);
  octopus.colorPhase = frand(0.0f, 6.28318f);
  scheduleOctopusSpawn(now);
}

void updateOctopus(unsigned long now, float dt) {
  if (!octopus.active) {
    if (octopus.nextSpawnMs == 0) {
      scheduleOctopusSpawn(now);
    } else if (timeReached(now, octopus.nextSpawnMs)) {
      spawnOctopus(now);
    }
    return;
  }

  float t = now * 0.001f;
  octopus.x += octopus.vx * dt;
  octopus.y = octopus.baseY + sinf(t * 0.45f + octopus.phase) * 6.0f;
  if ((octopus.vx > 0.0f && octopus.x > SCREEN_W + OCTOPUS_EXIT_PAD) ||
      (octopus.vx < 0.0f && octopus.x < -OCTOPUS_EXIT_PAD)) {
    octopus.active = false;
  }
}

void scheduleSeahorseSpawn(unsigned long now) {
  seahorse.nextSpawnMs = now + seahorseSpawnIntervalMs();
}

void spawnSeahorse(unsigned long now) {
  bool fromLeft = (random(100) < 50);
  seahorse.active = true;
  seahorse.facingRight = fromLeft;
  seahorse.vx = fromLeft ? frand(1.6f, 2.9f) * SEAHORSE_SPEED_BOOST
                          : -frand(1.6f, 2.9f) * SEAHORSE_SPEED_BOOST;
  seahorse.x = fromLeft ? -SEAHORSE_EXIT_PAD : (SCREEN_W + SEAHORSE_EXIT_PAD);
  seahorse.baseY = frand(34.0f, (float)SEA_LEVEL_Y - 56.0f);
  seahorse.y = seahorse.baseY;
  seahorse.phase = frand(0.0f, 6.28318f);
  seahorse.finPhase = frand(0.0f, 6.28318f);
  scheduleSeahorseSpawn(now);
}

void spawnSeahorseAtCenter(unsigned long now) {
  seahorse.active = true;
  seahorse.facingRight = (random(100) < 50);
  seahorse.x = SCREEN_W * 0.5f - 16.0f;
  seahorse.baseY = SEA_LEVEL_Y * 0.46f;
  seahorse.y = seahorse.baseY;
  seahorse.vx = seahorse.facingRight ? frand(1.4f, 2.4f) * SEAHORSE_SPEED_BOOST
                                      : -frand(1.4f, 2.4f) * SEAHORSE_SPEED_BOOST;
  seahorse.phase = frand(0.0f, 6.28318f);
  seahorse.finPhase = frand(0.0f, 6.28318f);
  scheduleSeahorseSpawn(now);
}

void updateSeahorse(unsigned long now, float dt) {
  if (!seahorse.active) {
    if (seahorse.nextSpawnMs == 0) {
      scheduleSeahorseSpawn(now);
    } else if (timeReached(now, seahorse.nextSpawnMs)) {
      spawnSeahorse(now);
    }
    return;
  }

  float t = now * 0.001f;
  float pulse = 1.0f + sinf(t * 0.55f + seahorse.phase) * 0.18f;
  seahorse.x += seahorse.vx * pulse * dt;
  seahorse.y = seahorse.baseY + sinf(t * 0.82f + seahorse.phase) * 4.5f +
               sinf(t * 2.15f + seahorse.phase * 1.7f) * 0.9f;

  if ((seahorse.vx > 0.0f && seahorse.x > SCREEN_W + SEAHORSE_EXIT_PAD) ||
      (seahorse.vx < 0.0f && seahorse.x < -SEAHORSE_EXIT_PAD)) {
    seahorse.active = false;
  }
}

void scheduleSnailSpawn(unsigned long now) {
  unsigned long interval = snailSpawnIntervalMs();
  snail.nextSpawnMs = (interval > 0) ? now + interval : 0;
}

void spawnSnail(unsigned long now) {
  bool fromLeft = (random(100) < 50);
  snail.active = true;
  snail.facingRight = fromLeft;
  snail.vx = fromLeft ? frand(2.2f, 3.7f) : -frand(2.2f, 3.7f);
  snail.x = fromLeft ? -SNAIL_EXIT_PAD : (SCREEN_W + SNAIL_EXIT_PAD);
  snail.y = SCREEN_H - 15.0f;
  snail.phase = frand(0.0f, 6.28318f);
  scheduleSnailSpawn(now);
}

void spawnSnailAtCenter(unsigned long now) {
  snail.active = true;
  snail.facingRight = (random(100) < 50);
  snail.x = SCREEN_W * 0.5f - SNAIL_CENTER_X_OFFSET;
  snail.y = SCREEN_H - 15.0f;
  snail.vx = snail.facingRight ? frand(2.0f, 3.4f) : -frand(2.0f, 3.4f);
  snail.phase = frand(0.0f, 6.28318f);
  scheduleSnailSpawn(now);
}

void updateSnail(unsigned long now, float dt) {
  if (!snail.active) {
    if (normalizeCreatureFrequency(snailFrequency) <= 0) {
      snail.nextSpawnMs = 0;
      return;
    }
    if (snail.nextSpawnMs == 0) {
      scheduleSnailSpawn(now);
    } else if (timeReached(now, snail.nextSpawnMs)) {
      spawnSnail(now);
    }
    return;
  }

  float t = now * 0.001f;
  float scoot = 1.0f + sinf(t * 2.2f + snail.phase) * 0.18f;
  snail.x += snail.vx * scoot * dt;
  snail.y = SCREEN_H - 15.0f;

  if ((snail.vx > 0.0f && snail.x > SCREEN_W + SNAIL_EXIT_PAD) ||
      (snail.vx < 0.0f && snail.x < -SNAIL_EXIT_PAD)) {
    snail.active = false;
  }
}

void scheduleJellyfishSpawn(unsigned long now) {
  unsigned long interval = jellyfishSpawnIntervalMs();
  jellyfish.nextSpawnMs = (interval > 0) ? now + interval : 0;
}

void spawnJellyfish(unsigned long now) {
  bool fromLeft = (random(100) < 50);
  jellyfish.active = true;
  jellyfish.x = fromLeft ? -34.0f : (SCREEN_W + 34.0f);
  jellyfish.baseY = SEA_LEVEL_Y * 0.48f;
  jellyfish.y = jellyfish.baseY;
  jellyfish.vx = fromLeft ? frand(2.2f, 4.0f) : -frand(2.2f, 4.0f);
  jellyfish.phase = frand(0.0f, 6.28318f);
  scheduleJellyfishSpawn(now);
}

void spawnJellyfishAtCenter(unsigned long now) {
  jellyfish.active = true;
  jellyfish.x = SCREEN_W * 0.5f - JELLYFISH_CENTER_X_OFFSET;
  jellyfish.baseY = SEA_LEVEL_Y * 0.48f;
  jellyfish.y = jellyfish.baseY;
  jellyfish.vx = (random(100) < 50) ? -frand(2.2f, 4.0f) : frand(2.2f, 4.0f);
  jellyfish.phase = frand(0.0f, 6.28318f);
  scheduleJellyfishSpawn(now);
}

void updateJellyfish(unsigned long now, float dt) {
  if (!jellyfish.active) {
    if (normalizeCreatureFrequency(jellyfishFrequency) <= 0) {
      jellyfish.nextSpawnMs = 0;
      return;
    }
    if (jellyfish.nextSpawnMs == 0) {
      scheduleJellyfishSpawn(now);
    } else if (timeReached(now, jellyfish.nextSpawnMs)) {
      spawnJellyfish(now);
    }
    return;
  }

  float t = now * 0.001f;
  float pulseWave = sinf(t * 1.55f + jellyfish.phase);
  float pulse = pulseWave > 0.0f ? pulseWave * pulseWave : 0.0f;
  jellyfish.x += (jellyfish.vx + sinf(t * 0.38f + jellyfish.phase) * 2.0f) * dt;
  jellyfish.baseY -= pulse * 3.2f * dt;
  jellyfish.baseY += sinf(t * 0.22f + jellyfish.phase) * 0.42f * dt;
  jellyfish.y = jellyfish.baseY + sinf(t * 0.72f + jellyfish.phase) * 5.0f - pulse * 2.5f;
  jellyfish.y = clampVal(jellyfish.y, 24.0f, (float)SEA_LEVEL_Y - 34.0f);

  if (jellyfish.x < -34.0f || jellyfish.x > SCREEN_W + 34.0f || jellyfish.baseY < 10.0f) {
    jellyfish.active = false;
  }
}

void keepVisitorsSeparated() {
  if (!octopus.active || !seahorse.active) return;

  float octoCenterX = octopus.x;
  float octoCenterY = octopus.y + OCTOPUS_CENTER_Y_OFFSET;
  float horseCenterX = seahorse.x + SEAHORSE_CENTER_X_OFFSET;
  float horseCenterY = seahorse.y + SEAHORSE_CENTER_Y_OFFSET;
  float dx = horseCenterX - octoCenterX;
  float dy = horseCenterY - octoCenterY;
  float sx = dx / VISITOR_CLEAR_RADIUS_X;
  float sy = dy / VISITOR_CLEAR_RADIUS_Y;
  float scaledD2 = sx * sx + sy * sy;
  if (scaledD2 >= 1.0f) return;

  if (scaledD2 <= 0.0001f) {
    dx = (seahorse.vx >= octopus.vx) ? 1.0f : -1.0f;
    dy = 0.35f;
    scaledD2 = (dx / VISITOR_CLEAR_RADIUS_X) * (dx / VISITOR_CLEAR_RADIUS_X) +
               (dy / VISITOR_CLEAR_RADIUS_Y) * (dy / VISITOR_CLEAR_RADIUS_Y);
  }

  float scale = 1.0f / sqrtf(scaledD2);
  float targetHorseCenterX = octoCenterX + dx * scale;
  float targetHorseCenterY = octoCenterY + dy * scale;
  float pushX = (targetHorseCenterX - horseCenterX) * 0.18f;
  float pushY = (targetHorseCenterY - horseCenterY) * 0.22f;

  seahorse.x += pushX;
  seahorse.baseY = clampVal(seahorse.baseY + pushY, 24.0f, (float)SEA_LEVEL_Y - 54.0f);
  seahorse.y += pushY;
  octopus.x -= pushX * 0.55f;
  octopus.baseY = clampVal(octopus.baseY - pushY * 0.55f, 28.0f, (float)SEA_LEVEL_Y - 44.0f);
  octopus.y -= pushY * 0.55f;
}

// ===== part: drawing_kept =====
void seaweedPointAt(float u, float bx, int y0, float bladeHeight, float sway, float tSec, int bladeIndex, float& x, float& y) {
  u = clampVal(u, 0.0f, 1.0f);
  float bodyWave = wrappedSinf(tSec * (1.05f + bladeIndex * 0.025f) * seaweedSwaySpeed - u * 5.1f + bladeIndex * 0.72f);
  float ripple = wrappedSinf(tSec * 0.72f * seaweedSwaySpeed + u * 9.0f + bladeIndex * 1.31f);
  float bend = sway * u * (0.20f + u * 0.80f);
  float travel = bodyWave * (1.5f + bladeHeight * 0.055f) * u * u;
  float detail = ripple * 1.2f * u;
  x = bx + bend + travel + detail;
  y = y0 - bladeHeight * u;
}

void drawSeaweedBranches(TFT_eSprite& s, int bladeIndex, float bladeHeight, float sway, float tSec,
                         float bx, int y0) {
  int branchCount = clampVal((int)(bladeHeight / 14.0f), 2, 5);
  for (int b = 0; b < branchCount; ++b) {
    float u = 0.30f + b * 0.14f + ((bladeIndex + b) % 3) * 0.018f;
    if (u > 0.88f) u = 0.88f;

    float px, py;
    seaweedPointAt(u, bx, y0, bladeHeight, sway, tSec, bladeIndex, px, py);

    float side = ((bladeIndex + b) & 1) ? 1.0f : -1.0f;
    float branchLen = 5.5f + ((bladeIndex * 3 + b * 5) % 5);
    float branchWiggle = wrappedSinf(tSec * (1.1f + bladeIndex * 0.03f) * seaweedSwaySpeed + bladeIndex + b * 1.7f) * 1.2f;
    int ex = (int)(px + side * (branchLen * 0.58f + fabsf(sway) * 0.05f) + branchWiggle);
    int ey = (int)(py - branchLen * 0.78f);
    uint16_t color = (b & 1) ? TFT_DARKGREEN : TFT_GREEN;
    s.drawLine((int)px, (int)py, ex, ey, color);
  }
}

void drawSeaweed(TFT_eSprite& s, float tSec) {
  static const int roots = 12;
  static bool cached = false;
  static float baseX[roots];
  static float amp[roots];
  static float heightNoise[roots];

  if (!cached) {
    for (int i = 0; i < roots; ++i) {
      baseX[i] = 10 + i * (SCREEN_W - 20.0f) / (roots - 1);
      amp[i] = 5.0f + (i % 4) * 2.0f;
      heightNoise[i] = sinf(i * 2.173f + 0.61f);
    }
    cached = true;
  }

  for (int i = 0; i < roots; i++) {
    float bx = baseX[i];
    float sway = wrappedSinf(tSec * (0.8f + 0.09f * i) * seaweedSwaySpeed + i * 0.7f) * amp[i];
    float heightVariation = 1.0f + seaweedLengthRandomness * heightNoise[i];
    float bladeHeight = clampVal(32.0f * seaweedLength * heightVariation, 18.0f, 72.0f);
    int y0 = SCREEN_H - 2;

    float prevX, prevY;
    prevX = bx;
    prevY = y0;
    const int segments = 7;
    for (int seg = 1; seg <= segments; ++seg) {
      float u = (float)seg / segments;
      float x, y;
      seaweedPointAt(u, bx, y0, bladeHeight, sway, tSec, i, x, y);
      uint16_t color = (u < 0.38f) ? TFT_DARKGREEN : ((u < 0.76f) ? TFT_GREEN : TFT_GREENYELLOW);
      s.drawLine((int)prevX, (int)prevY, (int)x, (int)y, color);
      if (u < 0.78f) {
        s.drawLine((int)prevX + 1, (int)prevY, (int)x + 1, (int)y, TFT_DARKGREEN);
      }
      prevX = x;
      prevY = y;
    }
    drawSeaweedBranches(s, i, bladeHeight, sway, tSec, bx, y0);
  }
}

void drawFlakes(TFT_eSprite& s) {
  s.setTextSize(1);
  s.setTextDatum(MC_DATUM);
  for (int i = 0; i < MAX_FLAKES; i++) {
    if (!flakes[i].active) continue;
    s.setTextColor(flakes[i].color);
    s.drawString("*", (int)flakes[i].x, (int)flakes[i].y);
  }
}

void drawBubbles(TFT_eSprite& s) {
  s.setTextSize(1);
  s.setTextDatum(MC_DATUM);
  int bubbleCount = activeBubbleLimit();
  for (int i = 0; i < bubbleCount; i++) {
    if (!bubbles[i].active) continue;
    s.setTextColor(bubbles[i].color);
    s.drawString("o", (int)bubbles[i].x, (int)bubbles[i].y);
  }
}

void drawFish(TFT_eSprite& s) {
  s.setTextSize(1);
  s.setTextDatum(TL_DATUM);
  const float t = aquariumTimeSec();
  const float waveBase = t * FISH_SWIM_WAVE_SPEED;
  static const float waveStepSin = sinf(FISH_SWIM_WAVE_SPACING);
  static const float waveStepCos = cosf(FISH_SWIM_WAVE_SPACING);
  int fishCount = activeFishLimit();
  for (int i = 0; i < fishCount; i++) {
    Fish& f = fishPool[i];
    if (!f.active) continue;
    const char* txt = fishGlyphDrawing(f);
    const int16_t* glyphOffsets = fishGlyphOffsets(f);
    uint8_t len = fishGlyphLength(f);
    float waveAngle = waveBase + f.phase;
    float wave = sinf(waveAngle);
    float waveCos = cosf(waveAngle);
    s.setTextColor(f.renderColor);
    for (uint8_t c = 0; c < len; ++c) {
      if (txt[c] != ' ') {
        float yOffset = wave * FISH_SWIM_WAVE_AMPLITUDE;
        int charX = (int)f.x + glyphOffsets[c];
        int charY = (int)f.y + (int)(yOffset + ((yOffset >= 0.0f) ? 0.5f : -0.5f));
        s.drawChar((uint16_t)txt[c], charX, charY);
      }

      float nextWave = wave * waveStepCos + waveCos * waveStepSin;
      waveCos = waveCos * waveStepCos - wave * waveStepSin;
      wave = nextWave;
    }
  }
}

uint16_t octopusColor(float tSec) {
  int r = 205 + (int)(42.0f * sinf(tSec * 0.18f + octopus.colorPhase));
  int g = 78 + (int)(38.0f * sinf(tSec * 0.13f + octopus.colorPhase + 2.1f));
  int b = 178 + (int)(58.0f * sinf(tSec * 0.16f + octopus.colorPhase + 4.2f));
  return rgb565From888(r, g, b);
}

uint16_t cthulhuColor(float tSec) {
  int r = 8 + (int)(5.0f * sinf(tSec * 0.10f + octopus.colorPhase));
  int g = 82 + (int)(24.0f * sinf(tSec * 0.13f + octopus.colorPhase + 1.7f));
  int b = 34 + (int)(10.0f * sinf(tSec * 0.09f + octopus.colorPhase + 3.1f));
  return rgb565From888(r, g, b);
}

void drawOctopusGlyph(TFT_eSprite& s, const char* glyph, int x, int y) {
  if (!glyph || glyph[0] == '\0') return;
  s.drawChar((uint16_t)glyph[0], x, y);
}

void drawCthulhu(TFT_eSprite& s, float t, int cx, int cy) {
  s.setTextColor(cthulhuColor(t));
  s.setTextDatum(MC_DATUM);
  int tentacleFrame = ((int)(t * 3.0f + octopus.phase) & 1);
  int headBob = (int)(sinf(t * 1.1f + octopus.phase) * 1.4f);
  int tentacleBob = (int)(sinf(t * 1.8f + octopus.phase) * 1.6f);
  const char* tentacles = tentacleFrame ? "((()))" : "(()())";
  s.drawString("(;,,;)", cx, cy + headBob);
  s.drawString(tentacles, cx, cy + 15 + tentacleBob);
  s.setTextDatum(TL_DATUM);
}

void drawOctopus(TFT_eSprite& s) {
  if (!octopus.active) return;
  float t = aquariumTimeSec();
  int cx = (int)octopus.x;
  int cy = (int)octopus.y;

  s.setTextSize(1);
  s.setTextDatum(TL_DATUM);

  if (octopus.cthulhu) {
    drawCthulhu(s, t, cx, cy);
    return;
  }

  s.setTextColor(octopusColor(t));

  float topWave = sinf(t * 1.25f + octopus.phase) * 1.4f;
  drawOctopusGlyph(s, "(", cx - 13, cy + (int)topWave);
  drawOctopusGlyph(s, ".", cx - 3, cy - 5);
  drawOctopusGlyph(s, ".", cx + 7, cy - 5);
  drawOctopusGlyph(s, ")", cx + 16, cy - (int)topWave);

  static const char* tentacleGlyphs[] = {"(", "(", "(", ")", ")", ")"};
  static const int tentacleX[] = {-24, -16, -8, 2, 10, 18};
  for (int i = 0; i < 6; ++i) {
    float wave = sinf(t * 1.75f + octopus.phase + i * 0.72f);
    int x = cx + tentacleX[i] + (int)(wave * 1.4f);
    int y = cy + 13 + (int)(wave * 2.2f);
    drawOctopusGlyph(s, tentacleGlyphs[i], x, y);
  }
}

uint16_t seahorseColor(float tSec) {
  int r = 238 + (int)(12.0f * sinf(tSec * 0.11f + seahorse.phase));
  int g = 142 + (int)(18.0f * sinf(tSec * 0.16f + seahorse.phase + 1.4f));
  int b = 48 + (int)(12.0f * sinf(tSec * 0.13f + seahorse.phase + 2.8f));
  return rgb565From888(r, g, b);
}

char mirrorSeahorseGlyph(char glyph) {
  switch (glyph) {
    case '/': return '\\';
    case '\\': return '/';
    case '[': return ']';
    case ']': return '[';
    case '(': return ')';
    case ')': return '(';
    case '<': return '>';
    case '>': return '<';
    default: return glyph;
  }
}

void drawSeahorseGlyph(TFT_eSprite& s, char glyph, int x, int y) {
  s.drawChar((uint16_t)glyph, x, y);
}

void drawSeahorse(TFT_eSprite& s) {
  if (!seahorse.active) return;
  static const char* seahorseLeftRows[] = {
      "  ^^  ",
      " / o) ",
      "[__-/ ",
      "  /|  ",
      " / |  ",
      " \\ |  ",
      "  ( ) ",
      "  \\_/ ",
  };
  static const int SEAHORSE_ART_ROWS = sizeof(seahorseLeftRows) / sizeof(seahorseLeftRows[0]);
  static const int SEAHORSE_ART_COLS = 6;
  static const int SEAHORSE_CELL_W = 5;
  static const int SEAHORSE_ROW_H = 6;

  float t = aquariumTimeSec();
  int x = (int)seahorse.x;
  int y = (int)seahorse.y;
  int sway = (int)(sinf(t * 1.15f + seahorse.phase) * 1.2f);
  int finFlutter = (int)(sinf(t * 10.0f + seahorse.finPhase) * 1.2f);
  const char* finGlyph = (sinf(t * 12.0f + seahorse.finPhase) > 0.0f) ? "~" : "-";

  s.setTextSize(1);
  s.setTextFont(1);
  s.setTextDatum(TL_DATUM);
  s.setTextColor(seahorseColor(t));

  for (int row = 0; row < SEAHORSE_ART_ROWS; ++row) {
    const char* line = seahorseLeftRows[row];
    int len = strlen(line);
    int rowSway = (row >= 1 && row <= 3) ? sway : 0;
    for (int col = 0; col < SEAHORSE_ART_COLS; ++col) {
      char glyph = (col < len) ? line[col] : ' ';
      if (glyph == ' ') continue;
      int drawCol = col;
      if (seahorse.facingRight) {
        drawCol = SEAHORSE_ART_COLS - 1 - col;
        glyph = mirrorSeahorseGlyph(glyph);
      }
      drawSeahorseGlyph(s, glyph, x + drawCol * SEAHORSE_CELL_W + rowSway, y + row * SEAHORSE_ROW_H);
    }
  }

  s.setTextColor(rgb565From888(255, 188, 82));
  int finX = seahorse.facingRight ? x + 5 + finFlutter : x + 20 + finFlutter;
  s.drawString(finGlyph, finX, y + 24);
  s.setTextFont(2);
}

uint16_t snailColor(float tSec) {
  int r = 228 + (int)(14.0f * sinf(tSec * 0.16f + snail.phase));
  int g = 116 + (int)(16.0f * sinf(tSec * 0.13f + snail.phase + 1.3f));
  int b = 34 + (int)(8.0f * sinf(tSec * 0.11f + snail.phase + 2.6f));
  return rgb565From888(r, g, b);
}

void drawSnail(TFT_eSprite& s) {
  if (!snail.active) return;

  float t = aquariumTimeSec();
  bool stalkFrame = sinf(t * 1.6f + snail.phase) > 0.0f;
  int x = (int)snail.x;
  int y = (int)snail.y;

#if 0
  // Previous multi-line snail kept for quick revival if we want to revisit it.
  static const char* snailRightRows[2][3] = {
      {" __ |/", "(@l)o", "`~~~~'"},
      {" __ /|", "(@l)o", "`~--~'"},
  };
  static const char* snailLeftRows[2][3] = {
      {"\\| __", "o(l@)", "'~~~~`"},
      {"|\\ __", "o(l@)", "'~--~`"},
  };
#endif

  s.setTextSize(1);
  s.setTextFont(2);
  s.setTextDatum(TL_DATUM);
  s.setTextColor(snailColor(t));

  if (snail.facingRight) {
    s.drawString("@", x, y);
    s.drawString(stalkFrame ? "/" : "|", x + 11, y);
  } else {
    s.drawString(stalkFrame ? "\\" : "|", x, y);
    s.drawString("@", x + 7, y);
  }
}

uint16_t jellyfishColor(float tSec) {
  int r = 92 + (int)(20.0f * sinf(tSec * 0.12f + jellyfish.phase));
  int g = 188 + (int)(24.0f * sinf(tSec * 0.16f + jellyfish.phase + 1.2f));
  int b = 236 + (int)(16.0f * sinf(tSec * 0.10f + jellyfish.phase + 2.5f));
  return rgb565From888(r, g, b);
}

void drawJellyfish(TFT_eSprite& s) {
  if (!jellyfish.active) return;

  static const char* jellyRows[2][4] = {
      {" .-. ", "(   )", " \\|/ ", " /~\\ "},
      {" _-_ ", "(   )", "  |  ", " ~ ~ "},
  };

  float t = aquariumTimeSec();
  int frame = ((int)(t * 1.05f + jellyfish.phase) & 1);
  int x = (int)jellyfish.x;
  int y = (int)jellyfish.y;
  int tentacleSway = (int)(sinf(t * 1.45f + jellyfish.phase) * 1.4f);

  s.setTextSize(1);
  s.setTextFont(1);
  s.setTextDatum(TL_DATUM);
  s.setTextColor(jellyfishColor(t));

  const char* const* rows = jellyRows[frame];
  s.drawString(rows[0], x, y);
  s.drawString(rows[1], x, y + 7);
  s.drawString(rows[2], x + tentacleSway, y + 15);
  s.drawString(rows[3], x - tentacleSway, y + 23);
  s.setTextFont(2);
}

void drawAsciiClockBackground(TFT_eSprite& s) {
  if (!clockVisible || clockDisplayStyle != CLOCK_STYLE_ASCII) return;

  char timeText[16];
  formatClockTimeOnly(timeText, sizeof(timeText), false);
  const AsciiClockFont& font = currentAsciiClockFont();
  int artCols = asciiClockTextCols(timeText, font);
  int artPixelW = artCols * ASCII_CLOCK_CHAR_W;
  int x = (SCREEN_W - artPixelW) / 2;
  if (x < 0) x = 0;
  int y = ASCII_CLOCK_Y;

  s.setTextFont(1);
  s.setTextSize(1);
  s.setTextDatum(TL_DATUM);
  s.setTextColor(currentAsciiClockTextColor());

  for (int row = 0; row < font.rowCount; ++row) {
    char rowBuf[96] = "";
    for (size_t i = 0; timeText[i] != '\0'; ++i) {
      if (i > 0) {
        for (int gap = 0; gap < font.glyphGap; ++gap) appendCharSafe(rowBuf, sizeof(rowBuf), ' ');
      }
      appendAsciiClockGlyphRow(rowBuf, sizeof(rowBuf), font, asciiClockGlyphFor(font, timeText[i]), row);
    }
    trimTrailingSpaces(rowBuf);
    if (rowBuf[0] != '\0') s.drawString(rowBuf, x, y + row * ASCII_CLOCK_ROW_H);
  }

  s.setTextFont(2);
}

void drawClock(TFT_eSprite& s) {
  if (!clockVisible || clockDisplayStyle != CLOCK_STYLE_SMALL_TEXT) return;
  char line[32];
  formatClockDisplay(line, sizeof(line));
  s.setTextSize(1);
  s.setTextDatum(TC_DATUM);
  s.setTextColor(currentSmallClockTextColor());
  int y = (clockSmallPosition == CLOCK_SMALL_TOP) ? 4 : (SCREEN_H - 18);
  s.drawString(line, SCREEN_W / 2, y);
}

// ===== part: new_menu =====
// ------------------------------ Menu (EC11 encoder + K0 UI) ------------------
// Replaces the original touchscreen HUD/settings panels. Rotating the
// encoder opens/navigates the menu, pushing it validates/enters an item (or
// feeds the fish from the home screen), and K0 is the secondary action:
// it cycles the background style from the home screen, and acts as "back"
// while inside the menu.
enum MenuMode {
  MENU_MODE_HOME,
  MENU_MODE_LIST,
  MENU_MODE_EDIT,
  MENU_MODE_CLOCK_EDIT,
  MENU_MODE_WIFI_LIST,
  MENU_MODE_WIFI_NETWORKS,
  MENU_MODE_WIFI_PASSWORD,
};

enum MenuItemId {
  MENU_FISH_COUNT,
  MENU_BUBBLE_COUNT,
  MENU_BACKGROUND_STYLE,
  MENU_BACKGROUND_RAINBOW,
  MENU_BRIGHTNESS,
  MENU_SEAWEED_SWAY,
  MENU_SEAWEED_LENGTH,
  MENU_OCTOPUS_FREQ,
  MENU_SEAHORSE_FREQ,
  MENU_SNAIL_FREQ,
  MENU_JELLYFISH_FREQ,
  MENU_AUTOFEED_FREQ,
  MENU_CLOCK_MODE,
  MENU_CLOCK_24H,
  MENU_CLOCK_FONT,
  MENU_CLOCK_USE_NET,
  MENU_TIMEZONE,
  MENU_CLOCK_SET,
  MENU_WIFI,
  MENU_RESPAWN,
  MENU_RANDOMIZE_FLOWERS,
  MENU_EXIT,
  MENU_ITEM_COUNT
};

static const char* const kMenuLabels[MENU_ITEM_COUNT] = {
    "Fish Count",
    "Bubble Count",
    "Background",
    "Bg Rainbow",
    "Brightness",
    "Seaweed Sway",
    "Seaweed Length",
    "Octopus Visits",
    "Seahorse Visits",
    "Snail Visits",
    "Jellyfish Visits",
    "Auto-Feed",
    "Clock",
    "Clock 24h",
    "Clock Font",
    "Clock Net Sync",
    "Timezone",
    "Set Clock...",
    "WiFi...",
    "Respawn Fish",
    "Randomize Flowers",
    "Exit",
};

static const char kCharPickerCharset[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*-_.,:;";
static constexpr int kCharPickerCharCount = sizeof(kCharPickerCharset) - 1;  // exclude '\0'
static constexpr int kCharPickerTotal = kCharPickerCharCount + 2;            // + DONE + DEL

static MenuMode menuMode = MENU_MODE_HOME;
static int menuCursor = 0;
static int menuScrollTop = 0;
static unsigned long menuLastInputMs = 0;
static const unsigned long MENU_IDLE_TIMEOUT_MS = 20000UL;

static int wifiListCursor = 0;
static int wifiNetCursor = 0;
static int charPickerIndex = 0;
static char wifiPasswordBuffer[WIFI_PASS_MAX_LEN + 1] = "";

static void menuTouch() {
  menuLastInputMs = millis();
}

static int clockModeState() {
  if (!clockVisible) return 0;
  if (clockDisplayStyle == CLOCK_STYLE_ASCII) return 3;
  return (clockSmallPosition == CLOCK_SMALL_TOP) ? 1 : 2;
}

static void setClockModeState(int state) {
  int count = 4;
  state = ((state % count) + count) % count;
  switch (state) {
    case 0:
      clockVisible = false;
      break;
    case 1:
      clockVisible = true;
      clockDisplayStyle = CLOCK_STYLE_SMALL_TEXT;
      clockSmallPosition = CLOCK_SMALL_TOP;
      break;
    case 2:
      clockVisible = true;
      clockDisplayStyle = CLOCK_STYLE_SMALL_TEXT;
      clockSmallPosition = CLOCK_SMALL_BOTTOM;
      break;
    case 3:
      clockVisible = true;
      clockDisplayStyle = CLOCK_STYLE_ASCII;
      break;
  }
}

static void formatHourly(char* out, size_t cap, int freq) {
  if (freq <= 0) {
    copySafe(out, cap, "Off");
    return;
  }
  snprintf(out, cap, "%d/hr", freq);
}

static bool menuItemIsAction(MenuItemId id) {
  return id == MENU_RESPAWN || id == MENU_RANDOMIZE_FLOWERS || id == MENU_EXIT;
}

static bool menuItemIsSubmenu(MenuItemId id) {
  return id == MENU_CLOCK_SET || id == MENU_WIFI;
}

static void menuFormatValue(MenuItemId id, char* out, size_t cap) {
  switch (id) {
    case MENU_FISH_COUNT:
      snprintf(out, cap, "%d", fishTargetCount);
      return;
    case MENU_BUBBLE_COUNT:
      snprintf(out, cap, "%d", bubbleTargetCount);
      return;
    case MENU_BACKGROUND_STYLE:
      copySafe(out, cap, backgroundStyleName());
      return;
    case MENU_BACKGROUND_RAINBOW:
      copySafe(out, cap, backgroundRainbowEnabled ? "On" : "Off");
      return;
    case MENU_BRIGHTNESS:
      snprintf(out, cap, "%d%%", lcdBrightness);
      return;
    case MENU_SEAWEED_SWAY:
      snprintf(out, cap, "%.2f", (double)seaweedSwaySpeed);
      return;
    case MENU_SEAWEED_LENGTH:
      snprintf(out, cap, "%.2f", (double)seaweedLength);
      return;
    case MENU_OCTOPUS_FREQ:
      formatHourly(out, cap, octopusFrequency);
      return;
    case MENU_SEAHORSE_FREQ:
      formatHourly(out, cap, seahorseFrequency);
      return;
    case MENU_SNAIL_FREQ:
      formatHourly(out, cap, snailFrequency);
      return;
    case MENU_JELLYFISH_FREQ:
      formatHourly(out, cap, jellyfishFrequency);
      return;
    case MENU_AUTOFEED_FREQ:
      formatHourly(out, cap, autoFeedFrequency);
      return;
    case MENU_CLOCK_MODE: {
      static const char* names[4] = {"Off", "Small Top", "Small Bot", "ASCII"};
      copySafe(out, cap, names[clockModeState()]);
      return;
    }
    case MENU_CLOCK_24H:
      copySafe(out, cap, clockUse24Hour ? "24h" : "12h");
      return;
    case MENU_CLOCK_FONT:
      copySafe(out, cap, currentAsciiClockFont().label);
      return;
    case MENU_CLOCK_USE_NET:
      copySafe(out, cap, clockUseInternetTime ? "On" : "Off");
      return;
    case MENU_TIMEZONE:
      copySafe(out, cap, currentTimezone().label);
      return;
    case MENU_CLOCK_SET: {
      char line[32];
      formatClockDisplay(line, sizeof(line));
      copySafe(out, cap, line);
      return;
    }
    case MENU_WIFI:
      copySafe(out, cap, wifiStatusText);
      return;
    default:
      out[0] = '\0';
      return;
  }
}

static void menuAdjustItem(MenuItemId id, int delta) {
  switch (id) {
    case MENU_FISH_COUNT:
      fishTargetCount = clampVal(fishTargetCount + delta, MIN_FISH, MAX_FISH);
      applyFishPopulation();
      break;
    case MENU_BUBBLE_COUNT:
      bubbleTargetCount = clampVal(bubbleTargetCount + delta, MIN_BUBBLES, MAX_BUBBLES);
      applyBubblePopulation(true);
      break;
    case MENU_BACKGROUND_STYLE:
      cycleBackgroundStyle(delta);
      break;
    case MENU_BACKGROUND_RAINBOW:
      setBackgroundRainbowEnabled(!backgroundRainbowEnabled);
      break;
    case MENU_BRIGHTNESS:
      lcdBrightness = clampVal(lcdBrightness + delta * LCD_BRIGHTNESS_STEP, MIN_LCD_BRIGHTNESS, MAX_LCD_BRIGHTNESS);
      applyLcdBrightness();
      break;
    case MENU_SEAWEED_SWAY:
      seaweedSwaySpeed = clampVal(seaweedSwaySpeed + delta * 0.05f, MIN_SWAY, MAX_SWAY);
      break;
    case MENU_SEAWEED_LENGTH:
      seaweedLength = clampVal(seaweedLength + delta * 0.05f, MIN_SEAWEED_LENGTH, MAX_SEAWEED_LENGTH);
      break;
    case MENU_OCTOPUS_FREQ:
      cycleOctopusFrequency(delta);
      break;
    case MENU_SEAHORSE_FREQ:
      cycleSeahorseFrequency(delta);
      break;
    case MENU_SNAIL_FREQ:
      cycleSnailFrequency(delta);
      break;
    case MENU_JELLYFISH_FREQ:
      cycleJellyfishFrequency(delta);
      break;
    case MENU_AUTOFEED_FREQ:
      cycleAutoFeedFrequency(delta);
      break;
    case MENU_CLOCK_MODE:
      setClockModeState(clockModeState() + delta);
      break;
    case MENU_CLOCK_24H:
      clockUse24Hour = !clockUse24Hour;
      break;
    case MENU_CLOCK_FONT:
      adjustAsciiClockFont(delta);
      break;
    case MENU_CLOCK_USE_NET:
      clockUseInternetTime = !clockUseInternetTime;
      break;
    case MENU_TIMEZONE:
      cycleTimezone(delta);
      break;
    default:
      break;
  }
  markSettingsDirty();
}

static void menuActivateItem() {
  MenuItemId id = (MenuItemId)menuCursor;
  if (id == MENU_RESPAWN) {
    respawnFishPopulation();
    markSettingsDirty();
    return;
  }
  if (id == MENU_RANDOMIZE_FLOWERS) {
    randomizeFlowers();
    return;
  }
  if (id == MENU_EXIT) {
    menuMode = MENU_MODE_HOME;
    return;
  }
  if (id == MENU_CLOCK_SET) {
    menuMode = MENU_MODE_CLOCK_EDIT;
    return;
  }
  if (id == MENU_WIFI) {
    menuMode = MENU_MODE_WIFI_LIST;
    wifiListCursor = 0;
    return;
  }
  menuMode = MENU_MODE_EDIT;
}

// ------------------------------ OTA update -------------------------------------
// Fully blocking and takes over the physical display directly (bypassing the
// canvas/menu system) - this is a rare, deliberate action, not something that
// needs to keep the aquarium animating underneath it. The dual-core display
// task keeps idling on its semaphore meanwhile, so there's no conflict over
// the SPI bus.
static void otaDrawStatus(const char* line1, const char* line2 = nullptr) {
  tft.fillScreen(BG_COLOR);
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_CYAN, BG_COLOR);
  tft.drawString(line1, SCREEN_W / 2, SCREEN_H / 2 - 10);
  if (line2) {
    tft.setTextColor(TFT_WHITE, BG_COLOR);
    tft.drawString(line2, SCREEN_W / 2, SCREEN_H / 2 + 14);
  }
}

static void otaProgressCallback(size_t done, size_t total) {
  char pct[8];
  snprintf(pct, sizeof(pct), "%u%%", total ? (unsigned)((done * 100ULL) / total) : 0);
  otaDrawStatus("Updating...", pct);
}

// GitHub's release-asset download is a redirect chain across two different
// hosts (github.com -> release-assets.githubusercontent.com). Letting
// HTTPClient's built-in HTTPC_STRICT_FOLLOW_REDIRECTS handle that
// automatically was an intermittent, hard-to-debug source of hangs, so each
// hop gets a deliberately fresh HTTPClient/GET here instead - simpler to
// reason about, and each failure logs exactly which hop and HTTP code it
// died on.
static int otaFollowRedirects(WiFiClientSecure& client, HTTPClient& http, const char* startUrl) {
  String url = startUrl;
  for (int hop = 0; hop < 5; ++hop) {
    http.end();
    Serial.printf("[OTA] hop %d -> %s\n", hop, url.c_str());
    otaDrawStatus("Connecting...", hop == 0 ? "github.com" : "download server");
    if (!http.begin(client, url)) {
      Serial.println("[OTA] http.begin() failed");
      return -1;
    }
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    const char* headerKeys[] = {"Location"};
    http.collectHeaders(headerKeys, 1);
    int code = http.GET();
    Serial.printf("[OTA] hop %d HTTP code %d\n", hop, code);
    if (code == HTTP_CODE_MOVED_PERMANENTLY || code == HTTP_CODE_FOUND || code == HTTP_CODE_SEE_OTHER ||
        code == HTTP_CODE_TEMPORARY_REDIRECT || code == HTTP_CODE_PERMANENT_REDIRECT) {
      String location = http.header("Location");
      if (location.length() == 0) {
        Serial.println("[OTA] redirect with no Location header");
        return code;
      }
      url = location;
      continue;
    }
    return code;
  }
  Serial.println("[OTA] too many redirects");
  return -1;
}

static void parseVersionTag(const char* tag, int& major, int& minor, int& patch) {
  major = minor = patch = 0;
  if (tag[0] == 'v' || tag[0] == 'V') tag++;
  sscanf(tag, "%d.%d.%d", &major, &minor, &patch);
}

// Returns >0 if `a` is newer than `b`, <0 if older, 0 if equal.
static int compareVersions(const char* a, const char* b) {
  int aMaj, aMin, aPatch, bMaj, bMin, bPatch;
  parseVersionTag(a, aMaj, aMin, aPatch);
  parseVersionTag(b, bMaj, bMin, bPatch);
  if (aMaj != bMaj) return aMaj - bMaj;
  if (aMin != bMin) return aMin - bMin;
  return aPatch - bPatch;
}

// Cheap check for what the latest release actually is: this URL 302s
// straight to ".../releases/tag/<tag>" with no response body, so reading
// just the Location header tells us the latest version without pulling the
// ~12KB JSON API response or (much bigger) firmware.bin.
static bool fetchLatestVersionTag(WiFiClientSecure& client, HTTPClient& http, char* out, size_t cap) {
  http.end();
  if (!http.begin(client, kOtaLatestReleaseUrl)) return false;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char* headerKeys[] = {"Location"};
  http.collectHeaders(headerKeys, 1);
  int code = http.GET();
  Serial.printf("[OTA] version check HTTP code %d\n", code);
  if (code != HTTP_CODE_MOVED_PERMANENTLY && code != HTTP_CODE_FOUND) {
    http.end();
    return false;
  }
  String location = http.header("Location");
  http.end();
  int slash = location.lastIndexOf('/');
  if (slash < 0) return false;
  copySafe(out, cap, location.c_str() + slash + 1);
  return out[0] != '\0';
}

static void runOtaUpdate() {
  logHeap("runOtaUpdate start");
  if (!wifiConnected) {
    otaDrawStatus("Update failed", "WiFi not connected");
    delay(2000);
    return;
  }

  // TLS certificate validation checks the cert's validity dates against the
  // device's system clock. Right after WiFi connects, NTP sync is still
  // running in the background (see serviceInternetTime()), so the clock can
  // still read ~1970 for a few seconds - which makes every real-world
  // certificate look "not yet valid" and the handshake hang/retry for a long
  // time before failing. Wait for a sane-looking time (or give up after 10s
  // and try anyway) before opening the connection.
  otaDrawStatus("Waiting for time sync...");
  unsigned long waitStart = millis();
  while (time(nullptr) < 1700000000 && millis() - waitStart < 10000UL) {
    delay(200);
  }
  Serial.printf("[OTA] clock at start: %ld\n", (long)time(nullptr));

  otaDrawStatus("Checking for update...");

  WiFiClientSecure client;
  client.setCACert(kOtaTrustedRootCAs);
  client.setTimeout(15000);

  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setTimeout(15000);

  char latestTag[24] = "";
  if (fetchLatestVersionTag(client, http, latestTag, sizeof(latestTag))) {
    Serial.printf("[OTA] installed=%s latest=%s\n", kFirmwareVersion, latestTag);
    if (compareVersions(latestTag, kFirmwareVersion) <= 0) {
      char msg[32];
      snprintf(msg, sizeof(msg), "%s is current", kFirmwareVersion);
      otaDrawStatus("Already up to date", msg);
      delay(2500);
      return;
    }
  } else {
    Serial.println("[OTA] version check failed, downloading anyway");
  }

  logHeap("before otaFollowRedirects");
  int httpCode = otaFollowRedirects(client, http, kOtaFirmwareUrl);
  logHeap("after otaFollowRedirects");
  if (httpCode != HTTP_CODE_OK) {
    char err[32];
    snprintf(err, sizeof(err), "HTTP error %d", httpCode);
    Serial.printf("[OTA] failed: %s\n", err);
    otaDrawStatus("Update failed", err);
    http.end();
    delay(2000);
    return;
  }

  int len = http.getSize();
  Serial.printf("[OTA] content length: %d\n", len);
  if (len <= 0) {
    otaDrawStatus("Update failed", "Unknown download size");
    http.end();
    delay(2000);
    return;
  }

  if (!Update.begin(len)) {
    Serial.printf("[OTA] Update.begin failed: %s\n", Update.errorString());
    otaDrawStatus("Update failed", "Not enough OTA space");
    http.end();
    delay(2000);
    return;
  }

  logHeap("before writeStream");
  Update.onProgress(otaProgressCallback);
  WiFiClient* stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  http.end();
  Serial.printf("[OTA] wrote %u/%d bytes\n", (unsigned)written, len);
  logHeap("after writeStream");

  if (written != (size_t)len) {
    char err[40];
    snprintf(err, sizeof(err), "Wrote %u/%u bytes", (unsigned)written, (unsigned)len);
    otaDrawStatus("Update failed", err);
    Update.end(false);
    delay(2000);
    return;
  }

  if (!Update.end(true) || !Update.isFinished()) {
    Serial.printf("[OTA] Update.end failed: %s\n", Update.errorString());
    otaDrawStatus("Update failed", Update.errorString());
    delay(2000);
    return;
  }

  Serial.println("[OTA] success, rebooting");
  otaDrawStatus("Update complete", "Rebooting...");
  delay(1200);
  ESP.restart();
}

// ------------------------------ Input dispatch --------------------------------
void menuHandleInput() {
  wifiPanelOpen = (menuMode == MENU_MODE_WIFI_LIST || menuMode == MENU_MODE_WIFI_NETWORKS ||
                   menuMode == MENU_MODE_WIFI_PASSWORD);

  int delta = inputReadEncoderDelta();
  bool pushed = inputEncoderPressed();
  bool k0 = inputK0Pressed();

  switch (menuMode) {
    case MENU_MODE_HOME: {
      if (delta != 0) {
        menuMode = MENU_MODE_LIST;
        menuCursor = 0;
        menuScrollTop = 0;
        menuTouch();
      }
      if (pushed) {
        for (int i = 0; i < 3; ++i) {
          float x = frand(20.0f, (float)SCREEN_W - 20.0f);
          float y = frand(8.0f, 20.0f);
          spawnFlake(x, y);
        }
      }
      if (k0) {
        cycleBackgroundStyle(1);
        markSettingsDirty();
      }
      return;
    }
    case MENU_MODE_LIST: {
      if (delta != 0) {
        menuCursor = ((menuCursor + delta) % MENU_ITEM_COUNT + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
        menuTouch();
      }
      if (pushed) {
        menuActivateItem();
        menuTouch();
      }
      if (k0) {
        menuMode = MENU_MODE_HOME;
      }
      break;
    }
    case MENU_MODE_EDIT: {
      if (delta != 0) {
        menuAdjustItem((MenuItemId)menuCursor, delta);
        menuTouch();
      }
      if (pushed || k0) {
        menuMode = MENU_MODE_LIST;
        menuTouch();
      }
      break;
    }
    case MENU_MODE_CLOCK_EDIT: {
      if (delta != 0) {
        adjustClockField(delta);
        markSettingsDirty();
        menuTouch();
      }
      if (pushed) {
        selectClockField(1);
        menuTouch();
      }
      if (k0) {
        menuMode = MENU_MODE_LIST;
      }
      break;
    }
    case MENU_MODE_WIFI_LIST: {
      if (delta != 0) {
        wifiListCursor = ((wifiListCursor + delta) % 3 + 3) % 3;
        menuTouch();
      }
      if (pushed) {
        if (wifiListCursor == 0) {
          logHeap("before setWifiEnabled");
          setWifiEnabled(!wifiEnabled);
          logHeap("after setWifiEnabled");
        } else if (wifiListCursor == 1) {
          if (wifiEnabled) {
            logHeap("before startWifiScan");
            startWifiScan();
            logHeap("after startWifiScan");
            wifiNetCursor = 0;
            menuMode = MENU_MODE_WIFI_NETWORKS;
          }
        } else if (wifiConnected) {
          logHeap("before runOtaUpdate");
          runOtaUpdate();  // blocking; only returns on failure
          logHeap("after runOtaUpdate");
          menuTouch();
        }
        menuTouch();
      }
      if (k0) {
        menuMode = MENU_MODE_LIST;
      }
      break;
    }
    case MENU_MODE_WIFI_NETWORKS: {
      if (delta != 0 && wifiNetworkCount > 0) {
        wifiNetCursor = ((wifiNetCursor + delta) % wifiNetworkCount + wifiNetworkCount) % wifiNetworkCount;
        menuTouch();
      }
      if (pushed && wifiNetworkCount > 0) {
        copySafe(pendingWifiSsid, sizeof(pendingWifiSsid), wifiNetworkNames[wifiNetCursor]);
        if (wifiNetworkOpen[wifiNetCursor]) {
          startWifiConnect(pendingWifiSsid, "", true);
          menuMode = MENU_MODE_WIFI_LIST;
        } else {
          wifiPasswordBuffer[0] = '\0';
          charPickerIndex = 0;
          menuMode = MENU_MODE_WIFI_PASSWORD;
        }
        menuTouch();
      }
      if (k0) {
        menuMode = MENU_MODE_WIFI_LIST;
      }
      break;
    }
    case MENU_MODE_WIFI_PASSWORD: {
      if (delta != 0) {
        charPickerIndex = ((charPickerIndex + delta) % kCharPickerTotal + kCharPickerTotal) % kCharPickerTotal;
        menuTouch();
      }
      if (pushed) {
        if (charPickerIndex == 0) {
          startWifiConnect(pendingWifiSsid, wifiPasswordBuffer, true);
          menuMode = MENU_MODE_WIFI_LIST;
        } else if (charPickerIndex == 1) {
          size_t len = strlen(wifiPasswordBuffer);
          if (len > 0) wifiPasswordBuffer[len - 1] = '\0';
        } else {
          appendCharSafe(wifiPasswordBuffer, sizeof(wifiPasswordBuffer), kCharPickerCharset[charPickerIndex - 2]);
        }
        menuTouch();
      }
      if (k0) {
        menuMode = MENU_MODE_WIFI_NETWORKS;
      }
      break;
    }
  }

  if (menuMode != MENU_MODE_HOME && millis() - menuLastInputMs > MENU_IDLE_TIMEOUT_MS) {
    menuMode = MENU_MODE_HOME;
  }
}

// ------------------------------ Rendering -------------------------------------
static const int MENU_PANEL_X = 14;
static const int MENU_PANEL_Y = 6;
static const int MENU_PANEL_W = SCREEN_W - 28;
static const int MENU_PANEL_H = SCREEN_H - 12;
static const int MENU_ROW_H = 16;

static void drawMenuPanelFrame(TFT_eSprite& s, const char* title) {
  s.fillRoundRect(MENU_PANEL_X, MENU_PANEL_Y, MENU_PANEL_W, MENU_PANEL_H, 6, TFT_BLACK);
  s.drawRoundRect(MENU_PANEL_X, MENU_PANEL_Y, MENU_PANEL_W, MENU_PANEL_H, 6, TFT_DARKGREY);
  s.setTextFont(2);
  s.setTextDatum(TC_DATUM);
  s.setTextColor(TFT_CYAN, TFT_BLACK);
  s.drawString(title, SCREEN_W / 2, MENU_PANEL_Y + 6);
}

static void drawMenuFooter(TFT_eSprite& s, const char* hint) {
  s.setTextDatum(BC_DATUM);
  s.setTextColor(TFT_DARKGREY, TFT_BLACK);
  s.drawString(hint, SCREEN_W / 2, MENU_PANEL_Y + MENU_PANEL_H - 4);
}

static void drawMenuList(TFT_eSprite& s) {
  drawMenuPanelFrame(s, "SETTINGS");

  int listTop = MENU_PANEL_Y + 24;
  int listBottom = MENU_PANEL_Y + MENU_PANEL_H - 16;
  int visibleRows = (listBottom - listTop) / MENU_ROW_H;
  if (visibleRows < 1) visibleRows = 1;

  if (menuCursor < menuScrollTop) menuScrollTop = menuCursor;
  if (menuCursor >= menuScrollTop + visibleRows) menuScrollTop = menuCursor - visibleRows + 1;

  s.setTextFont(2);
  for (int row = 0; row < visibleRows; ++row) {
    int idx = menuScrollTop + row;
    if (idx >= MENU_ITEM_COUNT) break;
    int y = listTop + row * MENU_ROW_H;
    bool isCursor = (idx == menuCursor);
    bool isEditing = isCursor && (menuMode == MENU_MODE_EDIT);
    uint16_t bg = isEditing ? TFT_DARKGREEN : (isCursor ? TFT_NAVY : TFT_BLACK);
    s.fillRect(MENU_PANEL_X + 4, y, MENU_PANEL_W - 8, MENU_ROW_H, bg);

    s.setTextDatum(ML_DATUM);
    s.setTextColor(TFT_WHITE, bg);
    s.drawString(kMenuLabels[idx], MENU_PANEL_X + 10, y + MENU_ROW_H / 2);

    MenuItemId id = (MenuItemId)idx;
    if (!menuItemIsAction(id)) {
      char value[32];
      menuFormatValue(id, value, sizeof(value));
      s.setTextDatum(MR_DATUM);
      s.setTextColor(isEditing ? TFT_YELLOW : TFT_GREENYELLOW, bg);
      s.drawString(value, MENU_PANEL_X + MENU_PANEL_W - 10, y + MENU_ROW_H / 2);
    }
  }

  drawMenuFooter(s, "Push: select   K0: back");
}

static void drawClockEditScreen(TFT_eSprite& s) {
  drawMenuPanelFrame(s, "SET CLOCK");
  char line[32];
  formatClockDisplay(line, sizeof(line));
  s.setTextFont(2);
  s.setTextDatum(TC_DATUM);
  s.setTextColor(TFT_WHITE, TFT_BLACK);
  s.drawString(line, SCREEN_W / 2, MENU_PANEL_Y + 40);

  char fieldLine[32];
  snprintf(fieldLine, sizeof(fieldLine), "Editing: %s", clockFieldName());
  s.setTextColor(TFT_YELLOW, TFT_BLACK);
  s.drawString(fieldLine, SCREEN_W / 2, MENU_PANEL_Y + 64);

  drawMenuFooter(s, "Rotate: adjust   Push: next field   K0: done");
}

static void drawWifiListScreen(TFT_eSprite& s) {
  drawMenuPanelFrame(s, "WIFI");
  int y = MENU_PANEL_Y + 30;

  bool row0 = (wifiListCursor == 0);
  s.fillRect(MENU_PANEL_X + 4, y, MENU_PANEL_W - 8, MENU_ROW_H, row0 ? TFT_NAVY : TFT_BLACK);
  s.setTextFont(2);
  s.setTextDatum(ML_DATUM);
  s.setTextColor(TFT_WHITE, row0 ? TFT_NAVY : TFT_BLACK);
  s.drawString("WiFi Enabled", MENU_PANEL_X + 10, y + MENU_ROW_H / 2);
  s.setTextDatum(MR_DATUM);
  s.setTextColor(TFT_GREENYELLOW, row0 ? TFT_NAVY : TFT_BLACK);
  s.drawString(wifiEnabled ? "On" : "Off", MENU_PANEL_X + MENU_PANEL_W - 10, y + MENU_ROW_H / 2);

  y += MENU_ROW_H + 4;
  bool row1 = (wifiListCursor == 1);
  s.fillRect(MENU_PANEL_X + 4, y, MENU_PANEL_W - 8, MENU_ROW_H, row1 ? TFT_NAVY : TFT_BLACK);
  s.setTextDatum(ML_DATUM);
  s.setTextColor(wifiEnabled ? TFT_WHITE : TFT_DARKGREY, row1 ? TFT_NAVY : TFT_BLACK);
  s.drawString("Scan Networks", MENU_PANEL_X + 10, y + MENU_ROW_H / 2);

  y += MENU_ROW_H + 4;
  bool row2 = (wifiListCursor == 2);
  s.fillRect(MENU_PANEL_X + 4, y, MENU_PANEL_W - 8, MENU_ROW_H, row2 ? TFT_NAVY : TFT_BLACK);
  s.setTextDatum(ML_DATUM);
  s.setTextColor(wifiConnected ? TFT_WHITE : TFT_DARKGREY, row2 ? TFT_NAVY : TFT_BLACK);
  s.drawString("Check for Update", MENU_PANEL_X + 10, y + MENU_ROW_H / 2);

  y += MENU_ROW_H + 12;
  s.setTextDatum(ML_DATUM);
  s.setTextColor(TFT_CYAN, TFT_BLACK);
  char statusLine[56];
  snprintf(statusLine, sizeof(statusLine), "Status: %s", wifiStatusText);
  s.drawString(statusLine, MENU_PANEL_X + 10, y);

  y += MENU_ROW_H - 2;
  s.setTextColor(TFT_DARKGREY, TFT_BLACK);
  char versionLine[32];
  snprintf(versionLine, sizeof(versionLine), "Version: %s", kFirmwareVersion);
  s.drawString(versionLine, MENU_PANEL_X + 10, y);

  drawMenuFooter(s, "Push: select   K0: back");
}

static void drawWifiNetworksScreen(TFT_eSprite& s) {
  drawMenuPanelFrame(s, "SELECT NETWORK");
  int listTop = MENU_PANEL_Y + 26;
  s.setTextFont(2);
  if (wifiNetworkCount == 0) {
    s.setTextDatum(TC_DATUM);
    s.setTextColor(TFT_DARKGREY, TFT_BLACK);
    s.drawString(wifiScanInProgress ? "Scanning..." : "No networks found", SCREEN_W / 2, listTop + 30);
  } else {
    for (int i = 0; i < wifiNetworkCount; ++i) {
      int y = listTop + i * MENU_ROW_H;
      bool isCursor = (i == wifiNetCursor);
      uint16_t bg = isCursor ? TFT_NAVY : TFT_BLACK;
      s.fillRect(MENU_PANEL_X + 4, y, MENU_PANEL_W - 8, MENU_ROW_H, bg);
      s.setTextDatum(ML_DATUM);
      s.setTextColor(TFT_WHITE, bg);
      s.drawString(wifiNetworkNames[i], MENU_PANEL_X + 10, y + MENU_ROW_H / 2);
      s.setTextDatum(MR_DATUM);
      s.setTextColor(TFT_GREENYELLOW, bg);
      s.drawString(wifiNetworkOpen[i] ? "open" : "locked", MENU_PANEL_X + MENU_PANEL_W - 10, y + MENU_ROW_H / 2);
    }
  }
  drawMenuFooter(s, "Push: select   K0: back");
}

static void drawWifiPasswordScreen(TFT_eSprite& s) {
  drawMenuPanelFrame(s, "WIFI PASSWORD");
  s.setTextFont(2);
  s.setTextDatum(TC_DATUM);
  s.setTextColor(TFT_CYAN, TFT_BLACK);
  s.drawString(pendingWifiSsid, SCREEN_W / 2, MENU_PANEL_Y + 26);

  s.setTextColor(TFT_WHITE, TFT_BLACK);
  s.drawString(wifiPasswordBuffer[0] ? wifiPasswordBuffer : "(empty)", SCREEN_W / 2, MENU_PANEL_Y + 48);

  char candidate[8];
  if (charPickerIndex == 0) {
    copySafe(candidate, sizeof(candidate), "[DONE]");
  } else if (charPickerIndex == 1) {
    copySafe(candidate, sizeof(candidate), "[DEL]");
  } else {
    candidate[0] = kCharPickerCharset[charPickerIndex - 2];
    candidate[1] = '\0';
  }
  s.setTextColor(TFT_YELLOW, TFT_BLACK);
  s.drawString(candidate, SCREEN_W / 2, MENU_PANEL_Y + 80);

  drawMenuFooter(s, "Rotate: pick   Push: use   K0: cancel");
}

void drawMenuOverlay(TFT_eSprite& s) {
  switch (menuMode) {
    case MENU_MODE_HOME:
      return;
    case MENU_MODE_LIST:
    case MENU_MODE_EDIT:
      drawMenuList(s);
      return;
    case MENU_MODE_CLOCK_EDIT:
      drawClockEditScreen(s);
      return;
    case MENU_MODE_WIFI_LIST:
      drawWifiListScreen(s);
      return;
    case MENU_MODE_WIFI_NETWORKS:
      drawWifiNetworksScreen(s);
      return;
    case MENU_MODE_WIFI_PASSWORD:
      drawWifiPasswordScreen(s);
      return;
  }
}

// ===== part: new_scene =====
// ------------------------------ Scene composition -----------------------------
void drawSceneLayers(TFT_eSprite& s) {
  float tSec = aquariumTimeSec();
  drawBackground(s, tSec);
  drawAsciiClockBackground(s);
  drawSnail(s);
  drawSeaweed(s, tSec);
  drawBubbles(s);
  drawFlakes(s);
  drawFish(s);
  drawJellyfish(s);
  drawOctopus(s);
  drawSeahorse(s);
  drawClock(s);
  drawMenuOverlay(s);

  // Temporary diagnostic readout while chasing a reported stutter in fish
  // movement - remove once frame timing is confirmed smooth.
  char fpsText[12];
  snprintf(fpsText, sizeof(fpsText), "%.0ffps", fps);
  s.setTextFont(1);
  s.setTextDatum(TR_DATUM);
  s.setTextColor(TFT_DARKGREY, TFT_BLACK);
  s.drawString(fpsText, SCREEN_W - 2, 2);
}

// Runs on core 0: waits for a filled buffer, pushes it over SPI (the
// blocking part), then frees that buffer for reuse. Pinning this to the
// core the Arduino loop() doesn't run on lets that blocking SPI push
// happen concurrently with the next frame's physics/drawing on core 1.
//
// TFT_eSPI's SPI write path (pushPixels) is a tight polling loop with no
// FreeRTOS yield points of its own. When core 1 keeps this task fed with a
// new buffer the instant the previous push finishes, xSemaphoreTake never
// actually blocks, so core 0's IDLE0 task (priority 0) never gets scheduled
// - which starves it long enough to trip the task watchdog. The explicit
// vTaskDelay(1) guarantees IDLE0 a scheduling window every cycle.
static void displayTaskFn(void*) {
  for (;;) {
    xSemaphoreTake(pushRequestSem, portMAX_DELAY);
    int idx = pushBufferIndex;
    frameBuffers[idx]->pushSprite(0, 0);
    xSemaphoreGive(bufferFreeSem[idx]);
    vTaskDelay(1);
  }
}

void renderFrame() {
  if (!spriteReady) return;

  // Wait until this buffer's previous push (from two frames ago) is done
  // before drawing into it again.
  xSemaphoreTake(bufferFreeSem[drawBufferIndex], portMAX_DELAY);

  TFT_eSprite& s = *frameBuffers[drawBufferIndex];
  applyRenderViewport(s);
  drawSceneLayers(s);

  pushBufferIndex = drawBufferIndex;
  xSemaphoreGive(pushRequestSem);

  drawBufferIndex ^= 1;
}

// ===== part: new_setup_loop =====
// ------------------------------ Setup / Loop ----------------------------------
void setup() {
  Serial.begin(115200);
  randomSeed((uint32_t)esp_random());

  spriteReady = allocateMainCanvas();
  loadPersistentState();

  inputInit();

  ledcAttach(TFT_BL, 5000, 8);
  applyLcdBrightness();
  tft.init();
  // This ST7789 driver's init sequence sends INVON unconditionally
  // (TFT_Drivers/ST7789_Init.h doesn't actually consult TFT_INVERSION_ON/
  // OFF), so toggling that define in User_Setup.h has no effect - turn
  // inversion off explicitly here instead, since this panel showed black
  // as white with it left on.
  tft.invertDisplay(false);
  tft.setRotation(1);
  tft.fillScreen(BG_COLOR);
  tft.setTextWrap(false);
  tft.setTextFont(2);
  tft.setTextColor(TFT_GREEN, BG_COLOR);
  tft.setCursor(10, 10);
  tft.println("ASCII Aquarium booting...");

  if (spriteReady) {
    canvas.setTextFont(2);
    canvas2.setTextFont(2);
    bufferFreeSem[0] = xSemaphoreCreateBinary();
    bufferFreeSem[1] = xSemaphoreCreateBinary();
    pushRequestSem = xSemaphoreCreateBinary();
    xSemaphoreGive(bufferFreeSem[0]);  // both buffers start out free
    xSemaphoreGive(bufferFreeSem[1]);
    xTaskCreatePinnedToCore(displayTaskFn, "display", 4096, nullptr, 1, &displayTaskHandle, 0);
    allocateGradientBandCache();
    tft.setCursor(10, 28);
    tft.setTextColor(TFT_CYAN, BG_COLOR);
    tft.println("Sprite buffer: OK");
    tft.setCursor(10, 46);
    tft.printf("Render: %dx%d\n", SCREEN_W, SCREEN_H);
    tft.setCursor(10, 64);
    tft.printf("Heap: %lu/%luK\n", (unsigned long)(ESP.getFreeHeap() / 1024UL),
               (unsigned long)(ESP.getHeapSize() / 1024UL));
    tft.setCursor(10, 82);
    if (ESP.getPsramSize() > 0) {
      tft.printf("PSRAM: %lu/%luK\n", (unsigned long)(ESP.getFreePsram() / 1024UL),
                 (unsigned long)(ESP.getPsramSize() / 1024UL));
    } else {
      tft.println("PSRAM: none");
    }
  } else {
    tft.setCursor(10, 28);
    tft.setTextColor(TFT_RED, BG_COLOR);
    tft.println("Sprite alloc failed");
  }

  for (int i = 0; i < MAX_FLAKES; i++) flakes[i].active = false;
  for (int i = 0; i < MAX_BUBBLES; i++) bubbles[i].active = false;
  for (int i = 0; i < MAX_FISH_POOL; i++) fishPool[i].active = false;
  octopus.active = false;
  octopus.cthulhu = false;
  octopus.nextSpawnMs = 0;
  seahorse.active = false;
  seahorse.nextSpawnMs = 0;
  snail.active = false;
  snail.nextSpawnMs = 0;
  jellyfish.active = false;
  jellyfish.nextSpawnMs = 0;
  initFishMirrors();
  initFishGlyphMetrics();
  applyFishPopulation();
  spreadInitialFishLayout();
  applyBubblePopulation(true);

  if (wifiEnabled) {
    setWifiStatus(wifiSsid[0] ? "Starting..." : "Ready to scan");
  }

  delay(350);
  tft.fillScreen(BG_COLOR);
  lastMs = millis();
  aquariumNowMs = lastMs;
  clockLastMinuteMs = lastMs;
  lastSettingsSaveMs = lastMs;
  fpsTimer = lastMs;
  frameCount = 0;
}

void loop() {
  unsigned long now = millis();
  unsigned long elapsedMs = now - lastMs;
  lastMs = now;

  unsigned long aquariumStepMs = clampVal(elapsedMs, 1UL, 50UL);
  aquariumNowMs += aquariumStepMs;
  float dt = aquariumStepMs * 0.001f;

  updateClock(now);
  menuHandleInput();
  serviceAutoSky();
  serviceBackgroundRainbow(aquariumNowMs);
  serviceWifi(now);
  serviceSettingsPersistence(now);
  serviceAutoFeed(aquariumNowMs);
  updateFlakes(dt);
  updateBubbles(dt);
  updateFish(dt);
  updateOctopus(aquariumNowMs, dt);
  updateSeahorse(aquariumNowMs, dt);
  updateSnail(aquariumNowMs, dt);
  updateJellyfish(aquariumNowMs, dt);
  if (fishAvoidanceEnabled()) keepVisitorsSeparated();
  renderFrame();

  frameCount++;
  if (now - fpsTimer >= 500) {
    fps = (frameCount * 1000.0f) / (now - fpsTimer + 1);
    frameCount = 0;
    fpsTimer = now;
  }

  static unsigned long lastHeapLogMs = 0;
  if (now - lastHeapLogMs >= 10000UL) {
    logHeap("periodic");
    lastHeapLogMs = now;
  }
}
