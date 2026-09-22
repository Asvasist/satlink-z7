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

set(CMAKE_C_COMPILER ${SATLINK_CROSS_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${SATLINK_CROSS_PREFIX}gcc)
set(CMAKE_AR ${SATLINK_CROSS_PREFIX}ar)
set(CMAKE_OBJCOPY ${SATLINK_CROSS_PREFIX}objcopy)
set(CMAKE_SIZE ${SATLINK_CROSS_PREFIX}size)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-march=${SATLINK_RISCV_ARCH} -mabi=${SATLINK_RISCV_ABI}")
set(CMAKE_ASM_FLAGS_INIT "-march=${SATLINK_RISCV_ARCH} -mabi=${SATLINK_RISCV_ABI}")
