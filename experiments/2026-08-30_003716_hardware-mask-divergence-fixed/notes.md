# Controller mask divergence, found and fixed

## What was wrong

T5 held the precision mask in a local variable initialised once at task start
and never re read from `adapt_state.precision_mask`. T4 reads that shared field
on every inference. Nothing kept the two in step, so any write to the field from
outside the controller made them diverge permanently.

The benchmark writes the field directly to pin a configuration for its live
windows. After the first pinned window the controller was ranking moves against
a configuration that was not executing, and every trace row labelled a measured
cost with a mask that had not produced it.

Two consequences, both in the recorded output of the previous run:

* the live window labelled `adaptive` reported `mask=00000000`. It ran all INT8,
  not the converged mixed mask, because the preceding pinned INT8 window left
  the field at zero and the controller, sitting at its own local optimum, had no
  improving move to make and so never wrote the field again. Its energy and end
  to end accuracy figures were an INT8 measurement carrying an adaptive label.
* the trace rows recording 126 to 130 ms against mask `00aa` were static FP32
  inferences from the pinned FP32 window, attributed to the wrong mask. They
  were read as the adaptive configuration missing its deadline under load. It
  was not.

The isolated latency figures were never affected. `bench_run` drives each mask
explicitly and takes the adaptive one from `t5_stats.converged_mask`, so
125.9, 101.5 and 98.2 ms stand unchanged, and this run reproduces them to the
microsecond.

## The fix

The controller re reads `adapt_state.precision_mask` at the top of every
decision, so what it reasons about is always what executed. The convergence
probe restart is additionally excluded while `pin_precision` is set: a pinned
controller can never produce a move, so without the guard every decision would
look settled and the probe would overwrite the very mask being held.

## What this run establishes

| Configuration | Live mask | Core idle | Against FP32 |
| :-- | :-- | --: | --: |
| Static FP32 | 000003ff | 23.5 percent | 1.00 |
| Static INT8 | 00000000 | 39.9 percent | 1.69 |
| Adaptive | 000000aa | 42.0 percent | 1.79 |

The adaptive row is now a different configuration from the INT8 row, which is
what makes the comparison mean anything. It sleeps longer than either pure
build, which is the expected consequence of doing less work per classification.

Deadline behaviour under live pipeline load, from the trace, attributed to the
mask that actually ran:

| Mask | Traced decisions | Over deadline | Latency |
| :-- | --: | --: | :-- |
| 000000aa adaptive | 10 | 0 | 100.5 to 101.0 ms |
| 000003ff FP32 | 45 | 45 | 126.3 to 129.8 ms |

So the converged mask holds the 120 ms deadline with the whole pipeline running,
not only in the isolated benchmark, and static FP32 misses it on every inference.
Capture overruns stayed at zero throughout.
