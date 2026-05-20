cmake_minimum_required(VERSION 3.22)

project(KWS_TRON C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ── Linker script ────────────────────────────────────────────────────────────
# Place STM32H533RETX_FLASH.ld in KWS_TRON/ (generate via CubeMX or copy from BSP)
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

# TFLite Micro — added after generating the standalone source tree
# file(GLOB_RECURSE TFLM_SOURCES "tflm/*.c" "tflm/*.cc")

add_executable(${PROJECT_NAME}
    ${APP_SOURCES}
    ${CORE_SOURCES}
    # ${TFLM_SOURCES}
)

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(${PROJECT_NAME} PRIVATE
    Core/Inc
    app
    audio
    mtk3/tkernel/include
    mtk3/tkernel/include/sys
    cmsis_dsp/Include
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
# Uncomment and set path after installing STM32H5 firmware package via CubeMX
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
