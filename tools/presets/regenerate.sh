#!/usr/bin/env bash
# Regenera resources/presets/nps-community-fff desde los paquetes de PrusaSlicer 2.x.
#
#   tools/presets/regenerate.sh                 # todos los fabricantes FFF
#   tools/presets/regenerate.sh Creality Voron  # sólo esos
#   PS29_TAG=version_2.9.6 PS29_PROFILES=/ruta/resources/profiles tools/presets/regenerate.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
TAG=${PS29_TAG:-version_2.9.6}
SRC=${PS29_PROFILES:-}
if [[ -z "$SRC" ]]; then
  TMP=$(mktemp -d)
  trap 'rm -rf "$TMP"' EXIT
  git clone -q --depth 1 --filter=blob:none --sparse --branch "$TAG" \
      https://github.com/prusa3d/PrusaSlicer "$TMP/ps29"
  git -C "$TMP/ps29" sparse-checkout set resources/profiles
  SRC="$TMP/ps29/resources/profiles"
fi
OUT=resources/presets/nps-community-fff
python3 tools/presets/extract_schema.py > tools/presets/schema30.json
python3 -m tools.presets.convert --src "$SRC" --out "$OUT" "$@"
python3 -m tools.presets.validate30 "$OUT"/*/
