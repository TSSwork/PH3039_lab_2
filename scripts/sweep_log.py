# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "pyserial>=3.5",
#     "pyvisa>=1.14",
#     "pyvisa-py>=0.7",
#     "pyusb>=1.2",
# ]
# ///
"""Automated 10 Hz -> 1 kHz frequency sweep.

- Reader thread drains the XIAO serial stream continuously (never blocks USB).
- Main thread conducts the SCPI sweep on the Keysight EDU33212A.
- One merged CSV: freq_hz,time_ms,adc_v,gen_ref_v  (+ '#' metadata lines).
"""

import argparse
import math
import re
import sys
import threading
import time
from collections import deque
from pathlib import Path

import pyvisa
import serial

ROW_RE = re.compile(r"^(\d+),(\d+(?:\.\d+)?),(-?\d+)$")


class Reader(threading.Thread):
    """Drains serial lines into a deque with PC-monotonic arrival stamps."""

    def __init__(self, port, baud):
        super().__init__(daemon=True)
        self.ser = serial.Serial(port, baud, timeout=0.2)
        self.rows = deque(maxlen=1_000_000)
        self.lock = threading.Lock()
        self.bad = 0
        self.stop = False

    def run(self):
        while not self.stop:
            try:
                line = self.ser.readline().decode("utf-8", "replace").strip()
            except (OSError, ValueError, serial.SerialException):
                break
            if not line:
                continue
            m = ROW_RE.match(line)
            if m:
                with self.lock:
                    self.rows.append((
                        time.monotonic(),
                        int(m.group(1)),
                        float(m.group(2)),
                        int(m.group(3)),
                    ))
            elif not line.startswith("#"):
                self.bad += 1

    def take_window(self, t0, t1):
        """Return rows arriving in [t0, t1] and discard anything older."""
        out = []
        with self.lock:
            while self.rows and self.rows[0][0] < t0:
                self.rows.popleft()
            for r in self.rows:
                if r[0] <= t1:
                    out.append(r)
        return out

    def close(self):
        self.stop = True
        self.ser.close()


def open_generator():
    rm = pyvisa.ResourceManager("@py")
    names = []
    try:
        names = list(rm.list_resources("USB?*::INSTR"))
    except Exception:
        pass
    for name in names:
        try:
            inst = rm.open_resource(name)
            inst.timeout = 3000
            idn = inst.query("*IDN?").strip()
        except Exception:
            continue
        up = idn.upper()
        if "33212" in idn or "KEYSIGHT" in up or "AGILENT" in up:
            return rm, inst, idn
        inst.close()
    sys.exit(
        "No Keysight generator found via pyvisa-py.\n"
        f"  resources seen: {names}\n"
        "  check: USB cable, `lsusb`, udev rule, `sudo dnf install libusb1`"
    )


def main():
    ap = argparse.ArgumentParser(description="SCPI frequency sweep + XIAO ADC logging")
    ap.add_argument("--port", required=True, help="XIAO serial port, e.g. /dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--fs", type=int, default=20000, help="must match FS in sampling.cpp")
    ap.add_argument("--fstart", type=float, default=10)
    ap.add_argument("--fstop", type=float, default=1000)
    ap.add_argument("--fstep", type=float, default=10)
    ap.add_argument("--periods", type=float, default=30)
    ap.add_argument("--margin", type=float, default=0.2)
    ap.add_argument("--settle", type=float, default=0.1)
    ap.add_argument("--vpp", type=float, default=3.0)
    ap.add_argument("--voff", type=float, default=1.5)
    ap.add_argument("--outdir", default="data")
    ap.add_argument("--yes", action="store_true", help="skip the safety prompt")
    args = ap.parse_args()

    rm, g, idn = open_generator()
    print(f"Generator: {idn}")
    print(f"Output will be SINE {args.vpp} Vpp, {args.voff} V offset "
          f"(swing 0.0..{args.vpp} V) into the XIAO ADC.")
    if not args.yes:
        ans = input("Enable output and start the sweep? [yes/N] ").strip().lower()
        if ans not in ("y", "yes"):
            g.write("OUTP OFF")
            sys.exit("Aborted.")

    reader = Reader(args.port, args.baud)
    reader.start()
    time.sleep(1.0)  # let the stream stabilise

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    csv_path = outdir / f"sweep_{stamp}.csv"

    n_steps = int((args.fstop - args.fstart) // args.fstep) + 1
    try:
        g.write("FUNC SIN")
        g.write(f"VOLT {args.vpp}")
        g.write(f"VOFF {args.voff}")
        g.write("PHAS 0")
        g.write("OUTP ON")

        with open(csv_path, "w") as f:
            f.write(f"# generator={idn}\n")
            f.write(f"# sweep sine {args.fstart}..{args.fstop} Hz step {args.fstep}, "
                    f"vpp={args.vpp}, voff={args.voff}, phase_anchor=window_start\n")
            f.write(f"# fs={args.fs}, periods={args.periods}, margin={args.margin}, "
                    f"settle={args.settle}\n")
            f.write(f"# started={stamp}, adc_v=raw*3.3/4095\n")
            f.write("freq_hz,time_ms,adc_v,gen_ref_v\n")

            step = 0
            fr = args.fstart
            while fr <= args.fstop + 1e-9:
                step += 1
                g.write(f"FREQ {fr}")
                got = float(g.query("FREQ?"))          # confirm it landed
                time.sleep(args.settle)

                t_start = time.monotonic()
                duration = args.periods * (1.0 + args.margin) / got
                time.sleep(duration)
                t_end = time.monotonic()

                rows = reader.take_window(t_start, t_end)
                dropped = sum(1 for a, b in zip(rows, rows[1:]) if b[1] - a[1] != 1)

                if not rows:
                    print(f"[{step:3d}/{n_steps}] f={got:7.1f} Hz  NO ROWS - check firmware/port")
                else:
                    t_ms0 = rows[0][2]
                    for (_pc, _idx, t_ms, raw) in rows:
                        t_sig = (t_ms - t_ms0) / 1000.0
                        ref = args.voff + (args.vpp / 2.0) * math.sin(2.0 * math.pi * got * t_sig)
                        adc_v = raw * 3.6 / 4095.0
                        f.write(f"{got:.1f},{t_ms:.2f},{adc_v:.4f},{ref:.4f}\n")
                    f.flush()
                    expect = duration * args.fs
                    flag = "" if len(rows) > 0.5 * expect else "  <-- too few rows (FS mismatch?)"
                    print(f"[{step:3d}/{n_steps}] f={got:7.1f} Hz  rows={len(rows):6d} "
                          f"(expect ~{expect:.0f})  dropped={dropped}{flag}")

                fr += args.fstep

        print(f"\nDone. CSV: {csv_path}")
    except KeyboardInterrupt:
        print(f"\nInterrupted by user. Partial CSV kept: {csv_path}")
    finally:
        g.write("OUTP OFF")       # safety: kill the output no matter what
        reader.close()
        g.close()


if __name__ == "__main__":
    main()
