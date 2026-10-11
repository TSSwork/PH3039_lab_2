#!/usr/bin/env bash
# Generate .clangd from the real arduino-cli compile flags for a board.
set -e

BOARD="${1:-uno}"
BOARDFILE="boards/${BOARD}.env"
[ -f "$BOARDFILE" ] || { echo "Board file not found: $BOARDFILE" >&2; exit 1; }

# shellcheck disable=SC1090
source "$BOARDFILE"
[ -n "$FQBN" ] || { echo "FQBN is not set in $BOARDFILE" >&2; exit 1; }

BUILD="build/clangd-$BOARD"
./scripts/stage-sketch.sh "$BUILD"

LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

echo "Compiling with arduino-cli to collect compile flags..."
arduino-cli compile --fqbn "$FQBN" --verbose "$BUILD/sketch" >"$LOG" 2>&1 || true

PY=$(command -v python3 || command -v python)
"$PY" - "$LOG" "$BOARD" <<'PY'
import json
import shlex
import sys

text = open(sys.argv[1], errors="replace").read()
board = sys.argv[2]

includes, defines = [], []
for line in text.splitlines():
    if " -c " not in line and " -o " not in line:
        continue
    # Tokenise like a shell so -DNAME="a b" stays one flag.
    try:
        tokens = shlex.split(line)
    except ValueError:
        continue
    for i, t in enumerate(tokens):
        if t.startswith("-D") and len(t) > 2:
            defines.append(t)
        elif t.startswith("-I") and len(t) > 2:
            includes.append(t)
        elif t == "-I" and i + 1 < len(tokens):
            includes.append("-I" + tokens[i + 1])

def uniq(items):
    return list(dict.fromkeys(items))

defines, includes = uniq(defines), uniq(includes)

if not includes:
    sys.exit("No include paths found: does the sketch compile? "
             "Check `make BOARD=%s build` and that the core is installed." % board)

flags = ["-xc++", "-std=gnu++17", "-ferror-limit=0", "-Wno-unknown-attributes"]

with open(".clangd", "w") as f:
    f.write("CompileFlags:\n  Compiler: clang++\n  Add:\n")
    for flag in flags + defines + includes:
        f.write("    - " + json.dumps(flag) + "\n")
    f.write("  Remove:\n    - -m*\n    - -ffunction-sections\n    - -fdata-sections\n")
    f.write("\nDiagnostics:\n  UnusedIncludes: None\n  Suppress:\n    - pp_file_not_found\n")

print(f"Wrote .clangd for '{board}': {len(defines)} defines, {len(includes)} includes")
PY
echo "Now restart clangd in your editor."
