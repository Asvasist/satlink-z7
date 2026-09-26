# MicroBlaze V housekeeping controller (SATLINK_TARGET=hkc): bootloader and application.
#
#   hkc_bootloader.elf  baked into the bitstream (Vivado: associate the ELF with the MicroBlaze
#                       V instance, or updatemem -data hkc_bootloader.elf ...)
#   hkc_app.slhk        application image (header + binary) loaded over CAN by satlink-hkc
#
# @implements SRS-HKC-001
# @implements SRS-HKC-002

enable_language(ASM)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_library(hkc_bsp STATIC
    bsp/crt0.S
    bsp/libc_min.c
    drivers/drivers.c
)
target_include_directories(hkc_bsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hkc_bsp PUBLIC satlink::regs)
satlink_target_defaults(hkc_bsp)

set(HKC_COMMON_FLAGS -ffreestanding -fno-builtin -Os)
set(HKC_LINK_FLAGS -nostdlib -nostartfiles -Wl,--gc-sections -Wl,--no-warn-rwx-segments
                   -L${CMAKE_CURRENT_SOURCE_DIR}/linker)
target_compile_options(hkc_bsp PRIVATE ${HKC_COMMON_FLAGS})
target_compile_options(satlink_common PRIVATE ${HKC_COMMON_FLAGS})
target_compile_options(satlink_hkc_proto PRIVATE ${HKC_COMMON_FLAGS})
target_compile_options(satlink_mcp2515 PRIVATE ${HKC_COMMON_FLAGS})
target_compile_options(hkc_app_logic PRIVATE ${HKC_COMMON_FLAGS})

# hkc_firmware(<name> <linker script> <sources...>)
function(hkc_firmware name script)
    add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES SUFFIX ".elf")
    target_compile_options(${name} PRIVATE ${HKC_COMMON_FLAGS})
    # crt0.S is in a static library; pull it in explicitly so _start is always linked.
    target_link_options(${name} PRIVATE ${HKC_LINK_FLAGS}
        -T${CMAKE_CURRENT_SOURCE_DIR}/linker/${script}
        -Wl,-Map=$<TARGET_FILE_DIR:${name}>/${name}.map
        -Wl,--undefined=_start)
    target_link_libraries(${name} PRIVATE
        -Wl,--whole-archive hkc_bsp -Wl,--no-whole-archive
        satlink::mcp2515 satlink::hkc_proto satlink::common gcc)
    satlink_target_defaults(${name})
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> $<TARGET_FILE_DIR:${name}>/${name}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>
        VERBATIM)
endfunction()

hkc_firmware(hkc_bootloader bootloader.ld bootloader/main.c)
hkc_firmware(hkc_app app.ld app/main.c)
target_link_libraries(hkc_app PRIVATE hkc::app_logic)

add_custom_command(TARGET hkc_app POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/hkc/mkimage.py
            --load-addr 0x4000 --region-size 0x1C000 --version 1.0.0
            $<TARGET_FILE_DIR:hkc_app>/hkc_app.bin $<TARGET_FILE_DIR:hkc_app>/hkc_app.slhk
    VERBATIM)
