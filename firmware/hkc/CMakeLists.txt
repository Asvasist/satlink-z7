# Housekeeping controller firmware for the MicroBlaze V: bootloader and application.
# Built only for SATLINK_TARGET=hkc (cmake --preset hkc-riscv). Freestanding, no C library:
# the start-up code, the linker scripts and the few memcpy/memset helpers are in this directory.
# @implements SRS-HKC-004

set(HKC_CLOCK_HZ 100000000 CACHE STRING "MicroBlaze V and AXI peripheral clock in Hz")
set(HKC_CAN_OSC_HZ 16000000 CACHE STRING "Oscillator on PmodCAN #2 in Hz")

add_library(hkc_platform STATIC
    platform/hkc_platform.c
    platform/libc_min.c
)
target_include_directories(hkc_platform PUBLIC platform)
target_link_libraries(hkc_platform PUBLIC satlink::can satlink::common satlink::regs)
target_compile_definitions(hkc_platform PUBLIC
    HKC_CLOCK_HZ=${HKC_CLOCK_HZ}U
    HKC_CAN_OSC_HZ=${HKC_CAN_OSC_HZ}U
)
target_compile_options(hkc_platform PUBLIC -ffreestanding)
satlink_target_defaults(hkc_platform)

find_package(Python3 COMPONENTS Interpreter)

# hkc_firmware(<name> <linker script> <sources...>): links <name>.elf and writes <name>.bin.
function(hkc_firmware name linker_script)
    add_executable(${name}.elf ${ARGN} platform/crt0.S)
    target_link_libraries(${name}.elf PRIVATE hkc_platform)
    target_link_options(${name}.elf PRIVATE
        -nostartfiles
        -nostdlib
        -Wl,--gc-sections
        -Wl,--build-id=none
        -Wl,--no-warn-rwx-segments
        -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/linker/${linker_script}
        -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/${name}.map
    )
    set_target_properties(${name}.elf PROPERTIES
        LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker/${linker_script})
    satlink_target_defaults(${name}.elf)
    add_custom_command(TARGET ${name}.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}.elf> ${CMAKE_CURRENT_BINARY_DIR}/${name}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}.elf>
        VERBATIM
    )
endfunction()

hkc_firmware(hkc_bootloader bootloader.ld bootloader/main.c)
target_link_libraries(hkc_bootloader.elf PRIVATE satlink::boot)

hkc_firmware(hkc_app app.ld app/main.c)
target_link_libraries(hkc_app.elf PRIVATE satlink::boot satlink::hk)

# The application image gets its header (magic, size, CRC) from mkapp.py.
if(Python3_FOUND)
    add_custom_command(TARGET hkc_app.elf POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/hkc/mkapp.py
                ${CMAKE_CURRENT_BINARY_DIR}/hkc_app.bin ${CMAKE_CURRENT_BINARY_DIR}/hkc_app.img
        VERBATIM
    )
endif()
