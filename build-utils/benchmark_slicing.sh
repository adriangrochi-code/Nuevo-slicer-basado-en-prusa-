#!/usr/bin/env bash
# Tisma Slicer: benchmark of the slicing (phase 8).
#
# Slices a fixed set of models from the command line several times and prints the median total time and the median
# time of every step of the slicing (from the log of PrusaSlicer). The G-code of the first run is kept, so that two
# builds can be compared with `cmp` / `diff` (an optimization must not change the G-code, or it must be explained).
#
# Usage: build-utils/benchmark_slicing.sh [binary] [runs] [output dir]
#   binary:     default build/src/prusa-slicer
#   runs:       default 3
#   output dir: default /tmp/tisma-bench
set -euo pipefail

BIN=${1:-build/src/prusa-slicer}
RUNS=${2:-3}
OUT=${3:-/tmp/tisma-bench}
SHAPES=resources/shapes
mkdir -p "$OUT"

# name | model | extra options
CASES=(
  "benchy|$SHAPES/3DBenchy.stl|"
  "benchy_x2_gyroid|$SHAPES/3DBenchy.stl|--scale 200% --fill-pattern gyroid --fill-density 15%"
  "benchy_supports|$SHAPES/3DBenchy.stl|--support-material --support-material-auto"
  "benchy_organic|$SHAPES/3DBenchy.stl|--support-material --support-material-style organic"
  "bunny_x1.5|$SHAPES/bunny.stl|--scale 150%"
  "screw_x4|$SHAPES/M3x10_screw.stl|--scale 400% --fill-density 40%"
)

# Markers of the log (in order); every column is the time from a marker to the next one.
STEPS=("Slicing volumes" "Generating perimeters" "Detecting solid surfaces" "Preparing fill surfaces"
       "Processing external surfaces" "Bridge over infill - Start" "Bridge over infill - End"
       "Slicing process finished" "Exporting G-code finished")
COLUMNS=("Slicing" "Perim" "Solid" "Shells" "ExtSurf" "Bridge" "Infill+" "G-code")

median() { sort -g | awk '{ a[NR] = $1 } END { if (NR == 0) print "-"; else print a[int((NR + 1) / 2)] }'; }
to_seconds() { awk -F: '{ printf "%.3f\n", $1 * 3600 + $2 * 60 + $3 }'; }

printf "%-20s %8s" "case" "total"
for s in "${COLUMNS[@]}"; do printf " %8s" "$s"; done
printf "\n"

for c in "${CASES[@]}"; do
  IFS='|' read -r name model opts <<< "$c"
  totals=()
  declare -A step_times=()
  for ((r = 0; r < RUNS; ++r)); do
    log="$OUT/$name.$r.log"
    gcode="$OUT/$name.gcode"
    [ "$r" -gt 0 ] && gcode="$OUT/$name.$r.gcode"
    start=$(date +%s.%N)
    # shellcheck disable=SC2086
    "$BIN" --export-gcode "$model" $opts -o "$gcode" --loglevel 3 > "$log" 2>&1
    end=$(date +%s.%N)
    totals+=("$(echo "$end - $start" | bc)")
    [ "$r" -gt 0 ] && rm -f "$gcode"
    ts=()
    for s in "${STEPS[@]}"; do
      t=$(grep -F "$s" "$log" | grep -m1 '^\[' | sed -E 's/^\[[0-9-]+ ([0-9:.]+)\].*/\1/' | to_seconds || true)
      ts+=("${t:-}")
    done
    for ((i = 0; i + 1 < ${#STEPS[@]}; ++i)); do
      a=${ts[$i]}; b=${ts[$((i + 1))]}
      if [ -n "$a" ] && [ -n "$b" ]; then
        step_times[$i]+="$(echo "$b - $a" | bc) "
      fi
    done
  done
  printf "%-20s %8.2f" "$name" "$(printf "%s\n" "${totals[@]}" | median)"
  for ((i = 0; i < ${#COLUMNS[@]}; ++i)); do
    v=$(printf "%s\n" ${step_times[$i]:-} | median)
    if [ "$v" = "-" ]; then printf " %8s" "-"; else printf " %8.2f" "$v"; fi
  done
  printf "\n"
  unset step_times
done
