#!/usr/bin/env python3
"""Capture framed raw I2S snapshots from the mic probe build and verify the INMP441.

The probe task in app/audio_probe.c streams SNAP frames over UART. This tool
resyncs on the frame magic, de-interleaves the two I2S slots, reports signal
statistics, plots the waveform, and saves the raw data and figure into a
timestamped folder under experiments/.

Usage: python tools/check_mic_pcm.py [serial_port]
"""
import sys
import struct
import datetime
import pathlib
import numpy as np
import serial

MAGIC = b"SNAP"
PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/tty.usbmodem1103"
BAUD = 115200
FULL_SCALE = float(1 << 23)  # 24 bit signed


def resync(s):
    window = b""
    while True:
        b = s.read(1)
        if not b:
            raise TimeoutError("no data on UART, is the probe build flashed and running?")
        window = (window + b)[-4:]
        if window == MAGIC:
            return


def read_exact(s, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = s.read(n - len(buf))
        if not chunk:
            raise TimeoutError("short read on UART")
        buf += chunk
    return bytes(buf)


def read_frame(s):
    resync(s)
    ver, ch, bps, _rsv, rate, frames = struct.unpack("<BBBBII", read_exact(s, 12))
    payload = read_exact(s, frames * ch * bps)
    (chk,) = struct.unpack("<I", read_exact(s, 4))
    words = np.frombuffer(payload, dtype="<i4")
    if (int(words.astype(np.uint32).sum()) & 0xFFFFFFFF) != chk:
        print("checksum mismatch, discarding frame")
        return None
    return rate, ch, words.reshape(-1, ch)


def main():
    with serial.Serial(PORT, BAUD, timeout=4) as s:
        print(f"listening on {PORT} at {BAUD} baud for SNAP frames ...")
        frame = None
        while frame is None:
            frame = read_frame(s)
    rate, ch, data = frame

    left = data[:, 0].astype(np.float64) / FULL_SCALE
    right = (data[:, 1].astype(np.float64) / FULL_SCALE) if ch > 1 else np.zeros_like(left)

    peak = float(np.max(np.abs(left)))
    rms = float(np.sqrt(np.mean(left ** 2)))
    dc = float(np.mean(left))
    rms_r = float(np.sqrt(np.mean(right ** 2)))
    print(f"frames {len(left)}  rate {rate} Hz  channels {ch}")
    print(f"left  peak {peak:.4f}  rms {rms:.5f}  dc {dc:+.5f}")
    print(f"right rms {rms_r:.5f}  (near 0 confirms the mic drives one I2S slot)")
    print("verdict:", "mic RESPONDING" if rms > 1e-4 else "SILENT, make noise and retry")

    ts = datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S")
    out = pathlib.Path(__file__).resolve().parents[1] / "experiments" / f"{ts}_mic-verify"
    (out / "data").mkdir(parents=True, exist_ok=True)
    (out / "plots").mkdir(parents=True, exist_ok=True)
    np.save(out / "data" / "capture.npy", data)

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        t = np.arange(len(left)) / rate
        fig, ax = plt.subplots(2, 1, figsize=(10, 5), sharex=True)
        ax[0].plot(t, left, lw=0.5)
        ax[0].set_ylabel("left")
        ax[0].set_title("INMP441 capture")
        ax[1].plot(t, right, lw=0.5, color="tab:orange")
        ax[1].set_ylabel("right")
        ax[1].set_xlabel("time (s)")
        fig.tight_layout()
        fig.savefig(out / "plots" / "waveform.png", dpi=120)
        print("saved", out)
    except Exception as exc:
        print("plot skipped:", exc)


if __name__ == "__main__":
    main()
