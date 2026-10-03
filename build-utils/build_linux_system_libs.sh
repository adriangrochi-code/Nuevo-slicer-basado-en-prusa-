#!/usr/bin/env bash
# Compila en Linux (Ubuntu 24.04) con las librerías del sistema, sin descargar las
# dependencias de deps/ (muchas vienen de github.com).
#
#   build-utils/build_linux_system_libs.sh          # configurar y compilar
#   ctest --test-dir build --output-on-failure      # tests
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "${1:-}" == "--deps" ]]; then
  sudo sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/ubuntu.sources
  sudo apt-get update
  sudo apt-get build-dep -y prusa-slicer
  sudo apt-get install -y libwebkit2gtk-4.1-dev libwxgtk-webview3.2-dev libsecret-1-dev libhidapi-dev \
      libz3-dev ninja-build ccache
fi
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSLIC3R_STATIC=OFF -DSLIC3R_GTK=3 -DSLIC3R_WX_STABLE=1 -DSLIC3R_PCH=OFF -DBUILD_TESTING=1 \
  -DOPENVDB_FIND_MODULE_PATH=/usr/lib/x86_64-linux-gnu/cmake/OpenVDB \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
ninja -C build
