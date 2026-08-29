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

TFLite Micro builds a static graph. A layer's datatype cannot be flipped mid inference inside its interpreter, so this project does not use the TFLM interpreter for the inference core.

Decided path, a hand written DS-CNN core where per layer precision is genuinely selectable at runtime. The layer kernels and the transform are in the tree rather than pulled from a library, so the build is self contained and every cycle the benchmark reports belongs to code in this repository. T5 reads the per layer DWT cycle count, and when a layer overruns its budget it drops the next layer to INT8 before the deadline is missed. This delivers the true per layer switch the proposal describes and gives a cleaner per layer timing story than the interpreter would. Whole model variant selection, swapping two prebuilt models by DWT slack, is kept only as an optional bootstrap for the first baseline in M3.

Correct the mechanism description too. The INT8 advantage on this part comes from weights being a quarter of the size and so a quarter of the memory traffic, from packed multiply accumulate through the DSP extension, and from not moving values through the floating point register file. It does not come from an ALU that does an INT8 multiply accumulate in one cycle against four for FP32, and that figure should not be repeated. The honest claim is the ratio measured on this silicon for the same model and the same input.

### Memory pool streaming needs a model large enough to justify it

A typical KWS DS-CNN is tens of kilobytes of weights, which the 272 KB SRAM holds whole, so streaming saves nothing for a small model. To make the saving real and measurable, either use a model large enough that whole residency is wasteful, or set a self imposed SRAM budget, then report peak SRAM with and without streaming. Frame it as the technique that lets a model larger than SRAM run at all, and back it with numbers.

### TrustZone isolation is a separable stretch goal

Secure world weight isolation is real on the M33 but costs a secure and non secure project split, SAU and NSC veneers, and a dual binary build. It is a model IP protection story, largely orthogonal to the RTOS and ML co-optimisation thesis the contest rewards. Keep it as the last milestone and be ready to cut it without weakening the core.

## Headline contribution, self tuning controller

T5 is a self tuning controller, not a fixed threshold if else. It adds online hysteresis and a lightweight policy, for example an adaptive threshold rule or a small contextual bandit, that adjusts its variance and budget thresholds to the observed acoustic environment at runtime. The framing, the RTOS learns its own adaptation policy on device, is the fresh research angle that separates this work from any fixed adaptive scheme, and it is only a few hundred lines on top of the fixed threshold loop. The self tuning policy must be shown to converge and to beat the best hand tuned fixed thresholds, otherwise it is decoration.

## Where the claim currently stands

Every mechanism above is implemented and builds. The inference core, the transform, and the quantised graph are validated against a golden reference on the host, so the arithmetic is settled. The trained model reaches 92.8 percent on the twelve class task, and the core holds 94 percent on the on device evaluation set identically under full INT8, full FP32, and alternating per layer precision, which is the evidence that a precision switch preserves meaning rather than merely running.

The headline figure exists with hardware numbers, and it says something sharper than the original framing. On the NUCLEO-H533RE at 250 MHz static FP32 takes 125.9 ms per classification, static INT8 takes 101.5 ms, and the adaptive pipeline reaches 98.2 ms. The adaptive point is not a compromise between the two, it is faster than either, because the cost optimum on this part is a mixed precision mask that no single precision build can express.

The reason is measurable and was measured: the four depthwise layers together are 57 percent slower in INT8 than in FP32, between 41 and 82 percent depending on the layer, while every other layer is 32 percent faster in INT8, because the depthwise kernels are still scalar and the rest use packed multiply accumulate. The controller calibrates on its first two inferences, learns that asymmetry from the cycle counter, and ranks layers by it. From full FP32 it reaches the optimum in six cost reducing demotions; restarted from all INT8 it climbs back to the same mask in four promotions. Converging to the same point from both extremes is what makes the operating point a property of the silicon rather than of where the search began, and it is the strongest form the claim can take.

Two honest qualifications. The 94.0 percent quoted for every configuration is core accuracy on pre computed feature grids, so it exercises the inference core and nothing upstream; end to end accuracy is scored separately and is on too small a sample to state. And the power axis now has a mechanism and an idle residency of 82.3 percent of wall time, but not a current measurement, so it is not stated in watts. See [power.md](power.md).
