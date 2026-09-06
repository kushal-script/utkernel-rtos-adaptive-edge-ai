# Requantise undefined behaviour removed, and what it did to the numbers

The requantiser left shifted a negative accumulator, which is undefined in C11
and which UndefinedBehaviorSanitizer reports on every inference. The shift now
goes through an unsigned intermediate. Values are unchanged, proven three ways:
every shift in this model is negative so the left operand is always zero, a
sweep of 36.1 million triples finds no mismatch, and the logits for all 150
evaluation samples under all three masks are byte identical.

The generated ARM code did change, in register allocation rather than in the
packed inner loop, and it turned out to be faster. The four pointwise layers,
which run through `kws_conv_int8`, each dropped about 136,000 cycles, and four
times that is 2.18 ms, which is the whole of the measured improvement. The
depthwise layers moved by a few thousand cycles, matching a kernel that gained
one move and one alignment pad.

| Configuration | Previous run | This run |
| :-- | --: | --: |
| Static FP32 | 125.9 ms | 126.0 ms |
| Static INT8 | 101.5 ms | 99.3 ms |
| Adaptive | 98.2 ms | 95.9 ms |
| Idle, adaptive against FP32 | 1.79 | 1.84 |

Every claim the project makes survives unchanged: the adaptive point is still
faster than both static builds, static FP32 still misses the 120 ms deadline
while the other two meet it, the converged mask is still 0x0AA reached from both
extremes, core accuracy is still 94.0 percent under every configuration, and the
depthwise inversion the controller exploits is intact and slightly stronger at
43 percent against 42.

This run supersedes 2026-08-30_003716_hardware-mask-divergence-fixed as the
source of every published figure.

Reproduce with the procedure in [docs/operation_manual.md](../../docs/operation_manual.md):
build, flash, then `python tools/parse_bench.py <port>`.
