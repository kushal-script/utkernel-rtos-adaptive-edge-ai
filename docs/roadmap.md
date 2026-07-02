# Roadmap

Milestones are capability based, not date based. Each one produces a working system that adds a layer of intelligence over the previous. Every milestone ends with a commit, and any run that produces numbers writes its results into `experiments/`.

## M0, boot and kernel, done

Boot chain, 250 MHz clock at VOS0, DWT cycle counter, µT-Kernel 3.0 with the STM32H533 BSP, a heartbeat task, and T1 starting an I2S DMA capture that sets event flags on half and full transfer.

## M1, verified capture, in progress

T1 now uses a true circular linked-list DMA on GPDMA1 with no re-arm gap, and a flag gated probe task streams framed raw capture over UART for `tools/check_mic_pcm.py`. Both build clean. What remains is the on hardware confirmation, that the INMP441 signal is real, correctly scaled, and correctly framed. Nothing downstream is trustworthy until the audio is verified. See [mic_verification.md](mic_verification.md).

Exit: a plotted waveform on the host that responds to sound, with correct amplitude, the driven slot carrying signal and the other near zero, and a small dc offset.

## M2, feature extraction

T3 computes MFCC features on a fixed window with CMSIS-DSP for the FFT. Validate the on device features against a NumPy reference on the same input, within a small tolerance.

Exit: on device MFCC matches the host reference for a known test tone.

## M3, baseline inference

T4 runs the hand written DS-CNN core at fixed INT8 precision on a fixed window, using CMSIS-NN kernels. Measure per inference and per layer latency with the DWT and classification accuracy on a held out set. This is the static baseline that every adaptive result is compared against, and the same core that M5 makes precision switchable.

Exit: a stated baseline number for latency and accuracy, logged in `experiments/`.

## M4, first adaptation

T2 variance monitor and the VAD gate. On flat or low energy frames the pipeline skips T3 and T4 entirely rather than running the model on silence. Adaptive window sizing through a mailbox from T5 to T1.

Exit: measured reduction in average work on quiet input with no loss of keyword recall.

## M5, closed loop, centerpiece

T5 reads the per layer DWT budget and signal state every cycle and drives adaptation together: window size, feature gating, and per layer numeric precision in the hand written core, plus priority changes under load. The controller is self tuning, it adjusts its variance and budget thresholds to the observed environment at runtime rather than using fixed constants. This is the contribution the contest rewards and the research angle for a later paper, the RTOS as a controller that learns its own policy.

Exit: the adaptive pipeline holds a hard per inference deadline while cutting average power at a stated accuracy cost versus the M3 baseline, and the self tuning thresholds are shown to converge and to beat the best fixed thresholds.

## M6, benchmark harness

Automated static versus adaptive comparison across three axes, latency worst case bound from the DWT, average power from the SMPS, and accuracy. Plots generated into `experiments/`. This produces the headline result for the writeup.

Exit: one reproducible figure that shows the tradeoff the project claims.

## M7, stretch

TrustZone secure world weight isolation and larger model memory pool streaming. These strengthen the story but are separable from the core thesis and can be cut without weakening it. See [novelty.md](novelty.md).

## Recommended structural change

The firmware directory is named `KWS_TRON`. Renaming it to `firmware/` reads better for outside contributors, but it touches CMake paths, the flash alias, and editor launch configs, so it is deferred until a quiet point between milestones rather than mid feature.
