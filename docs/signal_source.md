# Signal source

The pipeline is fed through one interface, `app/signal_source.h`, so no stage
above it knows where samples come from. Two implementations exist and one is
selected by `KWS_SIGNAL_SOURCE` in `app/app_config.h`.

## Why replay is the default

The contribution this project makes is in the coupling between the kernel and
the model, not in the transducer. A live microphone is a convenience for
demonstration and a liability for measurement: accuracy cannot be scored
against a live signal because there is no ground truth, so any serious
evaluation has to replay labelled audio anyway.

The replay source therefore streams a labelled corpus held in flash. It is not
a simulation of the capture path, it is the capture path: the same GPDMA
channel behaviour, the same interrupt per filled block, the same event flag
handed to the same task. Only the origin of the bytes differs. Everything the
contest cares about, the real time behaviour of the task graph, is exercised
exactly as it would be with a sensor attached.

Because the corpus carries labels, the device can also score itself, which is
what makes the on device accuracy number in the benchmark meaningful.

## The corpus, and reproducing it exactly

Six clips alternating keyword and silence: down, silence, yes, silence, stop,
silence. The alternation is the point. A run of keywords holds the voice
activity gate permanently open, so the quiescent branch of the controller, which
is where the power saving comes from, is never entered and cannot be observed.

The corpus is built once and cached, then compiled into flash by the exporter:

```
python -m kws.make_replay          # model/datasets/cache/replay_clips.npz
python -m kws.export --checkpoint <run>/checkpoint.pt
```

Both are deterministic. `make_replay` seeds its selection, so rebuilding from
the dataset reproduces the cached clips bit for bit, and the exporter then
reproduces `replay_data.c` byte for byte. Run them with their defaults and the
committed sources come back unchanged; that is the check that the audio behind
every recorded experiment is still the audio in the repository.

Take the corpus whole. The exporter uses every clip in the cache unless
`--replay-clips` is passed, and warns when that flag truncates, because half a
stratified corpus is not half an experiment, it is a different one: dropping
clips changes which words are replayed and how often the gate is crossed, and a
run against it is no longer comparable with anything already recorded.

## How the replay source works

TIM6 is programmed directly, without the timer HAL, to raise an update event at
16 kHz. GPDMA1 channel 1 takes that update as a hardware request and moves one
sample per event out of the flash corpus into the capture buffer.

The direction is peripheral to memory with an incrementing source rather than
memory to memory. That is not a cosmetic choice: on this part the memory to
memory setting is the software request bit, so a memory to memory channel
ignores the timer and runs at bus speed. Peripheral to memory with an
incrementing source address is what makes the transfer happen at the sample
rate.

Two linked list nodes in circular mode ping pong between the two halves of the
capture buffer. When a node completes, its source address register is advanced
to the next chunk of the corpus. That node cannot run again until the other one
has finished, so the update is safe, and it is a single store rather than a
list rebuild. Channel 0 is left alone because it belongs to the microphone
path.

```
TIM6 update at 16 kHz
        │  hardware request
        ▼
GPDMA1 channel 1 ── node A ──► capture[0 .. w)      transfer complete
        ▲                                            sets FLG_HALF_READY
        └───────── node B ──► capture[w .. 2w)      transfer complete
                                                     sets FLG_FULL_READY
```

The window `w` is the adaptive capture block. Changing it stops the timer,
aborts the channel, rebuilds the two nodes, and restarts, which is why the
resize is handled in T1's task context and never in an interrupt.

## Microphone source

Selecting `KWS_SOURCE_I2S` switches to the INMP441 on I2S2 with the DMA setup
in `Core/Src/stm32h5xx_hal_msp.c`. The microphone delivers 24 bit samples in 32
bit slots, so the completed block is narrowed to the 16 bit stream the rest of
the pipeline expects before any consumer sees it. Wiring is in
[hardware.md](hardware.md) and the bring up procedure in
[mic_verification.md](mic_verification.md). This path has not been exercised on
hardware.

## Proven on hardware

Two points in this design deserved a measurement rather than an assumption, and
both were settled by the first hardware sessions and hold in every run since.

The first was whether GPDMA1 can read the flash corpus through the port it is
configured to use. It can: the capture buffer holds the corpus, no SRAM staging
was needed, and the fallback of staging one clip at boot was never exercised.

The second was the pacing itself. It is real: the block rate scales with the
window as the timer dictates and sits orders of magnitude below what a free
running channel would produce, and the capture overrun count reads zero across
the live windows of the final run, see
`experiments/2026-08-30_003716_hardware-mask-divergence-fixed`. The reported
rate can sit above the expectation printed beside it, because the expectation
is computed from the window size at print time while the controller resizes
the window during the measurement.

The checks and their expected numbers remain listed in
[benchmarking.md](benchmarking.md) for anyone bringing the pipeline up on a
fresh board.
