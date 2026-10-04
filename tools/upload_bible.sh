#!/bin/sh
# Pack the Bible (one zlib file per book) and the devotionals into a LittleFS image and
# flash it to the data partition (offset and size read from partitions.csv, label "spiffs").
# The score caches on that partition are wiped; they rebuild on the next fetch.
#   tools/upload_bible.sh [/dev/cu.usbmodem2101]   (no port: waits for the board to appear)
set -eu
cd "$(dirname "$0")/.."
# The port name can change after a reset (…101 vs …2101) and vanishes while the board sleeps: wait for one.
PORT=${1:-}
for i in $(seq 1 120); do
 [ -n "$PORT" ] && [ -e "$PORT" ] && break
 PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1); [ -n "$PORT" ] && break
 [ $i -eq 1 ] && echo "waiting for the board on USB..."; sleep 5
done
[ -n "$PORT" ] && [ -e "$PORT" ] || { echo "no board on USB"; exit 1; }
[ -f .tools/bible/fs/bible/bsb/66.z ] || .tools/venv/bin/python tools/build_bible.py
OFFSET=$(grep "^spiffs," firmware/RetroSports/partitions.csv | cut -d, -f4 | tr -d " ")
SIZE=$(grep "^spiffs," firmware/RetroSports/partitions.csv | cut -d, -f5 | tr -d " ")
# Pre-load the devotionals too (the device keeps them in sync over Wi-Fi afterwards).
mkdir -p .tools/bible/fs/devo && cp devotionals/out/*.json .tools/bible/fs/devo/ 2>/dev/null || true
# Scenes (dithered plates) and character cards.
rm -rf .tools/bible/fs/scenes .tools/bible/fs/chars; mkdir -p .tools/bible/fs/scenes .tools/bible/fs/chars && cp scenes/out/*.img scenes/days.json .tools/bible/fs/scenes/ 2>/dev/null; cp characters/out/index.json .tools/bible/fs/chars/ 2>/dev/null || true
MK=$(ls .tools/data/packages/esp32/tools/mklittlefs/*/mklittlefs | head -1)
"$MK" -c .tools/bible/fs -p 256 -b 4096 -s "$SIZE" .tools/bible/littlefs.bin >/dev/null
ls -l .tools/bible/littlefs.bin | awk '{print "image "$5" bytes"}'
.tools/venv/bin/python -m esptool --chip esp32s3 --port "$PORT" --baud 921600 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB "$OFFSET" .tools/bible/littlefs.bin || {
 sleep 5; PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1); echo "retrying on ${PORT:-no port}"  # the board may have reset or slept mid-upload
 .tools/venv/bin/python -m esptool --chip esp32s3 --port "$PORT" --baud 921600 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB "$OFFSET" .tools/bible/littlefs.bin; }
