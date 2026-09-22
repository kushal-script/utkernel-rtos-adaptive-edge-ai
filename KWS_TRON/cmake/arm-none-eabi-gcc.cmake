set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Arm GNU Toolchain, the bare metal arm-none-eabi release with newlib. The
# shipped image was built with 14.2.Rel1. Set ARM_TOOLCHAIN_DIR to its bin
# directory, or put it on PATH. A gcc without newlib, which is what the
# Homebrew arm-none-eabi-gcc formula is on its own, fails at stdint.h; see
# docs/operation_manual.md.
set(TOOLCHAIN_PREFIX arm-none-eabi-)
find_program(ARM_GCC ${TOOLCHAIN_PREFIX}gcc
    HINTS $ENV{ARM_TOOLCHAIN_DIR} ${ARM_TOOLCHAIN_DIR}
          "$ENV{HOME}/arm-gnu-toolchain/bin"
    PATH_SUFFIXES bin)
if(NOT ARM_GCC)
    message(FATAL_ERROR
        "arm-none-eabi-gcc not found. Install the Arm GNU Toolchain, bare metal "
        "with newlib, then set ARM_TOOLCHAIN_DIR to its bin directory or add it to PATH.")
endif()
get_filename_component(TOOLCHAIN_DIR ${ARM_GCC} DIRECTORY)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_DIR}/${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_DIR}/${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_DIR}/${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_OBJCOPY      ${TOOLCHAIN_DIR}/${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE         ${TOOLCHAIN_DIR}/${TOOLCHAIN_PREFIX}size)

set(CMAKE_C_COMPILER_FORCED   TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

# STM32H533RE — Cortex-M33 with FPU and DSP, hard float ABI
set(CPU_FLAGS "-mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS_INIT   "${CPU_FLAGS} -fdata-sections -ffunction-sections")
set(CMAKE_CXX_FLAGS_INIT "${CPU_FLAGS} -fdata-sections -ffunction-sections -fno-exceptions -fno-rtti")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS} -x assembler-with-cpp")

set(CMAKE_EXE_LINKER_FLAGS_INIT "${CPU_FLAGS} -Wl,--gc-sections -specs=nano.specs -specs=nosys.specs")

# Strip debug info for release
set(CMAKE_C_FLAGS_RELEASE_INIT   "-O2 -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG_INIT     "-Og -g3 -gdwarf-4")
