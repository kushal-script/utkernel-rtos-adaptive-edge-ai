# Double buffering the published tensor, a correct change that was not the cause

Second of three runs closing the core to end to end accuracy gap. After the
first fix static FP32 was the one configuration still short, at 83.8 percent
against 98.7 for the other two. The hypothesis tested here was that the next
publication, which under FP32 lands during every inference because 126 ms of
inference outlasts the 120 ms stride, was rewriting the tensor the core was
reading.

The publication now fills whichever of two buffers the core is not reading
and swaps the pointer, so an inference always reads a tensor nothing writes
to. The desktop tool moved from 70 to 76 percent on the same 60 second
command; the eval set, mask and walk are unchanged.

On hardware FP32 read 71 of 84 at 84.5 percent, against 83.8 before. Within
noise. The hypothesis was wrong: the tensor was not what a slow inference was
losing. The change stays because the race it removes is real, and the next
run finds the actual cause, the ground truth label being read from a global
after the inference rather than with the tensor.

Also recorded here because it surfaced in this run: the learned noise floor
came out at 94 with a gate threshold of 752, against 28 and 224 in every other
run, so the adaptive window gated more, ran 145 inferences instead of 171, and
reported 51.9 percent idle. The seeding window takes the minimum over the
first 64 blocks and which audio those cover depends on start up timing. A run
to run variance in the gate, not a change in the pipeline.
