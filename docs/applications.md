# Where this applies

The classifier is the vehicle, not the product. What this project builds is a kernel that measures the silicon it is running on and reshapes a workload to fit a deadline. Keyword spotting is the workload that proves it. This note records what transfers to other problems, what does not, and which of those claims are measured rather than asserted.

The measurements behind it are in [experiments/2026-09-06_225144_hardware-nodsp-kernel-counterfactual](../experiments/2026-09-06_225144_hardware-nodsp-kernel-counterfactual/notes.md). That experiment compared two builds of its own date. The kernel changes made since, the stem's border windows taking the folded path and FPU rounding at precision boundaries, touch neither the depthwise layers nor the packed path the experiment removed, so its finding stands; the current per layer figures are in the canonical run named in [benchmarking.md](benchmarking.md).

## The finding that generalises

The result is not that adaptive beats static. It is that **the best configuration was mixed and counter intuitive, and only measurement found it.** The universal prior is that INT8 is faster than FP32. On this part that is false for four of the ten layers, by 40 percent, and a compile time decision, which is what essentially every TinyML deployment makes today, would have been wrong in a direction nobody would think to check.

Three experiments establish what that depends on.

**Removing the DSP extension does not change the answer.** Rebuilt with `+nodsp`, so every kernel is scalar and the object file holds no packed instructions at all, the board still converges on the same mask. The depthwise layers do not move at all, because they never used the packed path: depthwise convolution reads one channel per output with a stride of the channel count, so its operands are never contiguous. A depthwise output costs nine multiply accumulates where a pointwise output costs sixty four or more, so INT8's fixed per output overhead, an offset add and a 64 bit requantisation, is amortised in one case and dominant in the other.

That matters because it means the inversion is **structural to depthwise separable convolution on a microcontroller**, not an artifact of a kernel library nobody has finished optimising. Depthwise separable convolution is the dominant TinyML architecture family: MobileNet, DS-CNN, the EfficientNet Lite series. The condition that makes a mixed precision optimum exist is present in all of them.

**A uniform change of speed does not change the answer.** Doubling every cycle count leaves the converged mask identical. This is provable rather than lucky: the controller ranks moves on the difference between the two precisions and carries no deadline term, so scaling all costs by a constant cannot reorder them. A slower clock, a thermal throttle or a voltage scaled part changes whether the deadline is met, never which configuration is cheapest.

**Changing the relative costs does change the answer, correctly.** Given a hypothetical depthwise kernel 40 percent cheaper in INT8, roughly what a channel blocked layout would buy, the controller converges on plain all INT8 in ten demotions and no promotions. The mixed optimum exists exactly as long as the inversion does, and when it stops existing this controller finds the new answer without being told.

Together: the operating point is a property of the relative per layer cost of the model and its kernels, and of nothing else. That is the case for measuring at runtime rather than deciding at compile time.

## Where it fits

**Where the deadline is real rather than cosmetic.** Motor and power electronics control with inference inside the control period, where an overrun destabilises a loop rather than dropping a frame. Industrial sensor fusion on a fixed cycle. Medical wearables doing arrhythmia detection from ECG or PPG, where a provable worst case is worth more than a good average, which is why this project reports worst case and not mean.

**Where power dominates and the signal is mostly uninteresting.** Always on acoustic or vibration event detection on a battery: predictive maintenance on rotating machinery, structural monitoring, environmental and wildlife acoustics on solar. The shape is identical to this one, a continuous stream that is mostly boring with an occasional event and a hard budget, and the variance gate transfers directly. Only the test for whether a frame is worth processing changes.

**Where the hardware moves under the firmware.** A product re-spun onto a different part during a component shortage, a fleet with several hardware revisions in the field, or a design that scales voltage and frequency at runtime. Firmware that measures itself re-derives its own operating point on each part instead of needing a hand tuning campaign per variant. The experiments above bound this claim honestly: a different clock changes nothing about the optimum, while a different instruction set, kernel library or memory system does, and it is the second case this is for.

**What actually has to change to port it.** The signal source behind the one interface in `app/signal_source.h`, the model, and the gate's test for an interesting frame. The controller, the cost calibration, the deadline logic and the `tk_get_mpl` weight streaming are untouched. That is the reuse claim, and it is small enough to be credible.

## Where it does not apply

It needs a cycle counter, so the smallest Cortex-M0 parts are out. It needs enough slack for adaptation to have somewhere to go: a model that clears its deadline by a factor of ten gains nothing but overhead. It needs a model whose accuracy survives precision switching, which was measured here at 94.0 percent identically across every mask but is a property of this model's quantisation and must be rechecked rather than assumed for another.

And there is a cold start. The controller reaches its operating point after a few hundred decisions, and it gets there by trying configurations, so a deployment that cannot tolerate a learning transient would need the converged mask persisted to flash and reloaded at boot, with a fresh calibration only when the part or the build changes. Nothing in the design prevents that; it simply was not needed to produce the result this project reports.

Finally, the fixed threshold sweep that would show the learned gate thresholds beat the best hand tuned ones was not performed, so the self tuning claim rests on convergence and path independence rather than on a comparison against a tuned baseline. See the note in [adaptation.md](adaptation.md).
