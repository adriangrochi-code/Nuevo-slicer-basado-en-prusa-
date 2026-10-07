#!/usr/bin/env bash
# Tisma Slicer: Linux AppImage from a build tree.
#
#   packaging/linux/make_appimage.sh <build dir> <version> <output dir>
#
# The dependencies of deps/ are linked statically; GTK 3, WebKitGTK 4.1, OpenGL and D-Bus come from the system, as in
# the AppImage of PrusaSlicer. tisma-gcodeviewer in the AppImage name (or ARGV0) starts the G-code viewer.
# appimagetool (MIT) is downloaded unless APPIMAGETOOL points to it.
#
# PrusaSlicer is released under the terms of the AGPLv3 or higher
set -euo pipefail

build_dir=$(realpath "$1")
version=$2
out_dir=$(realpath -m "$3")
src_dir=$(realpath "$(dirname "$0")/../..")
arch=$(uname -m)

binary="$build_dir/src/tisma-slicer"
[ -x "$binary" ] || { echo "tisma-slicer not found in $build_dir/src" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
app="$work/TismaSlicer.AppDir"
mkdir -p "$app/usr/bin" "$out_dir"

cp "$binary" "$app/usr/bin/tisma-slicer"
ln -s tisma-slicer "$app/usr/bin/tisma-gcodeviewer"
# The binary finds its data in ../resources (src/CLI/Setup.cpp).
cp -r "$src_dir/resources" "$app/usr/resources"
strip --strip-unneeded "$app/usr/bin/tisma-slicer" || true

cat > "$app/AppRun" <<'EOF'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
case "${ARGV0:-$0}" in
    *gcodeviewer*) exec "$HERE/usr/bin/tisma-slicer" --gcodeviewer "$@" ;;
esac
exec "$HERE/usr/bin/tisma-slicer" "$@"
EOF
chmod +x "$app/AppRun"

cp "$src_dir/src/platform/unix/TismaSlicer.desktop" "$app/TismaSlicer.desktop"
cp "$src_dir/resources/icons/PrusaSlicer_192px.png" "$app/TismaSlicer.png"
mkdir -p "$app/usr/share/icons/hicolor/192x192/apps"
cp "$src_dir/resources/icons/PrusaSlicer_192px.png" "$app/usr/share/icons/hicolor/192x192/apps/TismaSlicer.png"

tool=${APPIMAGETOOL:-}
if [ -z "$tool" ]; then
    tool="$work/appimagetool"
    curl -fsSL -o "$tool" "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$arch.AppImage"
    chmod +x "$tool"
fi

output="$out_dir/TismaSlicer-$version-linux-$arch.AppImage"
# Without FUSE (CI containers): run appimagetool extracted.
ARCH=$arch "$tool" --appimage-extract-and-run --no-appstream "$app" "$output"
echo "$output"
