# /// script
# requires-python = ">=3.11"
# dependencies = ["matplotlib>=3.8"]
# ///
"""Live overlay plot: measured ADC vs ideal generator reference.

Redraws data/sweep_plot.png from the newest (or given) sweep CSV every 2 s,
same pattern as Lab 1's plot.sh. Ctrl+C to stop.
"""

import argparse
import glob
import sys
import time

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load_last_segment(path):
    freqs, t, adc, ref = [], [], [], []
    with open(path) as f:
        for line in f:
            if line.startswith("#") or line.startswith("freq_hz"):
                continue
            p = line.split(",")
            if len(p) != 4:
                continue
            freqs.append(float(p[0])); t.append(float(p[1]))
            adc.append(float(p[2]));  ref.append(float(p[3]))
    if not freqs:
        return None
    last = freqs[-1]
    i0 = len(freqs) - 1
    while i0 > 0 and freqs[i0 - 1] == last:
        i0 -= 1
    t0 = t[i0]
    return last, [x - t0 for x in t[i0:]], adc[i0:], ref[i0:]


def draw(path, out):
    seg = load_last_segment(path)
    fig, ax = plt.subplots(figsize=(9, 4.5))
    if seg is None:
        ax.set_title("waiting for data...")
    else:
        f, t, adc, ref = seg
        ax.plot(t, ref, "-", lw=1.0, color="tab:gray", label="gen_ref_v (ideal)")
        ax.plot(t, adc, ".-", ms=2, lw=0.6, color="tab:blue", label="adc_v (measured)")
        ax.set_title(f"current segment: {f:.0f} Hz")
        ax.set_xlabel("time in segment [ms]")
        ax.set_ylabel("V")
        ax.legend(loc="upper right")
        ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(out, dpi=110)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", default=None)
    ap.add_argument("--out", default="data/sweep_plot.png")
    ap.add_argument("--once", action="store_true")
    args = ap.parse_args()

    path = args.file
    if path is None:
        cands = sorted(glob.glob("data/sweep_*.csv"))
        if not cands:
            sys.exit("No data/sweep_*.csv yet. Run 'make sweep' first.")
        path = cands[-1]

    if args.once:
        draw(path, args.out)
        print(f"Plot: {args.out} (from {path})")
        return

    print(f"Plotting {path} -> {args.out} every 2 s (Ctrl+C to stop)")
    try:
        while True:
            draw(path, args.out)
            time.sleep(2)
    except KeyboardInterrupt:
        draw(path, args.out)
        print(f"\nFinal plot: {args.out}")


if __name__ == "__main__":
    main()
