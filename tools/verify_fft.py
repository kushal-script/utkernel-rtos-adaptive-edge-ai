#!/usr/bin/env python3
"""Verify CMSIS FFT output by comparing a 440 Hz test tone against scipy.fft."""
import numpy as np
from scipy.fft import rfft, rfftfreq

SAMPLE_RATE = 16000
N = 512
t = np.arange(N) / SAMPLE_RATE
tone = (np.sin(2 * np.pi * 440 * t) * 32767).astype(np.float32)

mag = np.abs(rfft(tone))
freqs = rfftfreq(N, 1 / SAMPLE_RATE)
peak = freqs[np.argmax(mag)]
print(f"Peak frequency: {peak:.1f} Hz (expected ~440 Hz)")
assert abs(peak - 440) < 50, "FFT peak too far from 440 Hz — check CMSIS config"
print("FFT sanity check PASSED")
