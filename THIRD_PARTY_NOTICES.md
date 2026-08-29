# Third party components

This project vendors third party source. Each component keeps its own licence
and copyright, which are reproduced in the files themselves.

| Component | Location | Origin and licence |
| :-- | :-- | :-- |
| µT-Kernel 3.0 BSP2 | `KWS_TRON/mtk3/` | TRON Forum, MIT licence. Board support package for the NUCLEO-H533RE |
| STM32H5 HAL and CMSIS device headers | `KWS_TRON/Drivers/` | STMicroelectronics, terms in the LICENSE file in each directory |
| CMSIS Core | `KWS_TRON/Drivers/CMSIS/` | Arm Limited, Apache 2.0 |
| Startup and linker script | `KWS_TRON/Core/Startup/`, `STM32H533RETX_FLASH.ld` | STMicroelectronics, derived from the BSP |

One vendored file carries a project modification, marked in its header:

* `KWS_TRON/mtk3/.../sysdepend/stm32_cube/power_save.c`, the kernel idle hook,
  which shipped as an empty function body and now sleeps. See
  [docs/power.md](docs/power.md).

Training data is not redistributed. `model/fetch_dataset.sh` downloads the
Google Speech Commands v0.02 corpus (Creative Commons BY 4.0) at build time.
