#!/bin/bash
# Builds Tisma's deps/ for the Android app (64-bit ARM). Usage: build-deps.sh <build dir> <install dir>
# Needs ANDROID_NDK_ROOT, cmake >= 3.22, ninja and, for GMP and MPFR, make, autoconf, automake, libtool and texinfo.
# The core is then built by Gradle with -PtismaDepsDir=<install dir>/usr/local.
set -euo pipefail

BUILD_DIR=$(realpath -m "$1")
DEST_DIR=$(realpath -m "$2")
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
: "${ANDROID_NDK_ROOT:?set ANDROID_NDK_ROOT to the NDK}"

API=26
BIN="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin"
# The autotools packages (GMP, MPFR) take the compilers from the environment.
export CC="$BIN/aarch64-linux-android$API-clang" CXX="$BIN/aarch64-linux-android$API-clang++"
export AR="$BIN/llvm-ar" RANLIB="$BIN/llvm-ranlib" STRIP="$BIN/llvm-strip" NM="$BIN/llvm-nm"

# oneTBB 2021.5 copies libc++_shared.so from where NDKs before r23 had it: point that old place to the current one.
OLD_STL="$ANDROID_NDK_ROOT/sources/cxx-stl/llvm-libc++/libs/arm64-v8a/libc++_shared.so"
if [ ! -e "$OLD_STL" ]; then
    mkdir -p "$(dirname "$OLD_STL")"
    ln -s "$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" "$OLD_STL"
fi

# Not needed by the app: the desktop GUI (wxWidgets, GLEW, OpenCSG), network (CURL, OpenSSL: the app uses Android's),
# tests (Catch2), STEP import (OCCT, later) and sequential arrangement (z3). ZLIB comes with the NDK.
cmake -S "$ROOT/deps" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$HERE/android-arm64.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DDESTDIR="$DEST_DIR" \
    "-DPrusaSlicer_deps_PACKAGE_EXCLUDES=wxWidgets|GLEW|OpenCSG|CURL|OpenSSL|Catch2|OCCT|z3" \
    -DPrusaSlicer_deps_PLATFORM_PACKAGES=ZLIB
# One package at a time (each one builds in parallel itself).
cmake --build "$BUILD_DIR" -j 1
