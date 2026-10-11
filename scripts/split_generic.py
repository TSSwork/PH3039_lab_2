#!/usr/bin/env python3
"""Split serial monitor lines (stdin) into <base>.log (every line) and
<base>.csv (data rows only).

- Lines starting with '#' are comments/errors and stay out of the CSV.
- With an expected header: wait for it; if not seen within GRACE seconds,
  create the CSV with the expected header anyway.
- Without one: the first non-comment line becomes the header.
"""
import sys
import time

base = sys.argv[1]
expected = sys.argv[2].strip() if len(sys.argv) > 2 else ""

GRACE = 3.0

log = open(base + ".log", "w", buffering=1)
csv = None
pending = []
start = time.time()


def open_csv(header):
    global csv
    csv = open(base + ".csv", "w", buffering=1)
    if header:
        csv.write(header + "\n")


def flush_pending():
    global pending
    for line in pending:
        csv.write(line + "\n")
    pending = []


try:
    for raw in sys.stdin:
        line = raw.rstrip("\r\n")
        print(line, flush=True)
        log.write(line + "\n")

        if not line or line.startswith("#"):
            continue

        if csv is None:
            if expected:
                if line == expected:
                    open_csv(expected)
                    pending = []
                else:
                    pending.append(line)
                    if time.time() - start > GRACE:
                        open_csv(expected)
                        flush_pending()
            else:
                open_csv(line)
        else:
            if expected and line == expected:
                continue  # repeated header after a board reset
            csv.write(line + "\n")
except KeyboardInterrupt:
    pass
