cmake_minimum_required(VERSION 3.22)

project(KWS_TRON C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ── Linker script ────────────────────────────────────────────────────────────
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/STM32H533RETX_FLASH.ld)

# ── Source files ─────────────────────────────────────────────────────────────
file(GLOB_RECURSE APP_SOURCES
    "app/*.c"
    "app/*.cpp"
    "benchmark/*.c"
)

file(GLOB_RECURSE CORE_SOURCES
    "Core/Src/*.c"
    "Core/Startup/*.s"
)

file(GLOB_RECURSE HAL_SOURCES
    "Drivers/STM32H5xx_HAL_Driver/Src/*.c"
)

# µT-Kernel kernel + lib: GLOB_RECURSE is fine — no platform variants here
file(GLOB_RECURSE MTK3_KERNEL_SOURCES
    "mtk3/mtk3_bsp2/mtkernel/kernel/*.c"
    "mtk3/mtk3_bsp2/mtkernel/kernel/*.s"
    "mtk3/mtk3_bsp2/mtkernel/kernel/*.S"
    "mtk3/mtk3_bsp2/mtkernel/lib/*.c"
    "mtk3/mtk3_bsp2/mtkernel/lib/*.s"
    "mtk3/mtk3_bsp2/mtkernel/lib/*.S"
    "mtk3/mtk3_bsp2/mtkernel/device/*.c"
)

# µT-Kernel BSP sysdepend: EXPLICIT list for STM32H533 (Cortex-M33 = ARMv8-M)
# Must NOT use GLOB_RECURSE here — the tree contains all STM32 family variants
# (stm32f4/f7/g4/h7/l4/h5/n6) and all board tm_com files. Including all of them
# causes multiply-defined symbols at link time.
set(MTK3_BSP_DIR mtk3/mtk3_bsp2/sysdepend/stm32_cube)
set(MTK3_SOURCES
    ${MTK3_KERNEL_SOURCES}

    # Top-level BSP glue
    ${MTK3_BSP_DIR}/power_save.c
    ${MTK3_BSP_DIR}/hw_setting.c
    ${MTK3_BSP_DIR}/devinit.c

    # CPU core — ARMv8-M only (STM32H533 is Cortex-M33)
    ${MTK3_BSP_DIR}/cpu/core/armv8m/cpu_cntl.c
    ${MTK3_BSP_DIR}/cpu/core/armv8m/interrupt.c
    ${MTK3_BSP_DIR}/cpu/core/armv8m/sys_start.c
    ${MTK3_BSP_DIR}/cpu/core/armv8m/exc_hdr.c
    ${MTK3_BSP_DIR}/cpu/core/armv8m/dispatch.S

    # libtk — ARMv8-M + STM32H5 timer/interrupt only
    ${MTK3_BSP_DIR}/lib/libtk/ptimer_stm32.c
    ${MTK3_BSP_DIR}/lib/libtk/cpu/core/armv8m/int_armv8m.c
    ${MTK3_BSP_DIR}/lib/libtk/cpu/core/armv8m/wusec_armv8m.c
    ${MTK3_BSP_DIR}/lib/libtk/cpu/stm32h5/ptimer_stm32h5.c
    ${MTK3_BSP_DIR}/lib/libtk/cpu/stm32h5/int_stm32h5.c

    # libtm — NUCLEO-H533 serial monitor only
    ${MTK3_BSP_DIR}/lib/libtm/nucleo_stm32h533/tm_com.c

    # Device drivers
    ${MTK3_BSP_DIR}/device/hal_i2c/hal_i2c.c
    ${MTK3_BSP_DIR}/device/hal_adc/hal_adc.c
    ${MTK3_BSP_DIR}/device/hal_adc/sysdepend/hal_adc_stm32h5.c
)

# TFLite Micro — uncomment after running project_generation script (Phase 3)
# file(GLOB_RECURSE TFLM_SOURCES "tflm/*.c" "tflm/*.cc" "tflm/*.cpp")

add_executable(${PROJECT_NAME}
    ${APP_SOURCES}
    ${CORE_SOURCES}
    ${HAL_SOURCES}
    ${MTK3_SOURCES}
    # ${TFLM_SOURCES}
)
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".elf")

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(${PROJECT_NAME} PRIVATE
    Core/Inc
    app
    audio
    benchmark
    # HAL + CMSIS device headers (from Drivers/ copied out of BSP zip)
    Drivers/STM32H5xx_HAL_Driver/Inc
    Drivers/STM32H5xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32H5xx/Include
    Drivers/CMSIS/Include
    # µT-Kernel 3.0 BSP2 headers
    mtk3/mtk3_bsp2
    mtk3/mtk3_bsp2/include
    mtk3/mtk3_bsp2/include/sys/sysdepend/stm32_cube/nucleo_stm32h533
    # BSP-level config (config.h, config_bsp.h — overrides mtkernel defaults)
    mtk3/mtk3_bsp2/config
    # mtkernel internal includes (knlinc/ kernel-private headers)
    mtk3/mtk3_bsp2/mtkernel/config
    mtk3/mtk3_bsp2/mtkernel/kernel/knlinc
    mtk3/mtk3_bsp2/mtkernel/device/include
    mtk3/mtk3_bsp2/mtkernel/device/common/drvif
    # BSP sysdepend headers — ARMv8-M core headers (cpu_status.h, cpu_task.h, sys_timer.h)
    mtk3/mtk3_bsp2/sysdepend/stm32_cube
    mtk3/mtk3_bsp2/sysdepend/stm32_cube/cpu/core/armv8m
    mtk3/mtk3_bsp2/sysdepend/stm32_cube/lib/libtk/cpu/stm32h5
    mtk3/mtk3_bsp2/sysdepend/stm32_cube/lib/libtm/nucleo_stm32h533
    # CMSIS-DSP (populated when STM32H5 firmware package is installed)
    # cmsis_dsp/Include
    # TFLite Micro (uncomment for Phase 3)
    # tflm
    # tflm/tensorflow/lite/micro
)

# ── Compile definitions ───────────────────────────────────────────────────────
target_compile_definitions(${PROJECT_NAME} PRIVATE
    STM32H533xx
    USE_HAL_DRIVER
    TF_LITE_STATIC_MEMORY
    TF_LITE_DISABLE_X86_NEON
    # µT-Kernel 3.0 BSP2: selects H533 machine.h → defines MTKBSP_STM32CUBE,
    # MTKBSP_CPU_CORE_ARMV8M, MTKBSP_CPU_STM32H533, etc.
    _STM32CUBE_NUCLEO_H533_
)

# ── CMSIS-DSP static library ──────────────────────────────────────────────────
# Populate after running: Help → STM32Cube Updater → STM32H5 in VS Code
# then set path to the .a file from the firmware package.
# target_link_libraries(${PROJECT_NAME}
#     ${CMAKE_SOURCE_DIR}/cmsis_dsp/Lib/GCC/libarm_cortexM33_math.a
# )

# ── Linker flags ──────────────────────────────────────────────────────────────
target_link_options(${PROJECT_NAME} PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,-Map=${PROJECT_NAME}.map,--cref
    -Wl,--print-memory-usage
)

# ── Post-build: generate .hex and .bin ───────────────────────────────────────
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
)
