#!/usr/bin/env bash
set -euo pipefail

# Builds ASCII Aquarium as a guest app for ludodefgh/launcher
# (https://github.com/ludodefgh/launcher), plus the launcher itself, then
# prints a single esptool write_flash command with every piece at its
# correct offset (see partitions_launcher_guest.csv for the full layout).
#
# Deliberately two separate build systems, not merged into one: the
# launcher is an ESP-IDF/idf.py project, this repo is PlatformIO/Arduino.
# See the design discussion in https://github.com/ludodefgh/launcher/issues/11
# for why (and what a tighter integration would require).
#
# Usage:
#   scripts/build_launcher_guest.sh [launcher_dir]
#
# launcher_dir defaults to a throwaway clone at .launcher-cache/ (gitignored)
# inside this repo. Pass an existing local checkout instead if you have one
# (e.g. ~/Documents/Projects/Bootloader) to skip the clone - NOTE this runs
# `idf.py build` there, which creates/updates build/ and sdkconfig in that
# directory. Don't point this at a checkout you have open in an editor/agent
# with unsaved config changes you don't want touched.

AQUARIUM_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LAUNCHER_DIR="${1:-$AQUARIUM_DIR/.launcher-cache}"
IDF_IMAGE="espressif/idf:release-v5.5"
GUEST_ENV="esp32-s3-n8r2-launcher-guest"

if [ ! -d "$LAUNCHER_DIR/.git" ]; then
  echo "==> Cloning ludodefgh/launcher into $LAUNCHER_DIR"
  git clone https://github.com/ludodefgh/launcher.git "$LAUNCHER_DIR"
else
  echo "==> Using existing launcher checkout at $LAUNCHER_DIR (not pulling - update it yourself if needed)"
fi

echo "==> Building launcher (idf.py, esp32s3 target) via $IDF_IMAGE"
docker run --rm -v "$LAUNCHER_DIR":/workspaces/launcher -w /workspaces/launcher "$IDF_IMAGE" \
  bash -c '. $IDF_PATH/export.sh && idf.py set-target esp32s3 && idf.py build'

echo "==> Building ASCII Aquarium ($GUEST_ENV)"
cd "$AQUARIUM_DIR"
pio run -e "$GUEST_ENV"

LAUNCHER_BUILD="$LAUNCHER_DIR/build"
AQUARIUM_BUILD="$AQUARIUM_DIR/.pio/build/$GUEST_ENV"

echo "==> Verifying partition tables are byte-identical (launcher vs aquarium build)"
if ! diff -q "$LAUNCHER_BUILD/partition_table/partition-table.bin" \
             "$AQUARIUM_BUILD/partitions.bin" > /dev/null; then
  echo "ERROR: partition tables differ - do not flash." >&2
  echo "Check partitions_launcher_guest.csv still matches $LAUNCHER_DIR/partitions.csv" >&2
  exit 1
fi
echo "    OK - identical"

cat <<EOF

==> Flash with (replace /dev/ttyUSB0 with your port):

python -m esptool --chip esp32s3 -p /dev/ttyUSB0 -b 460800 \\
  --before default_reset --after hard_reset write_flash \\
  --flash_mode dio --flash_size 8MB --flash_freq 80m \\
  0x0      $LAUNCHER_BUILD/bootloader/bootloader.bin \\
  0x8000   $LAUNCHER_BUILD/partition_table/partition-table.bin \\
  0xf000   $LAUNCHER_BUILD/ota_data_initial.bin \\
  0x20000  $LAUNCHER_BUILD/launcher.bin \\
  0x1a0000 $AQUARIUM_BUILD/firmware.bin

This flashes the launcher itself (bootloader/partition-table/otadata/
factory) plus ASCII Aquarium into app_slot1. First boot (no "last app"
remembered in NVS) shows the launcher's menu; picking Aquarium boots it.
Long-press K0 for 3s inside Aquarium returns to that menu.
EOF
