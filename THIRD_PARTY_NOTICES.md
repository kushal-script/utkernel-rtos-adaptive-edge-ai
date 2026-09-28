# Third party components

This project vendors third party source. Each component keeps its own licence and copyright, which are reproduced in the files themselves.

| Component | Location | Origin and licence |
| :-- | :-- | :-- |
| µT-Kernel 3.0 BSP2 | `KWS_TRON/mtk3/` | TRON Forum, copyright Ken Sakamura, distributed under the T-License 2.1 and 2.2 as stated in each file's header. Board support package for the NUCLEO-H533RE |
| STM32H5 HAL and CMSIS device headers | `KWS_TRON/Drivers/` | STMicroelectronics, terms in the LICENSE file in each directory |
| CMSIS Core | `KWS_TRON/Drivers/CMSIS/` | Arm Limited, Apache 2.0 |
| Startup and linker script | `KWS_TRON/Core/Startup/`, `STM32H533RETX_FLASH.ld` | STMicroelectronics, derived from the BSP |

The project's own code, everything under `KWS_TRON/app/`, `KWS_TRON/audio/`, `KWS_TRON/benchmark/`, `desktop/`, `model/`, `tools/` and `docs/`, is released under the MIT licence in [LICENSE](LICENSE). The vendored kernel is not. The T-License text is published by the TRON Forum at
<https://www.tron.org/t-license/>.

Two vendored kernel files carry project modifications. Both keep the original copyright and licence header, and each states its change where it is made:

* `KWS_TRON/mtk3/.../config/config.h`, the kernel configuration the BSP is designed to have edited per application.
* `KWS_TRON/mtk3/.../sysdepend/stm32_cube/power_save.c`, the kernel idle hook, which shipped as an empty function body and now sleeps. See [docs/power.md](docs/power.md).

Training data is not redistributed. `model/fetch_dataset.sh` downloads the Google Speech Commands v0.02 corpus (Creative Commons BY 4.0) at build time.
