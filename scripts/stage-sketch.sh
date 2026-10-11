#!/bin/sh
# Stage src/main.cpp (or main.ino) as <BUILD>/sketch/sketch.ino for arduino-cli.
set -e

BUILD="$1"
SKETCH="$BUILD/sketch"

rm -rf "$SKETCH"
mkdir -p "$SKETCH"
cp -r src/. "$SKETCH/"

for f in main.cpp main.ino; do
    [ -f "$SKETCH/$f" ] && mv "$SKETCH/$f" "$SKETCH/sketch.ino"
done

if [ ! -f "$SKETCH/sketch.ino" ]; then
    echo "stage-sketch.sh: expected src/main.cpp or src/main.ino" >&2
    exit 1
fi
