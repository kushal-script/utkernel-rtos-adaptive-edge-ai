# Hardware

## Board

NUCLEO-H533RE. STM32H533RET6, Cortex-M33 at 250 MHz, 272 KB SRAM, TrustZone, single precision FPU, on board ST-LINK V3 with SWD and SWO. Power is measurable through the SMPS for milliwatt level current readings.

## Clock tree

System clock is 250 MHz derived from the internal HSI.

```
HSI 64 MHz / PLLM 16      = 4 MHz    PLL input, VCIRANGE_1 range 2 to 4 MHz
4 MHz * PLLN 125          = 500 MHz  VCO, wide range 192 to 836 MHz
500 MHz / PLLP 2          = 250 MHz  SYSCLK
```

PLL1Q and PLL1R stay at divide by 2 so peripheral domains also see 250 MHz. AHB and all APB buses are undivided. Voltage scaling 0 is required above 200 MHz, and flash runs at five wait states with the high HCLK programming delay set. Configured in `SystemClock_Config` in `Core/Src/main.c`.

## Microphone, INMP441

The INMP441 is an I2S MEMS microphone. It outputs 24 bit data in a 32 bit I2S frame, Philips standard, one channel selected by its L R pin.

| INMP441 | Connection |
| :-- | :-- |
| VDD | 3V3 |
| GND | GND |
| SD | PB15, I2S2 SD, AF5 |
| SCK | PB13, I2S2 CK, AF5 |
| WS | PB12, I2S2 WS, AF5 |
| L R | GND for the left slot, or VDD for the right |

Capture is at 16 kHz. That is the standard KWS sample rate, gives roughly 4 kHz of voice band with headroom under the 8 kHz Nyquist limit, and matches typical trained model exports without resampling.

## Capture buffer

The DMA target is `int32_t[1024]`, because each 24 bit I2S slot occupies one 32 bit word in memory. 1024 words is 4 KB, which is 512 stereo frames, which is 32 ms at 16 kHz. The half transfer event therefore fires every 16 ms. The INMP441 places 24 bit signed data in the upper bits of the left slot, every other word, and leaves the right slot at zero.

The STM32H5 has no data cache, so DMA and CPU see the same memory with no flush or invalidate. Alignment is four bytes.

GPDMA1 clock and IRQ are enabled before I2S2 init, because `HAL_I2S_MspInit` links the DMA channel to the I2S handle. The channel configuration for SPI2 RX lives in `HAL_I2S_MspInit` in `Core/Src/stm32h5xx_hal_msp.c`.

## I2S kernel clock

SPI2 on the H5 has no PCLK or HSI kernel clock option, it must be fed from a PLL output, so it takes PLL1Q at 250 MHz. The HAL computes a divider of roughly 122 for 16 kHz at 64 bits per frame, which lands about 0.07 percent off the exact rate, well inside what the INMP441 tolerates. If low jitter ever matters, a dedicated audio PLL on PLL2P would give an exact ratio.

## DMA transfer shape

The GPDMA channel runs in circular linked list mode with a single node that describes the whole buffer and loops back to itself, so capture never gaps between blocks. The I2S driver overwrites the node size, source, and destination at receive start. `Init.Mode` must carry the circular value, otherwise the I2S receive complete handler disables the DMA request after the first block. Transfers are halfword wide because the I2S data register presents each 32 bit slot as two 16 bit beats.

## DWT cycle counter

The DWT CYCCNT register is the timing source for the whole adaptation loop. `dwt_init` enables the trace unit and the cycle counter, `dwt_read` returns the count, and `dwt_log_layer` streams per layer cycle counts over SWO ITM port 0. See `benchmark/dwt_logger.c`.

## MPU

The MPU is disabled. The generated `MPU_Config` guarded only the option byte page, which is already read only in hardware, and its privileged default mode bus faulted the unprivileged µT-Kernel tasks the moment they touched DMA or peripheral registers. TrustZone is the intended isolation mechanism for this project, not the MPU. The disabled configuration function is kept in `main.c` for reference.

## Toolchain

| Tool | Version and source |
| :-- | :-- |
| Compiler | `arm-none-eabi-gcc`, Homebrew formula |
| Build | CMake, Ninja or Make |
| Flash | `STM32_Programmer_CLI`, ST bundle |
| Debug | ST-LINK GDB server, ST bundle, SWO for cycle logging |
| Kernel | µT-Kernel 3.0 BSP2 for STM32H533 |

`setup_env.sh` activates the venv and puts all of the above on `PATH`, and defines the `flash`, `connect`, and `probe` aliases.
