# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "pyserial>=3.5",
# ]
# ///

import argparse
import array
import math
import struct
import time
import wave
from pathlib import Path

import serial

MAGIC = b"PCMW"
PKT_MAGIC = b"\xA5\x5A"


class Reader:
    def __init__(self, ser):
        self.ser = ser
        self.buf = bytearray()

    def pump(self, timeout=0.2):
        self.ser.timeout = timeout
        data = self.ser.read(4096)
        if data:
            self.buf.extend(data)
        return len(data)

    def read_exact(self, n, timeout=8.0):
        deadline = time.time() + timeout

        while len(self.buf) < n:
            remaining = deadline - time.time()
            if remaining <= 0:
                raise TimeoutError(
                    f"Timed out waiting for {n} bytes (have {len(self.buf)})"
                )

            self.pump(min(0.2, max(0.01, remaining)))

        out = bytes(self.buf[:n])
        del self.buf[:n]
        return out

    def find_magic(self, magic, timeout=8.0):
        deadline = time.time() + timeout
        keep = len(magic) - 1

        while True:
            idx = self.buf.find(magic)
            if idx >= 0:
                del self.buf[:idx + len(magic)]
                return

            if len(self.buf) > keep:
                del self.buf[:len(self.buf) - keep]

            if time.time() > deadline:
                raise TimeoutError(f"Timed out waiting for magic {magic!r}")

            self.pump(0.2)


def capture_one(ser, reader):
    # Clear old data before requesting a fresh capture.
    ser.reset_input_buffer()
    reader.buf.clear()

    last_error = None

    # Retry a few times in case the board resets when the port opens.
    for attempt in range(6):
        ser.write(b"R")
        ser.flush()

        try:
            reader.find_magic(MAGIC, timeout=3.0)
            break
        except TimeoutError as e:
            last_error = e
            time.sleep(0.35)
    else:
        raise RuntimeError(f"No capture header from board: {last_error}")

    header = reader.read_exact(8, timeout=5.0)
    sample_rate, total_samples = struct.unpack("<II", header)

    needed_bytes = total_samples * 2
    pcm = bytearray()

    while len(pcm) < needed_bytes:
        reader.find_magic(PKT_MAGIC, timeout=6.0)

        h = reader.read_exact(4, timeout=6.0)
        count = h[0] | (h[1] << 8)
        inv = h[2] | (h[3] << 8)

        # False sync protection.
        if count == 0 or count > 1024 or (count ^ 0xFFFF) != inv:
            continue

        chunk = reader.read_exact(count * 2, timeout=6.0)
        pcm.extend(chunk)

    return sample_rate, bytes(pcm[:needed_bytes])


def stats(pcm):
    samples = array.array("h")
    samples.frombytes(pcm)

    if len(samples) == 0:
        return 0, 0.0

    total = 0
    peak = 0

    for s in samples:
        a = abs(s)
        if a > peak:
            peak = a
        total += s * s

    rms = math.sqrt(total / len(samples))
    return peak, rms


def main():
    ap = argparse.ArgumentParser(
        description="Capture 16-bit PCM WAV clips from DAQ firmware"
    )
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--label", default="unknown")
    ap.add_argument("--count", type=int, default=1)
    ap.add_argument("--outdir", default="edge_data")
    args = ap.parse_args()

    outdir = Path(args.outdir) / args.label
    outdir.mkdir(parents=True, exist_ok=True)

    print(f"Opening {args.port} ...")

    ser = serial.Serial(args.port, args.baud, timeout=0.2)

    # Give the board a moment, especially if opening the port causes a reset.
    time.sleep(2.0)

    reader = Reader(ser)

    saved = 0

    try:
        while saved < args.count:
            print(f"\n[{saved + 1}/{args.count}] label='{args.label}'")
            input("Press Enter, then say the word immediately (2-second clip)...")

            # Small human reaction-time buffer.
            time.sleep(0.25)

            try:
                rate, pcm = capture_one(ser, reader)
            except Exception as e:
                print(f"Capture failed: {e}")
                print("Try again, or close any other serial monitor.")
                continue

            stamp = time.strftime("%Y%m%d_%H%M%S")
            path = outdir / f"{args.label}_{saved + 1:03d}_{stamp}.wav"

            with wave.open(str(path), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(rate)
                w.writeframes(pcm)

            peak, rms = stats(pcm)

            print(f"Saved: {path}")
            print(f"  rate={rate} Hz, samples={len(pcm)//2}, peak={peak}, rms={rms:.1f}")

            if peak < 100:
                print("  Warning: signal is very quiet. Speak closer or increase PDM gain.")
            elif peak > 32000:
                print("  Warning: signal is clipping. Lower PDM gain.")

            saved += 1

    finally:
        ser.close()


if __name__ == "__main__":
    main()
