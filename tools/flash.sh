#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
PORT=${1:-/dev/cu.usbmodem1101}
.tools/venv/bin/python -m esptool --chip esp32s3 --port "$PORT" --baud 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 build/RetroSports.ino.bootloader.bin 0x8000 build/RetroSports.ino.partitions.bin 0xe000 .tools/data/packages/esp32/hardware/esp32/3.3.0/tools/partitions/boot_app0.bin 0x10000 build/RetroSports.ino.bin
