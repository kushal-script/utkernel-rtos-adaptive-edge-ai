# RTOS-Coupled Adaptive Edge AI

Keyword spotting on the STM32H533RE (Cortex-M33, 250 MHz) where µT-Kernel 3.0 actively co-optimises inference at runtime instead of merely scheduling a static model. Every inference cycle the kernel reads the DWT cycle counter and live signal statistics, then reshapes how the model runs: capture window size, feature gating, task priority, and numeric precision. The kernel and the model form a closed feedback loop.

TRON Programming Contest 2026, RTOS Application (Students). Target board: NUCLEO-H533RE. Microphone: INMP441 I2S MEMS.

## Status

Phase 1, audio ingest. Boot chain, 250 MHz clock, DWT cycle counter, µT-Kernel 3.0, heartbeat task, and the T1 I2S DMA capture path from the INMP441 are up. Feature extraction, inference, and the adaptation controller are not yet implemented. Current position and next steps live in [docs/roadmap.md](docs/roadmap.md).

## Repository layout

| Path | Purpose |
| :-- | :-- |
| `KWS_TRON/` | Firmware, CMake project. HAL init, µT-Kernel BSP, application |
| `KWS_TRON/app/` | Task graph T1 to T5, IPC objects, adaptation controller |
| `KWS_TRON/benchmark/` | DWT and power instrumentation |
| `KWS_TRON/audio/` | MFCC configuration and the exported model header |
| `docs/` | Architecture, roadmap, novelty, hardware, benchmarking |
| `model/` | Training and export pipeline, versioned model artifacts |
| `experiments/` | Timestamped experiment and model runs |
| `tools/` | Host side Python for mic verification, FFT check, plotting |

## Documentation

* [Architecture](docs/architecture.md), the five task pipeline and IPC map
* [Roadmap](docs/roadmap.md), milestones and exit criteria
* [Novelty](docs/novelty.md), the research thesis and how it differs from standard TinyML
* [Hardware](docs/hardware.md), board, mic wiring, clock tree, memory
* [Mic verification](docs/mic_verification.md), the M1 bring up probe and host tool
* [Benchmarking](docs/benchmarking.md), how latency, power, and accuracy are measured

## Build and flash

```
source setup_env.sh
cmake -S KWS_TRON -B build/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug
flash
```

`setup_env.sh` activates the Python venv and puts the ARM toolchain and ST tools on `PATH`. The `flash` and `connect` aliases wrap `STM32_Programmer_CLI` and the serial console. Toolchain detail is in [docs/hardware.md](docs/hardware.md).

## License

Open source release planned after the contest submission. This is intended as a reusable template for real-time edge AI on constrained hardware.
