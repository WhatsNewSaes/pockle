#!/bin/sh
# Publish a firmware release that devices pick up over Wi-Fi.
#   tools/release.sh 1.5.0 "What changed"
# Sets FIRMWARE_VERSION, runs the tests and build, commits, tags v<version>, pushes,
# and creates a GitHub release carrying RetroSports.bin and version.json. Devices
# check the latest release at their 6:30 am wake and from Settings.
set -eu
cd "$(dirname "$0")/.."
VERSION=${1:?usage: tools/release.sh <version> [notes]}
NOTES=${2:-"Pockle $VERSION"}
case "$VERSION" in *.*.*) ;; *) echo "version must look like 1.5.0" >&2; exit 1;; esac
sed -i '' "s/#define FIRMWARE_VERSION \"[^\"]*\"/#define FIRMWARE_VERSION \"$VERSION\"/" firmware/RetroSports/Version.h
./tools/test.sh >/dev/null
./tools/build.sh | grep "Sketch uses"
mkdir -p release
cp build/RetroSports.ino.bin release/RetroSports.bin
printf '{"version":"%s","file":"RetroSports.bin","size":%s}\n' "$VERSION" "$(stat -f %z release/RetroSports.bin)" > release/version.json
git add -A
git commit -q -m "Release v$VERSION" || true
git tag -a "v$VERSION" -m "Pockle $VERSION"
git push -q --follow-tags
gh release create "v$VERSION" release/RetroSports.bin release/version.json --title "v$VERSION" --notes "$NOTES"
echo "released v$VERSION"
