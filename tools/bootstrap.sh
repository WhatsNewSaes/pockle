#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
mkdir -p .tools/data .tools/downloads .tools/user
if [ ! -x .tools/arduino-cli ]; then
 curl -fL https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_macOS_ARM64.tar.gz -o .tools/arduino-cli.tar.gz
 tar -xzf .tools/arduino-cli.tar.gz -C .tools arduino-cli
fi
cat > .tools/arduino.yaml <<YAML
directories:
  data: $ROOT/.tools/data
  downloads: $ROOT/.tools/downloads
  user: $ROOT/.tools/user
board_manager:
  additional_urls:
    - https://espressif.github.io/arduino-esp32/package_esp32_index.json
YAML
.tools/arduino-cli --config-file .tools/arduino.yaml version > .tools/arduino-cli-version.txt
.tools/arduino-cli --config-file .tools/arduino.yaml core update-index
.tools/arduino-cli --config-file .tools/arduino.yaml core install esp32:esp32@3.3.0
.tools/arduino-cli --config-file .tools/arduino.yaml lib install 'ArduinoJson@7.4.2' 'Adafruit GFX Library@1.12.1' 'Adafruit BusIO@1.17.4'
python3 -m venv .tools/venv
.tools/venv/bin/pip install esptool==4.9.0 pillow pyserial
