# The active frame lever was erasing the audio it should classify

## What was wrong

T3 keeps a sliding grid of 49 feature rows, oldest first, so the newest frame
is always the last row. The active frame lever, which shortens the context the
model is given on a quiet signal, zeroed rows from `active` to 48 of that same
grid, in place. Those are the newest rows. Every shortened classification
therefore ran on audio from up to a second earlier with the word just spoken
erased, and because the erase was in place the zeros slid along the history
and corrupted the next inferences as well.

Training masks the trailing frames of a fixed one second clip, whose tail is
the silence after the word, so the model expects the audio in the leading rows.
On a sliding history the leading rows must be the most recent audio. The two
conventions looked identical and meant opposite things.

The controller drove the count to a floor of 16 on silence, and the model's own
context curve gives 46.85 percent at 16 frames. The end to end accuracy this
repository had been reporting was 47 to 57 percent. That was not window
misalignment, which is what the documents attributed it to. It was this.

Two further facts established while fixing it. The lever never reduced any
work: T3 computes every frame regardless and the inference core runs over the
full tensor, so it was trading accuracy for nothing. And the old in place
rotation moved the tensor under a running inference every 20 ms, on every
configuration.

## The fix in this run

The sliding history is private. At inference time the newest `active` rows are
published, leading, into the tensor the core reads, with the rest at the input
zero point. The floor is raised from 16 to 32, where the context curve is still
flat. The count is now reported as `active=` in the live and pipeline lines.

## What this run establishes

Desktop, identical 60 second command before and after: 38 of 113 correct at 34
percent became 74 of 106 at 70 percent. Evaluation set, converged mask and the
six demotion four promotion walk unchanged.

Hardware, live 30 second windows, against the canonical run:

| Configuration | Before | After |
| :-- | --: | --: |
| Static FP32 | 47 of 83, 56.6 percent | 67 of 80, 83.8 percent |
| Static INT8 | 38 of 79, 48.1 percent | 78 of 79, 98.7 percent |
| Adaptive | 37 of 79, 46.8 percent | 77 of 78, 98.7 percent |

Isolated latencies reproduce the canonical run within 0.6 percent, 99.3 and
126.7 and 95.9 ms against 99.3 and 126.0 and 95.9, the 0.7 ms on FP32 being
code placement in a slightly larger image. Idle residency 23.0, 40.9 and 43.7
percent against 23.5, 41.0 and 43.3. Mask 0x0AA, six demotions, four
promotions in the probe. Capture overruns zero.

FP32 is the one configuration still short of the core figure. It is also the
only one whose inference, at 126.7 ms, outlasts the 120 ms stride, so the next
publication lands while it is still reading the tensor. The following run
double buffers the publication to close that.
