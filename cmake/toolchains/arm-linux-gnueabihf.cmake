# Core 0 — embedded Linux user space on the Cortex-A9 (NEON, hard float).
# For builds against the image's exact sysroot, use the Yocto SDK instead:
#   source /opt/poky/<ver>/environment-setup-cortexa9t2hf-neon-poky-linux-gnueabi
#   cmake -S . -B build/linux-sdk -DSATLINK_TARGET=linux   (the SDK provides its own toolchain file)
# @implements SRS-BLD-001

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(SATLINK_TARGET linux)

set(SATLINK_CROSS_PREFIX "arm-linux-gnueabihf-" CACHE STRING "Cross compiler prefix")
set(CMAKE_C_COMPILER ${SATLINK_CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${SATLINK_CROSS_PREFIX}g++)

set(_satlink_cpu_flags "-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT "${_satlink_cpu_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_satlink_cpu_flags}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
