# Core 1 modem firmware (SATLINK_TARGET=rtos): FreeRTOS on the Cortex-A9, linked into the
# rtos_fw DDR partition and loaded by the satlink_amp Linux driver.
#
#   satlink_rtos.elf   the image Linux loads (/lib/firmware/satlink-rtos.elf)
#
# Offline builds: -DFETCHCONTENT_SOURCE_DIR_FREERTOS_KERNEL=<FreeRTOS-Kernel checkout>
# @implements SRS-AMP-001

enable_language(ASM)
include(FetchContent)

FetchContent_Declare(freertos_kernel
    URL https://github.com/FreeRTOS/FreeRTOS-Kernel/archive/refs/tags/V11.1.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR cmake-not-used  # sources are compiled below with our port settings
)
FetchContent_MakeAvailable(freertos_kernel)
set(FREERTOS_SRC ${freertos_kernel_SOURCE_DIR})

add_library(freertos_kernel STATIC
    ${FREERTOS_SRC}/tasks.c
    ${FREERTOS_SRC}/queue.c
    ${FREERTOS_SRC}/list.c
    ${FREERTOS_SRC}/timers.c
    ${FREERTOS_SRC}/event_groups.c
    ${FREERTOS_SRC}/stream_buffer.c
    ${FREERTOS_SRC}/portable/MemMang/heap_4.c
    ${FREERTOS_SRC}/portable/GCC/ARM_CA9/port.c
    ${FREERTOS_SRC}/portable/GCC/ARM_CA9/portASM.S
)
target_include_directories(freertos_kernel
    PUBLIC
        ${FREERTOS_SRC}/include
        ${FREERTOS_SRC}/portable/GCC/ARM_CA9
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# Third-party code: built as released, without the project warning set.
target_compile_options(freertos_kernel PRIVATE -w -ffunction-sections -fdata-sections)

add_executable(satlink_rtos
    bsp/startup.S
    bsp/mmu.c
    bsp/gic.c
    bsp/timer.c
    bsp/uart.c
    bsp/fault.c
    bsp/syscalls.c
    app/main.c
    app/ipc.c
    app/payload.c
    app/modem_task.c
    app/monitor.c
)
set_target_properties(satlink_rtos PROPERTIES SUFFIX ".elf")
target_include_directories(satlink_rtos PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(satlink_rtos PRIVATE
    freertos_kernel satlink::modem_app satlink::amp satlink::modem satlink::regs rtos::axi_dma)
target_link_options(satlink_rtos PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/rtos.ld
    -nostartfiles --specs=nano.specs --specs=nosys.specs
    -Wl,--gc-sections -Wl,--no-warn-rwx-segments
    -Wl,-Map=$<TARGET_FILE_DIR:satlink_rtos>/satlink_rtos.map)
satlink_target_defaults(satlink_rtos)
add_custom_command(TARGET satlink_rtos POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:satlink_rtos>
    VERBATIM)
