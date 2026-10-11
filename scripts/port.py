"""Prints the serial port of the connected Arduino (works on Linux, macOS, Windows).
Override with PORT=<port> (env var or `make PORT=...`)."""
import json
import os
import subprocess
import sys

if os.environ.get("PORT"):
    print(os.environ["PORT"])
    sys.exit(0)

try:
    out = subprocess.run(["arduino-cli", "board", "list", "--format", "json"],
                         capture_output=True, text=True, check=True).stdout
except (OSError, subprocess.CalledProcessError) as e:
    sys.exit(f"port.py: arduino-cli board list failed: {e}")

data = json.loads(out)
ports = data.get("detected_ports", data) if isinstance(data, dict) else data

def serial(p):
    return p["port"].get("protocol") == "serial"

# Best: a recognised board. Next: any USB serial device (has a vendor id).
for want in (lambda p: p.get("matching_boards"),
             lambda p: p["port"].get("properties", {}).get("vid")):
    for p in ports:
        if serial(p) and want(p):
            print(p["port"]["address"])
            sys.exit(0)

sys.exit("port.py: no Arduino found. Plug it in, or set PORT=<port> "
         "(e.g. /dev/ttyACM0, /dev/cu.usbmodem101, COM3).")
