# Introduction slides

The contest requires slides introducing the program as a separate mandatory
item. The built deck is [TRON2026_intro_slides.pptx](TRON2026_intro_slides.pptx),
eleven slides. This file is the content behind it, slide by slide, so the wording
can be reviewed and edited without opening PowerPoint.

## Rebuilding

```
python tools/make_slide_figures.py     # docs/figures from the raw device captures
node tools/build_slides.js             # docs/TRON2026_intro_slides.pptx
```

`make_slide_figures.py` reads the newest raw capture under `experiments/` that
carries a cost table and a decision trace, and writes `layer_inversion.png` and `convergence.png`. The deck
embeds those two, so a slide can never drift from the run it came from: change
the run, rerun both commands, and the figures follow.

`build_slides.js` needs `pptxgenjs`. Install it locally with `npm install
pptxgenjs`, or point `NODE_PATH` at a global install.

Speaker notes are attached to every slide in the built file. Edit content here
first, then regenerate, so this file stays the source of truth rather than a
stale copy of it.

---

**1. Title**

RTOS-Coupled Adaptive Edge AI
Real time co-optimisation of TinyML inference on µT-Kernel 3.0
NUCLEO-H533RE, Cortex-M33 at 250 MHz. RTOS Application, Students.

---

**2. The problem**

Deploying a model on a microcontroller means fixing every optimisation decision
at compile time: window size, whether features run, numeric precision. The RTOS
schedules the model but does not shape it.

Both static choices are wrong somewhere. On this part FP32 misses the deadline
and INT8 gives up accuracy headroom for nothing, and neither can express the
configuration that is actually best.

---

**3. The idea**

Make the kernel an active participant. µT-Kernel reads the DWT cycle counter and
signal statistics, and reshapes how the model executes: capture window, feature
gating, task priority, and per layer numeric precision. Five tasks, every link a
native kernel primitive.

*Figure: the five task graph with the primitive named on every edge.*

---

**4. The headline result**

| Configuration | Latency | 120 ms deadline | Core idle |
| :-- | --: | :-- | --: |
| Static FP32 | 126.0 ms | missed | 23.5 percent |
| Static INT8 | 99.3 ms | met | 41.0 percent |
| **Adaptive** | **95.9 ms** | **met** | **43.3 percent** |

The adaptive point is **faster than either static build**, not a compromise
between them. The deadline is the classification period itself, derived from the
inference stride, not chosen after seeing the costs.

---

**5. Why a mixed configuration wins**

Measured on this silicon, per layer:

* the four depthwise layers are together **43 percent slower in INT8** than in
  FP32, between 37 and 45 percent depending on the layer, because their samples
  are not contiguous and their kernels are still scalar
* every other layer is **38 percent faster in INT8**, running packed SXTB16 and
  SMLAD pairs
* the fully connected layer is a few thousand cycles either way, invisible next
  to the rest

So the cost optimum is mixed: depthwise at FP32, everything else INT8. **No
single precision build can express it.**

*Figure: per layer INT8 against FP32 cycles, the four depthwise bars inverted.*

---

**6. The kernel finds it by measuring, not by being told**

T4 calibrates on its first two inferences, running both pure precisions and
recording what every layer costs. T5 ranks layers by that measured delta and
hill climbs on cost, so every accepted move strictly reduces the projection and
the walk cannot cycle.

* from all FP32: **6 demotions** to mask 0x0AA
* restarted from all INT8: **4 promotions** back to the same mask
* converges to the same point from both extremes

*Figure: the decision trace, latency against inference index, deadline line and
the moves marked.*

---

**7. Honesty, and what the measurements refuted**

The program plan predicted INT8 would be three to four times faster from an ALU
cycle count argument. The first hardware run measured it **28 percent slower**,
because a scalar INT8 path pays a per element offset add and a 64 bit
requantisation while FP32 rides the FPU. Folding the offsets and rewriting with
packed SXTB16 and SMLAD, bit identical against the golden reference, brought it
to 1.22 times faster. **The refutation is what produced the real result.**

Also corrected: this part has no SMPS, so the power measurement point is the IDD
jumper.

---

**8. The three characteristics the contest rewards**

* **Real time.** Deadline derived from the application. Convergence transient
  reported, including the deadline misses during it, rather than only the
  settled average.
* **Power.** The kernel idle hook was an empty function; it now sleeps. Core
  measured asleep 43.3 percent of wall time adaptive against 23.5 percent FP32,
  a factor of 1.84 on identical audio, and above static INT8 at 41.0 percent.
* **Footprint.** 460 KB flash, 117 KB of 272 KB SRAM, per layer weight streaming
  through `tk_get_mpl` with peak pool use of 16 KB measured.

---

**9. How correctness is established**

The device is never the first implementation. A NumPy reference of exactly the
device arithmetic is checked against the trained float model, then the real
device C is compiled for the host and checked against that reference: transform
to 1.4e-05, MFCC frame to 5.5e-06, inference logits to 5e-08. A hardware
disagreement is therefore a hardware question, never an open one.

---

**10. What is not claimed**

* Core accuracy 94.0 percent is measured on **pre computed feature grids**. End
  to end accuracy, measured separately, is 47 to 57 percent and the
  configurations are **not** statistically separable at this sample size.
* Power is an **idle residency ratio**, not a wattage. No ammeter was used.
* TrustZone, promised in the plan's section 6.6, is **cut** as separable from
  the co-optimisation thesis, and the descope is documented.

---

**11. What this generalises to**

The operating point is a property of **relative per layer cost**, and of nothing
else. Three experiments pin that down:

* **Remove the DSP extension entirely** and the board converges on the same
  mask. The depthwise layers do not move at all, because they never used the
  packed path: their channel access is not contiguous. The inversion is
  **structural to depthwise separable convolution**, not a library that nobody
  finished optimising, which puts it in MobileNet, DS-CNN and EfficientNet Lite
  alike.
* **Double every cycle count** and the mask is unchanged. The controller ranks
  on differences, so a slower clock or a thermal throttle changes whether the
  deadline is met, never which configuration is cheapest.
* **Give depthwise a kernel that wins** and the optimum collapses to plain INT8,
  found in ten demotions. The mixed answer exists exactly as long as the
  inversion does, and the controller finds the new one unprompted.

To port it: swap the signal source behind one interface, the model, and the test
for an interesting frame. Controller, calibration, deadline logic and weight
streaming are untouched.

Project code MIT, vendored µT-Kernel 3.0 under T-License 2.1 and 2.2.
github.com/kushal-script/utkernel-rtos-adaptive-kws
