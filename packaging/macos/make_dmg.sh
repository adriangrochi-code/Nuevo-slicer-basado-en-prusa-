#!/usr/bin/env bash
# Tisma Slicer: macOS application bundle and disk image from a build tree.
#
#   packaging/macos/make_dmg.sh <build dir> <version> <arch> <output dir>
#
# The bundle is signed ad hoc (needed to run on Apple silicon) but not with a Developer ID nor notarized: on the first
# start, right click > Open (or System Settings > Privacy & Security > Open Anyway). With an Apple Developer account,
# set MACOS_SIGN_IDENTITY (and notarize the disk image) to sign it for real.
#
# PrusaSlicer is released under the terms of the AGPLv3 or higher
set -euo pipefail

build_dir=$(cd "$1" && pwd)
version=$2
arch=$3
mkdir -p "$4"
out_dir=$(cd "$4" && pwd)
src_dir=$(cd "$(dirname "$0")/../.." && pwd)

binary="$build_dir/src/TismaSlicer"
[ -x "$binary" ] || { echo "TismaSlicer not found in $build_dir/src" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
app="$work/Tisma Slicer.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"

cp "$binary" "$app/Contents/MacOS/TismaSlicer"
# The G-code viewer is the same binary started with --gcodeviewer (src/slic3r/Utils/Process.cpp).
ln -s TismaSlicer "$app/Contents/MacOS/tisma-gcodeviewer"
cp "$build_dir/src/Info.plist" "$app/Contents/Info.plist"
# The binary finds its data in ../Resources (src/CLI/Setup.cpp).
cp -R "$src_dir/resources/" "$app/Contents/Resources/"
cp "$src_dir/resources/icons/PrusaSlicer.icns" "$app/Contents/Resources/PrusaSlicer.icns"
cp "$src_dir/resources/icons/gcode.icns" "$app/Contents/Resources/gcode.icns" 2>/dev/null || true
printf 'APPL????' > "$app/Contents/PkgInfo"

identity=${MACOS_SIGN_IDENTITY:--}
if [ "$identity" = "-" ]; then
    codesign --force --deep --sign - "$app"
else
    codesign --force --deep --options runtime --timestamp \
        --entitlements "$src_dir/src/platform/osx/entitlements.plist" --sign "$identity" "$app"
fi
codesign --verify --deep --strict "$app"

# Smoke test: the bundled binary starts and finds its resources (an x86_64 build on Apple silicon needs Rosetta).
if [ "$arch" = "$(uname -m)" ] || arch -x86_64 /usr/bin/true 2> /dev/null; then
    # sed reads the whole output (head would close the pipe early: SIGPIPE with pipefail).
    "$app/Contents/MacOS/TismaSlicer" --help | sed -n 1,2p
else
    echo "Smoke test skipped: $arch binaries cannot run on this machine"
fi

stage="$work/dmg"
mkdir -p "$stage"
mv "$app" "$stage/"
ln -s /Applications "$stage/Applications"
output="$out_dir/TismaSlicer-$version-macos-$arch.dmg"
hdiutil create -volname "Tisma Slicer $version" -srcfolder "$stage" -ov -format UDZO "$output"
echo "$output"
