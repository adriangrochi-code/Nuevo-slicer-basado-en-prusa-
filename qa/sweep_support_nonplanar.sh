#!/bin/bash
# Supports (grid, snug, organic; automatic on/off) and non-planar modes (relative E, with supports) on the qa models.
# Usage: sweep_support_nonplanar.sh <tisma-engine> <models...>
E=$1; shift; n=0; f=0
for m in "$@"; do
  for st in grid snug organic; do for ao in true false; do n=$((n+1))
    echo "{\"support\":{\"enabled\":true,\"style\":\"$st\",\"automatic\":$ao}}" > c.json
    timeout 120 "$E" slice "$m" --config c.json -o o.gcode > l.txt 2>&1 || { f=$((f+1)); echo "FAIL $(basename "$m") $st auto=$ao: $(grep -m1 -iE 'error|failed' l.txt)"; }
  done; done
  for mode in wave conical overhangs surface automatic; do n=$((n+1))
    echo "{\"machine\":{\"relative_extrusion\":true},\"curved\":{\"mode\":\"$mode\"},\"support\":{\"enabled\":true}}" > c.json
    timeout 300 "$E" slice "$m" --config c.json -o o.gcode > l.txt 2>&1 || { f=$((f+1)); echo "FAIL $(basename "$m") curved=$mode: $(grep -m1 -iE 'error|failed' l.txt)"; }
  done
done
echo "runs=$n fails=$f"
