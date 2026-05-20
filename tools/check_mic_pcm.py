#!/usr/bin/env python3
"""Quick I2S PCM sanity check — reads 480 int16 samples from UART and plots them."""
import sys
import struct
import numpy as np
import matplotlib.pyplot as plt
import serial

PORT  = sys.argv[1] if len(sys.argv) > 1 else "/dev/tty.usbmodem1103"
BAUD  = 115200
N     = 480  # one 30 ms frame at 16 kHz

with serial.Serial(PORT, BAUD, timeout=2) as s:
    raw = s.read(N * 2)  # int16 → 2 bytes/sample

if len(raw) < N * 2:
    print(f"Only got {len(raw)} bytes — check board UART output")
    sys.exit(1)

pcm = np.frombuffer(raw, dtype=np.int16)
plt.figure(figsize=(10, 3))
plt.plot(pcm)
plt.title("I2S raw PCM — 30 ms frame")
plt.xlabel("Sample")
plt.ylabel("Amplitude")
plt.tight_layout()
plt.show()
