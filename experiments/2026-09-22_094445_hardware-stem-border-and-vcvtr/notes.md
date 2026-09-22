# Two bit exact kernel changes, and the run every published figure now comes from

This run supersedes `2026-09-06_220415_hardware-requantise-ub-fixed` as the
canonical source for every number in the repository: latencies, the per layer
cost table, idle residency, the converged mask, and end to end accuracy.

## What changed in the kernels

**The stem's border windows now take the folded path.** The stem is a 10 by 4
convolution with 5 by 1 padding over the 49 by 10 grid, and 65 of its 125
output positions overlap the padding. Those ran the original per element
arithmetic with a bounds check and an offset add on every tap, while the 60
interior positions ran contiguous packed dot products against a folded bias.
A border window now uses the folded path as well: padding reads as zero in
real space, so the taps outside contribute nothing, and the folded term's
pre added offset for those taps is taken back out through a per kernel row
weight sum table. The same integers, regrouped. `tools/verify_device_core.py`
now compares the stem's output both ways over all 150 evaluation grids, byte
for byte, and they agree on every one. Stem INT8 cost falls from 5.52 to 3.92
million cycles, 29 percent.

**Rounding at precision boundaries uses the FPU.** `kws_quantise` rounded
every element through newlib's `lrintf`, a software routine of about thirty
cycles, and a mixed mask crosses four boundaries of eight thousand elements
each per inference. The Cortex-M33 FPU rounds under the same FPSCR mode in one
`VCVTR` instruction, bit identical for every in range value, and every caller
clamps to int8 immediately after. The host build keeps `lrintf`. The one
`lrintf` left in the image is the MFCC front end's, which runs ten times per
frame and is not worth the coupling.

## What moved

| | Canonical | This run |
| :-- | --: | --: |
| Static INT8 | 99.3 ms | 93.4 ms |
| Static FP32 | 126.0 ms | 126.7 ms |
| Adaptive | 95.9 ms | 86.1 ms |
| Adaptive under INT8 by | 3.4 ms | 7.3 ms |
| Idle, FP32 / INT8 / adaptive | 23.5 / 41.0 / 43.3 | 23.6 / 42.5 / 49.8 |
| Adaptive asleep against FP32 | 1.84 times | 2.11 times |
| End to end, FP32 / INT8 / adaptive | 98.8 / 100 / 98.7 | 98.9 / 98.8 / 100 |

The stem change reaches every INT8 path, so static INT8 and the adaptive point
both gain about 5.9 ms. The rounding change reaches only the adaptive path,
which is the only one with boundaries, and accounts for the further 4 ms. The
adaptive margin over the best static build therefore doubles. FP32 is
untouched by either change; its 0.7 ms is code placement in a different image,
the same drift seen in every rebuild.

The mask is still 0x0AA, reached in six demotions and climbed back to in four.
Deadline misses read 188 against 188 overruns, confirming the previous
commit's one flag call per inference; the canonical run read 360 against 180.

## Per layer table

The depthwise layers read 40 percent slower in INT8 in aggregate, 33 to 45 per
layer, against 43 and 37 to 45 in the canonical run. The kernels are untouched
and their INT8 costs agree within half a percent; the movement is dw3's FP32
calibration, 1.69 million cycles here against 1.55 in the canonical run. The
calibration is two live inferences with the capture chain preempting them, so
individual FP32 layer costs carry that jitter. Everything else is 44 percent
faster in INT8 against 38, which is the stem.
