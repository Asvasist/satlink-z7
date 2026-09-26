# rtos::axi_dma - AXI DMA scatter-gather ring (Core 1 firmware and host tests).
# @implements SRS-AMP-006
add_library(rtos_axi_dma STATIC src/axi_dma.c)
add_library(rtos::axi_dma ALIAS rtos_axi_dma)
target_include_directories(rtos_axi_dma PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
satlink_target_defaults(rtos_axi_dma)
