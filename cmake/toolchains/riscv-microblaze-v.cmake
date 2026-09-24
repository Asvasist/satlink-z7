# MicroBlaze V housekeeping controller — bare metal, RV32.
# SATLINK_RISCV_ARCH must match the MicroBlaze V configuration in Vivado (Microcontroller preset).
# The riscv64-unknown-elf toolchain from Vitis or from the distribution both work.
# @implements SRS-BLD-001

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)
set(SATLINK_TARGET hkc)

set(SATLINK_CROSS_PREFIX "riscv64-unknown-elf-" CACHE STRING "Cross compiler prefix")
set(SATLINK_RISCV_ARCH "rv32imc_zicsr_zifencei" CACHE STRING "RISC-V -march for MicroBlaze V")
set(SATLINK_RISCV_ABI "ilp32" CACHE STRING "RISC-V -mabi for MicroBlaze V")

# CMake's compiler test runs in a separate project that only sees these if told to pass them on.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES SATLINK_CROSS_PREFIX SATLINK_RISCV_ARCH SATLINK_RISCV_ABI)

# find_program also resolves the .exe suffix when the prefix is a full path on Windows.
foreach(_tool gcc ar objcopy size)
    find_program(SATLINK_RISCV_${_tool} NAMES ${SATLINK_CROSS_PREFIX}${_tool} REQUIRED)
endforeach()

set(CMAKE_C_COMPILER ${SATLINK_RISCV_gcc})
set(CMAKE_ASM_COMPILER ${SATLINK_RISCV_gcc})
set(CMAKE_AR ${SATLINK_RISCV_ar})
set(CMAKE_OBJCOPY ${SATLINK_RISCV_objcopy})
set(CMAKE_SIZE ${SATLINK_RISCV_size})

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-march=${SATLINK_RISCV_ARCH} -mabi=${SATLINK_RISCV_ABI}")
set(CMAKE_ASM_FLAGS_INIT "-march=${SATLINK_RISCV_ARCH} -mabi=${SATLINK_RISCV_ABI}")
