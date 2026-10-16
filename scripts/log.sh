#!/bin/sh
set -e

port="$1"
baud="$2"
header="${3:-}"
board="${4:-}"

mkdir -p data
base=data/log_${board:+${board}_}$(date +%Y%m%d_%H%M%S)

PY=${PYTHON:-$(command -v python3 || command -v python || command -v py)}
# MONITOR_CMD lets tests substitute a fake serial source.
MONITOR_CMD=${MONITOR_CMD:-arduino-cli monitor --quiet --port "$port" --config baudrate="$baud"}

finish() {
    echo
    if [ -f "$base.csv" ]; then
        rows=$(( $(wc -l < "$base.csv") - 1 ))
        errs=$(grep -c '^#' "$base.log" 2>/dev/null || true)
        echo "Saved $rows data rows, ${errs:-0} comment/error lines"
        echo "  $base.csv"
        echo "  $base.log"
    else
        echo "No CSV rows received."
        echo "Raw output, if any, is in:"
        echo "  $base.log"
    fi
}

trap finish EXIT
trap 'exit 130' INT TERM HUP

echo "Logging to:"
echo "  $base.log"
echo "  $base.csv"
echo "Press Ctrl+C to stop."
echo

eval "$MONITOR_CMD" | "$PY" "$(dirname "$0")/split_generic.py" "$base" "$header"
