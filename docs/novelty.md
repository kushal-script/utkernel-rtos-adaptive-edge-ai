# Novelty

## The core claim

In published TinyML on RTOS work the kernel is a passive scheduler and the model is a static passenger. This project makes the kernel an active participant. µT-Kernel reads silicon level performance counters and signal statistics and reshapes how the model executes every cycle. This bidirectional coupling between an RTOS and an ML model, using native kernel primitives, is the defensible contribution.

The contest category is RTOS Application. The winning angle is that the RTOS is the controller, not infrastructure. The single deliverable that proves it is one reproducible figure: the adaptive pipeline holding a hard per inference deadline while cutting average power at a stated accuracy cost, against a static baseline. Every mechanism below exists to produce that figure.

## How it differs from standard TinyML

| Technique | Standard TinyML | This project |
| :-- | :-- | :-- |
| Window sizing | Fixed at compile time | Runtime adaptive from variance, T2 |
| FFT preprocessing | Always runs | Skipped on flat frames, VAD gate |
| Quantisation | Static INT8 or FP32 | Selected at runtime from the timing budget |
| Task priority | Fixed | Driven by signal urgency with `tk_chg_pri` |
| Memory layout | Full model resident | Per layer pool streaming, T4 |
| RTOS role | Passive scheduler | Active co-optimiser in a feedback loop |

## Sharpened technical positions

The proposal is strong. Four claims need tightening so the numbers hold up under scrutiny from judges who reason at register level.

### Feature bypass is a VAD gate, not a fake feature vector

A model trained on MFCC features cannot be handed a zero padded vector and be relied on to emit a quiescent state, unless it was trained with such examples. The clean and honest mechanism is a voice activity detector. T2 measures short time energy and variance. Below threshold the pipeline classifies the frame as silence and skips T3 and T4 entirely. The variance monitor is the VAD. This saves the full feature and inference cost on quiet frames, which is a larger and more defensible saving than skipping only the FFT.

### Runtime precision is variant selection, not per layer switching inside TFLite Micro

TFLite Micro builds a static graph. A layer's datatype cannot be flipped mid inference inside its interpreter. Two achievable versions exist.

Safe path, model variant selection. Keep two or three prebuilt models, a full INT8 model and a higher precision or larger model. T5 picks which one runs on the next inference from the measured slack against the DWT deadline. Simple, robust, and directly measurable.

Ambitious path, a hand written DS-CNN core of a few hundred lines using CMSIS-NN INT8 kernels with an FP32 fallback, where per layer precision is genuinely selectable at runtime. This delivers the true per layer switch the proposal describes and gives a cleaner per layer DWT story than fighting the interpreter. Start on the safe path and evolve to this if time allows.

Correct the mechanism description too. The INT8 speedup on Cortex-M33 comes from CMSIS-NN packed SIMD multiply accumulate, SMLAD, and from avoiding the FPU, not from an ALU that does an INT8 MAC in one cycle versus four for FP32 on the same unit. The honest claim is that INT8 kernels are roughly two to four times faster and lower energy than FP32 kernels, measured with the DWT.

### Memory pool streaming needs a model large enough to justify it

A typical KWS DS-CNN is tens of kilobytes of weights, which the 272 KB SRAM holds whole, so streaming saves nothing for a small model. To make the saving real and measurable, either use a model large enough that whole residency is wasteful, or set a self imposed SRAM budget, then report peak SRAM with and without streaming. Frame it as the technique that lets a model larger than SRAM run at all, and back it with numbers.

### TrustZone isolation is a separable stretch goal

Secure world weight isolation is real on the M33 but costs a secure and non secure project split, SAU and NSC veneers, and a dual binary build. It is a model IP protection story, largely orthogonal to the RTOS and ML co-optimisation thesis the contest rewards. Keep it as the last milestone and be ready to cut it without weakening the core.

## Novelty upgrade worth considering

Make T5 a self tuning controller rather than a fixed threshold if else. Add online hysteresis and a lightweight policy, for example an adaptive threshold rule or a small contextual bandit, that adjusts its variance and budget thresholds to the observed acoustic environment at runtime. The framing, the RTOS learns its own adaptation policy on device, is a genuinely fresh research angle, is only a few hundred lines, and separates this work from any fixed adaptive scheme. This is the strongest single addition beyond the current proposal.

## Immediate next engineering step

Fix T1 to a true circular DMA and verify captured audio on the host before building anything on top of it. Features and inference on unverified audio are wasted effort.
