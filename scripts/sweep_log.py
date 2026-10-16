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

- Main thread conducts the SCPI sweep on the Keysight EDU33212A.
- XIAO captures N samples into RAM at constant FS, then dumps them.
- One merged CSV: freq_hz,time_ms,adc_v,gen_ref_v  (+ '#' metadata lines).
"""

import argparse
import math
import re
import sys
import time
from pathlib import Path

import pyvisa
import serial

# Matches "idx,raw" where raw can be negative (e.g., -12)
ROW_RE = re.compile(r"^(\d+),(-?\d+)$")


def capture(ser, n, fs):
    """Ask the XIAO for n samples; return (rows, got_end). rows = [(idx, raw)]."""
    ser.reset_input_buffer()
    ser.write(f"C {n}\n".encode())
    rows = []
    # Timeout = capture time + transfer time + 5s slack
    deadline = time.monotonic() + (n / fs) + (n * 12 / 150_000) + 5.0
    while time.monotonic() < deadline:
        line = ser.readline().decode("utf-8", "replace").strip()
        if not line:
            continue
        if line.startswith("# END"):
            return rows, True
        m = ROW_RE.match(line)
        if m:
            rows.append((int(m.group(1)), int(m.group(2))))
    return rows, False


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
    ap.add_argument("--board", default="", help="board name, added to the CSV filename")
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
    ap.add_argument("--maxn", type=int, default=50000, help="must be <= MAX_N in sampling.cpp")
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

    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    time.sleep(1.0)
    ser.reset_input_buffer()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    tag = f"{args.board}_" if args.board else ""
    csv_path = outdir / f"sweep_{tag}{stamp}.csv"

    n_steps = int((args.fstop - args.fstart) // args.fstep) + 1
    try:
        g.write("FUNC SIN")
        g.write(f"VOLT {args.vpp}")
        g.write(f"VOFF {args.voff}")
        g.write("PHAS 0")
        g.write("OUTP ON")

        with open(csv_path, "w") as f:
            f.write(f"# board={args.board or 'unknown'}\n")
            f.write(f"# generator={idn}\n")
            f.write(f"# sweep sine {args.fstart}..{args.fstop} Hz step {args.fstep}, "
                    f"vpp={args.vpp}, voff={args.voff}, phase_anchor=window_start\n")
            f.write(f"# fs={args.fs}, periods={args.periods}, margin={args.margin}, "
                    f"settle={args.settle}\n")
            f.write(f"# started={stamp}, adc_v=raw*3.6/4095\n")
            f.write("freq_hz,time_ms,adc_v,gen_ref_v\n")

            step = 0
            fr = args.fstart
            while fr <= args.fstop + 1e-9:
                step += 1
                g.write(f"FREQ {fr}")
                got = float(g.query("FREQ?"))          # confirm it landed
                time.sleep(args.settle)

                # Calculate how many samples we want for 30 periods + 20% margin
                n_want = int(args.periods * args.fs / got)
                n = min(n_want, args.maxn)

                rows, ok = capture(ser, n, args.fs)

                if not rows:
                    print(f"[{step:3d}/{n_steps}] f={got:7.1f} Hz  NO ROWS - check firmware/port")
                else:
                    for (idx, raw) in rows:
                        t_ms = idx * 1000.0 / args.fs
                        # t_ms is in ms, so divide by 1000 for the sine wave calculation
                        ref = args.voff + (args.vpp / 2.0) * math.sin(2.0 * math.pi * got * (t_ms / 1000.0))
                        adc_v = raw * 3.6 / 4095.0
                        f.write(f"{got:.1f},{t_ms:.2f},{adc_v:.4f},{ref:.4f}\n")
                    f.flush()

                    flag = ""
                    if not ok:            flag += "  <-- no END (transfer cut off)"
                    if len(rows) != n:    flag += "  <-- rows != requested"
                    if n < n_want:        flag += f"  (capped: {n * got / args.fs:.1f} periods)"

                    print(f"[{step:3d}/{n_steps}] f={got:7.1f} Hz  rows={len(rows):6d} "
                          f"(asked {n}){flag}")

                fr += args.fstep

        print(f"\nDone. CSV: {csv_path}")
    except KeyboardInterrupt:
        print(f"\nInterrupted by user. Partial CSV kept: {csv_path}")
    finally:
        g.write("OUTP OFF")       # safety: kill the output no matter what
        ser.close()
        g.close()


if __name__ == "__main__":
    main()
