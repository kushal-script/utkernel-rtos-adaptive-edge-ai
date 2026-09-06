# What the optimum actually depends on

Three experiments testing whether the mixed precision result generalises, or
whether it is an artifact of this one build on this one part.

## 1. Remove the DSP extension, on hardware

Same sources, rebuilt with `-mcpu=cortex-m33+nodsp`, which undefines
`__ARM_FEATURE_DSP` and selects the scalar fallback in `kws_kernels.c`. The
packed path is gone: the object file holds zero `smlad` and zero `sxtb16`
against four and eight in the shipped build. Flashed and measured.

The prediction was that the inversion would disappear, because the story told
until now was that depthwise loses in INT8 for want of a packed kernel. **The
prediction was wrong.**

| Layer group | INT8 cost change without DSP | INT8 against FP32, with DSP | without DSP |
| :-- | --: | --: | --: |
| Four depthwise | 0.5 percent, noise | 42.9 percent slower | 43.0 percent slower |
| Everything else | 33 to 39 percent slower | 38.4 percent faster | 23.7 percent faster |

The depthwise layers are untouched by removing the DSP extension because they
never used it. Depthwise convolution reads one channel per output with a stride
of the channel count, so its operands are never contiguous and `dot_run` never
applies to them. What is left is that a depthwise output costs nine multiply
accumulates while a pointwise output costs sixty four or more, so INT8's fixed
per output overhead, the offset add and the 64 bit requantisation, is amortised
in one case and dominant in the other.

That makes the inversion **structural to depthwise separable convolution on a
microcontroller**, not a property of an immature kernel library. The controller
still converged on 0x0AA, and the adaptive point still beat both static builds
at 110.8 ms against 114.2 and 126.0.

## 2. Uniform cost scaling, counterfactual

`counterfactuals/cf_uniform2x.txt` doubles every cycle count. The controller
converges on the same 0x0AA in the same six demotions and four promotions.

This is provable rather than lucky: `best_move` ranks candidates on
`fp32_cycles[i] - int8_cycles[i]` and carries no deadline term, so scaling every
cost by a constant scales every difference by that constant and cannot reorder
them. A slower clock, a thermal throttle or a voltage scaled part therefore
changes whether the deadline is met, and never changes which mask is cheapest.

## 3. A depthwise kernel that does not lose, counterfactual

`counterfactuals/cf_dwfixed.txt` makes the four depthwise layers 40 percent
cheaper in INT8, which is roughly what a channel blocked layout would buy, and
leaves every other layer alone. The controller converges on mask 0x000, all
INT8, in ten demotions and zero promotions.

So the mixed optimum exists exactly as long as the inversion does. If someone
writes a depthwise kernel that wins, the right answer becomes plain INT8, and
this controller finds that answer too without being told.

## What the three together establish

The operating point is a property of the relative per layer cost of the model
and the kernels, and of nothing else. It survives losing the DSP extension, it
survives an arbitrary uniform change of speed, and it moves precisely when the
relative costs move. That is the case for measuring at runtime instead of
deciding at compile time.

## Reproducing

```bash
cmake -S KWS_TRON -B build/nodsp -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/KWS_TRON/cmake/arm-none-eabi-gcc.cmake" \
      -DCMAKE_C_FLAGS="-mcpu=cortex-m33+nodsp -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard -fdata-sections -ffunction-sections" \
      -DCMAKE_ASM_FLAGS="-mcpu=cortex-m33+nodsp -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard -x assembler-with-cpp"
cmake --build build/nodsp
# flash build/nodsp/KWS_TRON.bin, then capture as usual

./build/desktop/kws-desktop converge --capture counterfactuals/cf_uniform2x.txt
./build/desktop/kws-desktop converge --capture counterfactuals/cf_dwfixed.txt
```

The board was returned to the shipped firmware afterwards and reproduces
99344, 125964 and 95941 microseconds, unchanged.
