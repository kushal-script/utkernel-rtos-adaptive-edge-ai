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
)

file(GLOB_RECURSE CORE_SOURCES
    "Core/Src/*.c"
    "Core/Startup/*.s"
)

file(GLOB_RECURSE HAL_SOURCES
    "Drivers/STM32H5xx_HAL_Driver/Src/*.c"
)

file(GLOB_RECURSE MTK3_SOURCES
    "mtk3/mtkernel/kernel/*.c"
    "mtk3/mtkernel/kernel/*.s"
    "mtk3/sysdepend/stm32_cube/*.c"
    "mtk3/sysdepend/stm32_cube/*.s"
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

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(${PROJECT_NAME} PRIVATE
    Core/Inc
    app
    audio
    # HAL + CMSIS device headers (from Drivers/ copied out of BSP zip)
    Drivers/STM32H5xx_HAL_Driver/Inc
    Drivers/STM32H5xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32H5xx/Include
    Drivers/CMSIS/Include
    # µT-Kernel 3.0 BSP2 headers
    mtk3/include
    mtk3/include/sys/sysdepend/stm32_cube/nucleo_stm32h533
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
