#!/usr/bin/env python3
"""Read project.yaml (flat keys + one list) and print Makefile assignments.

Supported: `key: value`, `key: [a, b]`, `key: []`, and `- item` lists.
Emits `NAME ?= value` so environment / command-line values still win.
"""
import re
import sys

KEYS = {"board": "BOARD", "csv_header": "CSV_HEADER", "port": "PORT", "libs": "LIBS"}


def clean(v):
    v = re.sub(r"\s+#.*$", "", v.strip())
    if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'":
        v = v[1:-1]
    return v


values, cur = {}, None
try:
    lines = open(sys.argv[1] if len(sys.argv) > 1 else "project.yaml").read().splitlines()
except OSError as e:
    sys.exit(f"config.py: {e}")

for line in lines:
    if not line.strip() or line.lstrip().startswith("#"):
        continue
    m = re.match(r"^-\s+(.*)$", line.strip())
    if m and cur:
        values.setdefault(cur, []).append(clean(m.group(1)))
        continue
    m = re.match(r"^([A-Za-z_]\w*)\s*:\s*(.*)$", line)
    if not m:
        sys.exit(f"config.py: cannot parse line: {line!r}")
    key, val = m.group(1), clean(m.group(2))
    if key not in KEYS:
        sys.exit(f"config.py: unknown key {key!r} (allowed: {', '.join(KEYS)})")
    cur = key
    if val.startswith("[") and val.endswith("]"):
        values[key] = [clean(x) for x in val[1:-1].split(",") if x.strip()]
    elif val:
        values[key] = val
    else:
        values[key] = []  # block list follows

for key, name in KEYS.items():
    v = values.get(key)
    if not v:
        continue
    if isinstance(v, list):
        v = ";".join(v)
    v = v.replace("$", "$$").replace("#", r"\#")
    print(f"{name} ?= {v}")
