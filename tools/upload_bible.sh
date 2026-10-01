#!/bin/sh
# Pack the Bible chapter files into a LittleFS image and flash it to the data
# partition (offset 0x810000, 0x7E0000 bytes, label "spiffs"). The score caches
# on that partition are wiped; they rebuild on the next fetch.
#   tools/upload_bible.sh /dev/cu.usbmodem2101
set -eu
cd "$(dirname "$0")/.."
PORT=${1:-/dev/cu.usbmodem1101}
[ -d .tools/bible/fs/bible ] || .tools/venv/bin/python tools/build_bible.py
# Pre-load the devotionals too (the device keeps them in sync over Wi-Fi afterwards).
mkdir -p .tools/bible/fs/devo && cp devotionals/out/*.json .tools/bible/fs/devo/ 2>/dev/null || true
MK=$(ls .tools/data/packages/esp32/tools/mklittlefs/*/mklittlefs | head -1)
"$MK" -c .tools/bible/fs -p 256 -b 4096 -s 0x7E0000 .tools/bible/littlefs.bin >/dev/null
ls -l .tools/bible/littlefs.bin | awk '{print "image "$5" bytes"}'
.tools/venv/bin/python -m esptool --chip esp32s3 --port "$PORT" --baud 921600 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x810000 .tools/bible/littlefs.bin
