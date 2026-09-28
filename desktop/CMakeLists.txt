cmake_minimum_required(VERSION 3.16)
project(kws_desktop C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(FIRMWARE "${CMAKE_CURRENT_SOURCE_DIR}/../KWS_TRON" CACHE PATH
    "Firmware tree whose sources this target compiles for the host")

if(NOT EXISTS "${FIRMWARE}/app/t1_ingest.c")
    message(FATAL_ERROR
        "No firmware tree at ${FIRMWARE}. This target compiles the real task "
        "sources, so build desktop/ from inside the repository, or pass "
        "-DFIRMWARE=<path to KWS_TRON>.")
endif()

# Listed, not globbed: signal_source.c and app_main.c touch the HAL and are the two files replaced here.
set(FIRMWARE_SOURCES
    ${FIRMWARE}/app/t1_ingest.c
    ${FIRMWARE}/app/t2_variance.c
    ${FIRMWARE}/app/t3_features.c
    ${FIRMWARE}/app/t4_inference.c
    ${FIRMWARE}/app/t5_controller.c
    ${FIRMWARE}/app/ipc_objects.c
    ${FIRMWARE}/app/sample_ring.c
    ${FIRMWARE}/audio/kws_fft.c
    ${FIRMWARE}/audio/kws_features.c
    ${FIRMWARE}/audio/kws_kernels.c
    ${FIRMWARE}/audio/kws_infer.c
    ${FIRMWARE}/audio/kws_model.c
    ${FIRMWARE}/audio/mfcc_tables.c
    ${FIRMWARE}/audio/eval_set.c
    ${FIRMWARE}/audio/replay_data.c
)

set(HOST_SOURCES
    src/main.c
    src/ukernel.c
    src/uthread.c
    src/host_clock.c
    src/host_source.c
    src/wav.c
    src/device_costs.c
)

add_executable(kws-desktop ${HOST_SOURCES} ${FIRMWARE_SOURCES})

target_include_directories(kws-desktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FIRMWARE}/app
    ${FIRMWARE}/audio
    ${FIRMWARE}/benchmark
)

target_compile_definitions(kws-desktop PRIVATE _USE_MATH_DEFINES)

# Floating point contraction off, or arm64 and x86-64 hosts disagree on FP32 results.
if(NOT MSVC)
    target_compile_options(kws-desktop PRIVATE
        -Wall -Wextra -ffp-contract=off)
endif()

find_package(Threads REQUIRED)
target_link_libraries(kws-desktop PRIVATE Threads::Threads)

if(NOT WIN32)
    target_link_libraries(kws-desktop PRIVATE m)
endif()

if(MSVC)
    message(FATAL_ERROR
        "MSVC cannot build this target. The inference core defines its cycle "
        "counter and weight streaming hooks as weak symbols, which MSVC has no "
        "equivalent for, and overriding that would mean editing the firmware "
        "source this program exists to run unmodified. Use clang or MinGW-w64 "
        "on Windows, see desktop/README.md.")
endif()
