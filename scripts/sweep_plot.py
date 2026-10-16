# /// script
# requires-python = ">=3.11"
# dependencies = ["matplotlib>=3.8"]
# ///
"""Sweep plotter, three modes:
  live (default): redraw data/sweep_plot.png every 2 s with ONLY the newest
                  frequency segment - watch it while the sweep runs.
  --grid        : ONE png containing one subplot per frequency (all of them).
  --split       : ONE png per frequency, written to data/plots/.
"""
import argparse
import glob
import math
import os
import sys
import time

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load_rows(path):
    rows = []
    with open(path) as f:
        for line in f:
            if line.startswith("#") or line.startswith("freq_hz"):
                continue
            p = line.split(",")
            if len(p) != 4:
                continue
            rows.append((float(p[0]), float(p[1]), float(p[2]), float(p[3])))
    return rows


def segments(rows):
    """Group rows by frequency, in order; time relative to each segment start."""
    segs = []
    for f, t, a, r in rows:
        if not segs or segs[-1][0] != f:
            segs.append([f, [], [], []])
        s = segs[-1]
        s[1].append(t); s[2].append(a); s[3].append(r)
    out = []
    for f, ts, ad, rf in segs:
        t0 = ts[0]
        out.append((f, [x - t0 for x in ts], ad, rf))
    return out


def style_ax(ax, f, t, a, r, big):
    ax.plot(t, r, "-", lw=1.0, color="tab:gray", label="ref" if big else None)
    ax.plot(t, a, ".-", ms=2 if big else 1.2, lw=0.6,
            color="tab:blue", label="adc" if big else None)
    ax.set_title(f"{f:.0f} Hz", fontsize=11 if big else 8)
    ax.grid(alpha=0.3)
    if big:
        ax.set_xlabel("ms in segment"); ax.set_ylabel("V")


def draw_live(seg, out):
    fig, ax = plt.subplots(figsize=(9, 4.5))
    if seg is None:
        ax.set_title("waiting for data...")
    else:
        style_ax(ax, *seg, big=True)
        ax.legend(loc="upper right")
    fig.tight_layout(); fig.savefig(out, dpi=110); plt.close(fig)


def draw_grid(segs, out, cols):
    n = len(segs)
    nrows = math.ceil(n / cols)
    fig, axes = plt.subplots(nrows, cols, squeeze=False,
                             figsize=(cols * 2.3, nrows * 1.7))
    for i, row in enumerate(axes):
        for j, ax in enumerate(row):
            k = i * cols + j
            if k < n:
                style_ax(ax, *segs[k], big=False)
                ax.tick_params(labelsize=6)
            else:
                ax.axis("off")
    fig.suptitle("measured (blue) vs ideal (grey) - one panel per frequency")
    fig.tight_layout(rect=(0, 0, 1, 0.98))
    fig.savefig(out, dpi=110); plt.close(fig)


def draw_split(segs, outdir):
    os.makedirs(outdir, exist_ok=True)
    for f, t, a, r in segs:
        fig, ax = plt.subplots(figsize=(6, 3))
        style_ax(ax, f, t, a, r, big=True)
        ax.legend(loc="upper right", fontsize=8)
        fig.tight_layout()
        fig.savefig(f"{outdir}/f{f:07.1f}Hz.png", dpi=110)
        plt.close(fig)
    print(f"Wrote {len(segs)} figures to {outdir}/")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", default=None)
    ap.add_argument("--out", default="data/sweep_plot.png")
    ap.add_argument("--grid", action="store_true")
    ap.add_argument("--cols", type=int, default=10)
    ap.add_argument("--split", action="store_true")
    ap.add_argument("--splitdir", default="data/plots")
    ap.add_argument("--once", action="store_true")
    args = ap.parse_args()

    path = args.file
    if path is None:
        cands = sorted(glob.glob("data/sweep_*.csv"), key=os.path.getmtime)
        if not cands:
            sys.exit("No data/sweep_*.csv yet. Run 'make sweep' first.")
        path = cands[-1]

    if args.grid or args.split:
        segs = segments(load_rows(path))
        if not segs:
            sys.exit("CSV has no data rows yet.")
        if args.grid:
            draw_grid(segs, args.out, args.cols)
            print(f"Grid plot ({len(segs)} panels): {args.out}")
        if args.split:
            draw_split(segs, args.splitdir)
        return

    print(f"Plotting {path} -> {args.out} every 2 s (Ctrl+C to stop)")
    try:
        while True:
            segs = segments(load_rows(path))
            draw_live(segs[-1] if segs else None, args.out)
            if args.once:
                return
            time.sleep(2)
    except KeyboardInterrupt:
        segs = segments(load_rows(path))
        draw_live(segs[-1] if segs else None, args.out)
        print(f"\nFinal plot: {args.out}")


if __name__ == "__main__":
    main()
