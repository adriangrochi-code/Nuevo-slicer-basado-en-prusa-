#!/bin/bash
export DISPLAY=:77
SP=/tmp/claude-0/-home-user-Nuevo-slicer-basado-en-prusa-/f17f8ca4-2774-5edc-a3fd-f193f43ef743/scratchpad
pkill -x tisma; sleep 1
rm -rf $SP/ui/home3; mkdir -p $SP/ui/home3; cp -r $SP/ui/home/. $SP/ui/home3/
cd $SP/ui; HOME=$SP/ui/home3 setsid nohup $SP/eb/apps/tisma/tisma $SP/bughunt/models/c20x10.stl --lang es > app.log 2>&1 < /dev/null &
sleep 8
xdotool getactivewindow getwindowgeometry
xdotool key Escape; sleep 0.5
xdotool mousemove 326 28 click 1; sleep 1.5
xdotool mousemove 299 236 click 1; sleep 0.7
eval "$1"; sleep 1.5
import -window root $SP/ui/$2.png
