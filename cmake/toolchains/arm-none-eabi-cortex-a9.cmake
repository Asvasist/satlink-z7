# Core 1 — FreeRTOS firmware on the Cortex-A9 (same flags as the AMD standalone BSP).
# @implements SRS-BLD-001

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(SATLINK_TARGET rtos)

set(SATLINK_CROSS_PREFIX "arm-none-eabi-" CACHE STRING "Cross compiler prefix")
set(CMAKE_C_COMPILER ${SATLINK_CROSS_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${SATLINK_CROSS_PREFIX}gcc)
set(CMAKE_AR ${SATLINK_CROSS_PREFIX}ar)
set(CMAKE_OBJCOPY ${SATLINK_CROSS_PREFIX}objcopy)
set(CMAKE_SIZE ${SATLINK_CROSS_PREFIX}size)

# No OS to link against during compiler checks.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard")
