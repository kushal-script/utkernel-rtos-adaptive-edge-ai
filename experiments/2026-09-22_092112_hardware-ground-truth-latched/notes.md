# End to end accuracy matches core accuracy once the ground truth travels with the tensor

This is the third of three runs that together close the gap between core and
end to end accuracy. The first published the newest rows from a private
history instead of erasing them in place, and raised the active frame floor to
32. The second double buffered the published tensor. This one carries the
corpus position inside the published buffer, so the one pointer read the
inference core makes yields the tensor and its ground truth together.

## Why the second run left FP32 short

After the first fix INT8 and the adaptive configuration scored 98.7 percent
end to end but static FP32 sat at 83.8. Double buffering the tensor did not
move it, 84.5 in the second run, so the tensor was not being corrupted. What
FP32 alone does is outlast the stride: 126 ms of inference against a 120 ms
publication period, so a new tensor is published during every FP32 inference.
The classification was scored against `t3_stats.grid_corpus_end`, a single
global T3 overwrote at every publication and T4 read only after the inference
returned. Under FP32 that global had already moved on, so every FP32
classification was scored against the audio 120 ms later than the audio it
saw, and near a clip boundary that is a different label. The other two
configurations finish inside the stride and never saw the problem.

## What this run establishes

Hardware, live 30 second windows, canonical run against this one:

| Configuration | Canonical | This run |
| :-- | --: | --: |
| Static FP32 | 47 of 83, 56.6 percent | 83 of 84, 98.8 percent |
| Static INT8 | 38 of 79, 48.1 percent | 79 of 79, 100 percent |
| Adaptive | 37 of 79, 46.8 percent | 78 of 79, 98.7 percent |

Desktop, identical 60 second command through the three fixes: 34, 70, 76 and
finally 98 percent.

End to end accuracy is now higher than the 94.0 percent core figure. That is
not a contradiction: the core figure is scored on 150 grids drawn across all
twelve classes, while the replay corpus holds three keywords and silence, an
easier task. The claim this supports is that the pipeline loses nothing
between capture and classification, not that it beats the model.

Everything else reproduces the canonical run. Isolated latencies 99.3, 125.9
and 95.9 ms against 99.3, 126.0 and 95.9. Idle residency 23.5, 41.0 and 43.3
percent, identical. Mask 0x0AA, six demotions, four promotions in the probe,
a further four climbing back after the pinned INT8 window, which is what the
`promote=8` total counts. Capture overruns zero. The canonical run therefore
remains the source for costs, latencies and idle residency, and this run is
the source for end to end accuracy.

One thing this and the previous run made visible: the learned noise floor
varies from boot to boot, 28 here and in the canonical run, 94 in the previous
one, because the 64 block seeding window lands on different audio depending on
start up timing. It changes how much of a window the gate closes and so the
idle figure of a single run. The published idle figures are from the canonical
run and this one, which agree.
