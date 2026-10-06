#!/bin/sh
# tools/flash_nano33ble.sh - flash a Zephyr build to an Arduino Nano 33 BLE
# from WSL, using Arduino's own bossac.exe on Windows.
#
#   1. Double-tap RESET on the board (orange LED pulses slowly).
#   2. powershell.exe -NoProfile -c "[System.IO.Ports.SerialPort]::GetPortNames()"
#   3. sh tools/flash_nano33ble.sh COM7 [build_dir]     (default ~/build/vib)
#
# NO --offset IS PASSED, on purpose. Arduino's build of bossac (1.9.1-arduino2)
# writes at the bootloader's application address, 0x10000, by itself: Arduino's
# own core links sketches at 0x10000 and uploads them with no offset
# (ArduinoCore-mbed: variants/ARDUINO_NANO33BLE/linker_script.ld, platform.txt).
# Zephyr's west runner adds "-o 0x10000" on top, which put the image at 0x20000
# on this board, so the bootloader started the old Arduino sketch instead.
# The Zephyr image is linked for 0x10000 (code partition), so the same call the
# Arduino IDE makes is the right one.
#
# Guard rails:
#   - refuses an image bigger than the code partition (0xE8000 bytes)
#   - refuses to flash unless bossac reports an nRF52840 with the Arduino
#     bootloader on that port
#   - checks bossac's exit status and the "Verify successful" line
set -eu

PORT="${1:-}"
BUILD="${2:-$HOME/build/vib}"
LIMIT=$((0xE8000))
BOSSAC="${BOSSAC:-/mnt/c/Users/baliy/AppData/Local/Arduino15/packages/arduino/tools/bossac/1.9.1-arduino2/bossac.exe}"
STAGE_DIR="${STAGE_DIR:-/mnt/c/zephyr_flash}"       # same folder as seen from Windows:
STAGE_WIN="${STAGE_WIN:-C:\\zephyr_flash}"          #   C:\zephyr_flash

die() { echo "STOPPED: $*" >&2; exit 1; }

case "$PORT" in
  COM[0-9]|COM[0-9][0-9]|COM[0-9][0-9][0-9]) ;;
  *) die "usage: sh tools/flash_nano33ble.sh COMx [build_dir]   (get the port after double-tapping RESET)" ;;
esac
BIN="$BUILD/zephyr/zephyr.bin"
[ -f "$BIN" ] || die "no $BIN - build first"
SIZE=$(wc -c < "$BIN" | tr -d ' ')
[ "$SIZE" -gt 0 ] || die "$BIN is empty"
[ "$SIZE" -le "$LIMIT" ] || die "image is $SIZE bytes, the code partition holds $LIMIT"
[ -x "$BOSSAC" ] || die "bossac.exe not found at $BOSSAC (set BOSSAC=... to its path)"

INFO=$("$BOSSAC" --port="$PORT" -i 2>&1) || die "bossac could not talk to $PORT (is the board in bootloader mode? double-tap RESET):
$INFO"
INFO=$(printf '%s\n' "$INFO" | tr -d '\r')
echo "$INFO" | grep -q "nRF52840" || die "$PORT is not an nRF52840:
$INFO"
echo "$INFO" | grep -q "Arduino Bootloader" || die "no Arduino bootloader answered on $PORT:
$INFO"
echo "Found: $(echo "$INFO" | grep -m1 'Device') / $(echo "$INFO" | grep -m1 'Version')"

mkdir -p "$STAGE_DIR"
cp "$BIN" "$STAGE_DIR/app.bin"
echo "Flashing $SIZE bytes (no --offset: bossac writes at 0x10000, like the Arduino IDE) ..."
# same options as the Arduino IDE: -U -i -e -w ... -R, plus -v to verify
OUT=$("$BOSSAC" --port="$PORT" -U -i -e -w -v "$STAGE_WIN\\app.bin" -R 2>&1) || {
  printf '%s\n' "$OUT" | tr -d '\r'
  die "bossac reported an error; the bootloader is untouched - double-tap RESET and try again"
}
OUT=$(printf '%s\n' "$OUT" | tr -d '\r')
echo "$OUT"
echo "$OUT" | grep -q "Verify successful" || die "the write was not verified"
echo "Done. The board resets into the application."
