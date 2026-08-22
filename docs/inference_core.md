# Inference core

The model runner in `audio/kws_infer.c` and the layer kernels in
`audio/kws_kernels.c`. Written by hand rather than taken from an interpreter,
because the contribution needs something an interpreter cannot do: change a
layer's numeric precision between one inference and the next.

## Why not an interpreter

A graph interpreter fixes each tensor's datatype when the graph is built. To
switch a layer to a different precision at runtime you would have to carry two
graphs and swap whole models, which is a coarser mechanism than the one the
program plan describes and gives a worse per layer timing story.

The deployed graph here is deliberately flat. Batch normalisation is folded
into the convolution before it at export time, so every layer is one of
convolution, depthwise, pointwise, or fully connected, each optionally followed
by a ReLU, with a single global average pool before the classifier. Ten layers,
one descriptor table, one loop. That is small enough to read in a sitting and
to instrument at every boundary.

No external inference or transform library is linked. The transform, the mel
filterbank application, and every layer kernel are in the tree. That keeps the
build self contained, and it means every cycle the benchmark reports belongs to
code in this repository.

## The layer table

`export.py` emits one `kws_layer_t` per layer holding both an INT8 copy with
per channel requantisation parameters and an FP32 copy, plus the shapes and the
boundary scales. The runtime walks the table:

| Field | Purpose |
| :-- | :-- |
| `weight_int8`, `bias_int32` | INT8 path, weights OHWI, depthwise 1HWC |
| `multiplier`, `shift` | Per output channel requantisation |
| `weight_fp32`, `bias_fp32` | FP32 path, same layout, dequantised |
| `input_scale`, `input_offset` | Fixed at export, meaning of the input tensor |
| `output_scale`, `output_offset` | Fixed at export, meaning of the output tensor |
| `activation_min`, `activation_max` | Clamp, see the ReLU note below |

## Quantisation

Activations are per tensor asymmetric int8, `real = scale * (q - zero)`.
Weights are per channel symmetric, so their zero point is always zero. Bias is
int32 at the product scale `input_scale * weight_scale`, which makes the bias
addition exact rather than a second source of rounding.

The output multiplier `M = input_scale * weight_scale / output_scale` is split
into an int32 significand and a shift, and applied with a rounding doubling
high multiply followed by a rounding shift. That is the same convention
published inference libraries use, and the integer result matches float
rounding of the same product exactly, which is checked in the host tests.

One detail is easy to get wrong and was got wrong once here: **a ReLU in the
quantised domain clamps at the value that represents zero, which is the output
zero point, not the byte zero**. With an activation range starting at zero the
zero point is at the bottom of the int8 range, so clamping at byte zero forces
every activation to sit well above the true zero and the network collapses.
`activation_min` carries the correct bound and the kernels use it directly.

## Switching precision at runtime

`kws_infer` takes a bit mask, one bit per layer, set for FP32. Where two
consecutive layers disagree, the activation is converted at the boundary using
the scale recorded in the layer table.

This works because the boundary scales are fixed at export time from
calibration data and never recomputed. A layer's output means the same thing
whichever precision produced it, so the next layer can read it either way. The
alternative, deriving a scale from the tensor at runtime, would make a
precision switch change the numbers rather than just the arithmetic that
produced them.

The conversion costs one multiply and one add per element and is only paid at
an actual boundary, so an all INT8 or all FP32 run pays nothing.

## Weight streaming

`kws_weights_acquire` is a weak symbol. The plain build returns the flash
pointer. The RTOS build in `app/t4_inference.c` overrides it to take a block
from the memory pool, copy the layer's weights into it, and release the block
as soon as the layer finishes, so peak SRAM holds one layer rather than the
whole model.

Being honest about what that buys: with the model resident in flash, streaming
does not reduce the memory the model occupies, it demonstrates the mechanism
and produces the peak figure the benchmark reports. The technique earns its
place when the weights come from somewhere that is not directly addressable, or
when the model is larger than SRAM, and the pool sizing here is what makes that
case runnable rather than hypothetical.

## How correctness is established

The device core is never the first implementation of anything. `convert.py`
carries a NumPy reference of exactly the arithmetic the C performs, and that
reference is checked against the trained float model before any C runs. The C
is then compiled natively and checked against the reference.

At the last run the NumPy reference agreed with the float model on every
verification sample, and the C core reproduced the reference logits to within
five parts in a hundred million under mixed precision, reaching the same
accuracy on the evaluation set under all INT8, all FP32, and alternating per
layer precision. A disagreement on hardware is therefore a hardware or
toolchain question, never an unknown in the maths.

## On the speed claim

The program plan explains the INT8 advantage as one cycle per multiply
accumulate against four for FP32. That is not the mechanism, and the number
should not be repeated. The Cortex-M33 here has a single precision floating
point unit, so an FP32 multiply accumulate is not four ALU operations.

What actually makes INT8 faster is that weights are a quarter of the size and
so cost a quarter of the memory traffic, that the DSP extension can pack
multiply accumulates, and that integer work avoids moving values in and out of
the floating point register file. The honest claim is the measured ratio on
this silicon, which the benchmark reports for the same model and the same
input, and which is currently unmeasured because the board has not run it yet.
