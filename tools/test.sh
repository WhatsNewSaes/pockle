#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
clang++ -std=c++17 -I firmware/RetroSports -I .tools/user/libraries/ArduinoJson/src tests/core_test.cpp -o /tmp/retro-tests
/tmp/retro-tests
clang++ -std=c++17 -DARDUINO=100 -DARDUINOJSON_ENABLE_PROGMEM=0 -DARDUINOJSON_ENABLE_ARDUINO_STRING=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -I tests/host -I firmware/RetroSports -I .tools/user/libraries/ArduinoJson/src -I .tools/user/libraries/Adafruit_GFX_Library tests/render_preview.cpp .tools/user/libraries/Adafruit_GFX_Library/Adafruit_GFX.cpp -o /tmp/retro-render
/tmp/retro-render
.tools/venv/bin/python -c 'from PIL import Image; from pathlib import Path; [Image.open(p).transpose(Image.Transpose.ROTATE_270).save(p.with_suffix(".png")) for p in Path("previews").glob("*.pbm")]'

node tests/portal_test.cjs
clang++ -std=c++17 -I tests/host -I firmware/RetroSports -I .tools/user/libraries/ArduinoJson/src tests/network_body_test.cpp -o /tmp/retro-network-test
/tmp/retro-network-test
