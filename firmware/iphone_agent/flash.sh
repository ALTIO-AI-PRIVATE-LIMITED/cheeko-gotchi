#!/usr/bin/env bash
# Build and flash the iPhone Agent firmware onto a Cheeko Gotchi (needs git, arduino-cli, the esp32 core
# 3.3.x, NimBLE-Arduino 2.5.x and Adafruit GFX Library).
# Usage: firmware/iphone_agent/flash.sh [port] [--debug] [--build-only]
#   --debug       log commands and the AirPlay handshake on USB serial
#   --build-only  compile only (prints flash/RAM usage)
#
# Most of the firmware is ESP32 iPhone Agent's board firmware (hardware/hid_bridge), fetched at a pinned
# commit into build/. Its AirPlay receiver is GPL-3.0, so it is not kept in this repository, and the
# binary this script builds is GPL-3.0 (see README.md). The sketch is assembled as: upstream hid_bridge/
# without hid_bridge.ino, plus the files in iphone_agent/ (iphone_agent.ino is the adapted hid_bridge.ino).
# Flashing follows upstream hardware/flash.sh: a running board is a TinyUSB device, so it is first kicked
# into the ROM bootloader with a 1200-baud touch; a blank board is flashed directly.
set -euo pipefail
cd "$(dirname "$0")"
UPSTREAM=https://github.com/onehumanbeing/esp32-iphone-agent
REV=680d22768ef6c67968c1663b99c9870c9c647ab2
# XIAO profile, not the generic esp32s3 one: GPIO 39/40 are flaky there (SKILL.md, section 3). Upstream's
# CDCOnBoot=cdc is spelled CDCOnBoot=default on this profile (USB CDC on boot enabled, for the serial console).
FQBN="${FQBN:-esp32:esp32:XIAO_ESP32S3:USBMode=default,CDCOnBoot=default,PSRAM=opi,PartitionScheme=default_8MB}"
ports() { for p in /dev/cu.usbmodem* /dev/ttyACM*; do [[ -e $p ]] && echo "$p"; done; return 0; }
PORT="" DEFS="" BUILD_ONLY=0
for a in "$@"; do
  case $a in
    --debug) DEFS+=" -DDEBUG_SERIAL=1" ;;
    --build-only) BUILD_ONLY=1 ;;
    *) PORT=$a ;;
  esac
done

SRC=build/upstream-$REV
if [[ ! -d $SRC ]]; then
  rm -rf "$SRC.tmp"
  git init -q "$SRC.tmp"
  git -C "$SRC.tmp" fetch -q --depth 1 "$UPSTREAM" "$REV"
  git -C "$SRC.tmp" checkout -q FETCH_HEAD
  mv "$SRC.tmp" "$SRC"
fi
SKETCH=build/iphone_agent
rm -rf "$SKETCH"
mkdir -p "$SKETCH"
cp -R "$SRC/hardware/hid_bridge/." "$SKETCH/"
rm "$SKETCH/hid_bridge.ino"
cp iphone_agent/* "$SKETCH/"

[[ $PWD != *" "* ]] || { echo "the -include path below cannot contain spaces: $PWD"; exit 1; }
DEFS+=" -include $PWD/$SKETCH/nimble_cfg.h"
FLAGS=(--build-property "compiler.cpp.extra_flags=$DEFS" --build-property "compiler.c.extra_flags=$DEFS")

arduino-cli compile -b "$FQBN" "${FLAGS[@]}" --output-dir build/out "$SKETCH"
(( BUILD_ONLY )) && exit 0
BEFORE=$(ports)
PORT=${PORT:-$(head -n 1 <<<"$BEFORE")}
[[ -n $PORT ]] || { echo "no board found (looked for /dev/cu.usbmodem* and /dev/ttyACM*)"; exit 1; }
stty -f "$PORT" 1200 2>/dev/null || stty -F "$PORT" 1200 2>/dev/null || true
# The ROM bootloader may come back under a new name. Follow the port that appeared after the
# reset rather than the first one listed, so other connected boards are never picked up.
new_ports() { comm -13 <(sort <<<"$BEFORE") <(ports | sort); }
for _ in $(seq 20); do sleep 0.5; [[ -n $(new_ports) ]] && break; done
sleep 1  # let the ROM bootloader's serial come up
NEW=$(new_ports)
case $(grep -c . <<<"$NEW" || true) in
  0) [[ -e $PORT ]] || { echo "$PORT is gone after the reset and no new port appeared"; exit 1; } ;;
  1) PORT=$NEW ;;
  *) echo "several ports appeared after the reset, not guessing:" $NEW; exit 1 ;;
esac
arduino-cli upload -b "$FQBN" -p "$PORT" --input-dir build/out "$SKETCH"
