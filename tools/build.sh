#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 tools/patch_esp32.py
.tools/arduino-cli --config-file .tools/arduino.yaml compile --fqbn 'esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=16M,PSRAM=opi,PartitionScheme=custom' --build-property upload.maximum_size=2621440 --build-path build firmware/RetroSports
