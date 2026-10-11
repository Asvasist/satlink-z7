#------------------------------------------------------------------------------
# create_project.tcl - SatLink-Z7 hardware baseline hw-v1 (Zybo Z7-20)
#
# Recreates the Vivado project and block design from scratch. Addresses,
# interrupts and pins follow icd/address_map.yaml and hw/README.md.
#
#   B1  PS7: board preset + the PS peripherals the ICD assigns
#   B2  AXI GPIO on M_AXI_GP0 (LD0-3, BTN0-3), owned by Core 1 / FreeRTOS
#   B3  Housekeeping controller: MicroBlaze V with its own peripherals,
#       bootloader baked into the LMB BRAM
#   B6  Interrupts, address map, constraints, wrapper
#
# Not in hw-v1: the Linux PL accelerators (ccsds_frame_accel, spec_tap + FFT)
# and the modem datapath (payload_ctrl, I2S, NCOs, RRC filters, DMAs).
#
# Usage, from the repository root:
#   vivado -mode batch -source hw/scripts/create_project.tcl
#   vivado -mode batch -source hw/scripts/create_project.tcl -tclargs <hkc_bootloader.elf>
#
# The bootloader ELF defaults to the hkc-riscv CMake preset output
# (cmake --preset hkc-riscv && cmake --build --preset hkc-riscv).
#------------------------------------------------------------------------------

set hw_dir    [file normalize [file join [file dirname [info script]] ..]]
set repo_dir  [file normalize [file join $hw_dir ..]]
set proj_name satlink_hw
set proj_dir  [file join $hw_dir vivado $proj_name]
set bd_name   satlink_bd
set hkc_elf   [expr {[llength $argv] > 0 ? [file normalize [lindex $argv 0]] \
                    : [file join $repo_dir build hkc-riscv firmware hkc hkc_bootloader.elf]}]

if {[version -short] ne "2025.2"} {
    puts "WARNING: written for Vivado 2025.2, this is [version -short]"
}
if {![file exists $hkc_elf]} {
    error "HKC bootloader not found: $hkc_elf\nBuild it first: cmake --preset hkc-riscv && cmake --build --preset hkc-riscv"
}

set board_part [lindex [lsort [get_board_parts -quiet -latest_file_version "*:zybo-z7-20:*"]] end]
if {$board_part eq ""} {
    error "Digilent board files for zybo-z7-20 are not installed"
}

create_project $proj_name $proj_dir -part xc7z020clg400-1 -force
set_property board_part $board_part [current_project]
create_bd_design $bd_name

# Bring an interface pin out as a top-level port with a fixed name (used by the XDC).
proc ext {pin name} {
    set p [get_bd_intf_pins $pin]
    connect_bd_intf_net $p [create_bd_intf_port -mode [get_property MODE $p] -vlnv [get_property VLNV $p] $name]
}

#==============================================================================
# B1  Zynq PS
#==============================================================================
set ps7 [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 ps7]
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 \
    -config {make_external "FIXED_IO, DDR" apply_board_preset "1" Master "Disable" Slave "Disable"} $ps7

# On top of the preset (UART1 on MIO48/49, GEM0, SD0, USB0, MIO GPIO):
#   UART0 via EMIO     FreeRTOS console on Pmod JE1/JE2 (100 MHz ref -> 115200 in bsp/uart.c)
#   I2C0 via EMIO      SSM2603 codec configuration
#   SPI1 on MIO10..15  PmodCAN #1 on Pmod JF; its INT is MIO0 (GPIO, from the preset)
#   TTC1               FreeRTOS profiling timer
#   SWDT               system watchdog, resets the whole PS (Linux feeds it)
#   FCLK0 100 MHz      the only PL clock
#   M_AXI_GP0          PL peripherals of the PS (B2)
#   IRQ_F2P            PL interrupts; [8]/[9] are the AMP doorbells (ADR-0005)
set_property -dict [list \
    CONFIG.PCW_UART0_PERIPHERAL_ENABLE  {1} \
    CONFIG.PCW_UART0_UART0_IO           {EMIO} \
    CONFIG.PCW_UART_PERIPHERAL_FREQMHZ  {100} \
    CONFIG.PCW_I2C0_PERIPHERAL_ENABLE   {1} \
    CONFIG.PCW_I2C0_I2C0_IO             {EMIO} \
    CONFIG.PCW_SPI1_PERIPHERAL_ENABLE   {1} \
    CONFIG.PCW_SPI1_SPI1_IO             {MIO 10 .. 15} \
    CONFIG.PCW_TTC1_PERIPHERAL_ENABLE   {1} \
    CONFIG.PCW_TTC1_TTC1_IO             {EMIO} \
    CONFIG.PCW_WDT_PERIPHERAL_ENABLE    {1} \
    CONFIG.PCW_WDT_WDT_IO               {EMIO} \
    CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ {100} \
    CONFIG.PCW_USE_M_AXI_GP0            {1} \
    CONFIG.PCW_USE_FABRIC_INTERRUPT     {1} \
    CONFIG.PCW_IRQ_F2P_INTR             {1} \
] $ps7

set clk     [get_bd_pins ps7/FCLK_CLK0]
set ps_rstn [get_bd_pins ps7/FCLK_RESET0_N]

ext ps7/UART_0 uart0
ext ps7/IIC_0  codec_iic

#==============================================================================
# B3  Housekeeping controller (MicroBlaze V)
#==============================================================================
# rv32imc + Zicsr/Zifencei, matching cmake/toolchains/riscv-microblaze-v.cmake.
set hkc_cpu [create_bd_cell -type ip -vlnv xilinx.com:ip:microblaze_riscv:1.0 hkc_cpu]
set_property -dict [list \
    CONFIG.C_USE_MULDIV {2} \
    CONFIG.C_USE_COMPRESSION {1} \
    CONFIG.C_USE_INTERRUPT {1} \
] $hkc_cpu
# 128 KiB LMB BRAM at 0x0: bootloader 0x0000-0x3FFF, application 0x4000-0x1FFFF.
apply_bd_automation -rule xilinx.com:bd_rule:microblaze_riscv -config [list \
    axi_intc {1} axi_periph {Enabled} cache {None} clk {/ps7/FCLK_CLK0 (100 MHz)} \
    debug_module {Debug Enabled} ecc {None} local_mem {128KB} preset {None}] $hkc_cpu

# The automation creates one reset block for FCLK0; it becomes the HKC reset so
# the timebase watchdog can reset the MCU subsystem alone (ICD: hkc_wdt).
set rst_hkc [get_bd_cells -filter {VLNV =~ "*proc_sys_reset*" && NAME != "rst_ps"}]
set_property name rst_hkc $rst_hkc
set_property name hkc_intc [get_bd_cells -filter {VLNV =~ "*axi_intc*"}]
set_property name hkc_axi  [get_bd_cells -filter {VLNV =~ "*axi_interconnect*" || (VLNV =~ "*smartconnect*" && NAME != "ps_axi")}]
set irqs [get_bd_cells -quiet -filter {VLNV =~ "*concat*"}]
if {$irqs eq ""} {
    create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilconcat:1.0 hkc_irqs
    connect_bd_net [get_bd_pins hkc_irqs/dout] [get_bd_pins hkc_intc/intr]
} else {
    set_property name hkc_irqs $irqs
}
if {[llength [get_bd_intf_nets -quiet -of_objects [get_bd_intf_pins hkc_cpu/INTERRUPT]]] == 0} {
    connect_bd_intf_net [get_bd_intf_pins hkc_intc/interrupt] [get_bd_intf_pins hkc_cpu/INTERRUPT]
}

proc hkc_periph {name vlnv props} {
    set cell [create_bd_cell -type ip -vlnv $vlnv $name]
    if {[llength $props]} { set_property -dict $props $cell }
    apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
        -config {Master {/hkc_cpu (Periph)} Clk_master {Auto} Clk_slave {Auto} Clk_xbar {Auto} intc_ip {/hkc_axi} master_apm {0}} \
        [get_bd_intf_pins -of_objects $cell -filter {MODE == Slave && VLNV =~ "*aximm*"}]
    return $cell
}

hkc_periph hkc_gpio  xilinx.com:ip:axi_gpio:2.0 [list \
    CONFIG.C_IS_DUAL {1} \
    CONFIG.C_GPIO_WIDTH {4}  CONFIG.C_ALL_INPUTS {1} \
    CONFIG.C_GPIO2_WIDTH {3} CONFIG.C_ALL_OUTPUTS_2 {1} \
    CONFIG.C_INTERRUPT_PRESENT {1}]
hkc_periph hkc_uart  xilinx.com:ip:axi_uartlite:2.0 [list \
    CONFIG.C_BAUDRATE {115200} CONFIG.C_S_AXI_ACLK_FREQ_HZ {100000000}]
hkc_periph hkc_wdt   xilinx.com:ip:axi_timebase_wdt:3.0 {}
hkc_periph hkc_timer xilinx.com:ip:axi_timer:2.0 {}
# Standard SPI master, 1 slave select, 16-deep FIFO; SCK = 100 MHz / 16 = 6.25 MHz (MCP2515 max 10 MHz).
hkc_periph hkc_spi   xilinx.com:ip:axi_quad_spi:3.2 [list \
    CONFIG.C_SPI_MODE {0} CONFIG.C_NUM_SS_BITS {1} CONFIG.C_FIFO_DEPTH {16} \
    CONFIG.C_SCK_RATIO {16} CONFIG.C_USE_STARTUP {0}]
connect_bd_net $clk [get_bd_pins hkc_spi/ext_spi_clk]
# Continuous sequencer over the channels firmware/hkc/include/hkc/drivers.h reads.
hkc_periph hkc_xadc  xilinx.com:ip:xadc_wiz:3.3 [list \
    CONFIG.INTERFACE_SELECTION {Enable_AXI} \
    CONFIG.XADC_STARUP_SELECTION {channel_sequencer} \
    CONFIG.SEQUENCER_MODE {Continuous} \
    CONFIG.CHANNEL_ENABLE_TEMPERATURE {true} \
    CONFIG.CHANNEL_ENABLE_VCCINT {true} \
    CONFIG.CHANNEL_ENABLE_VCCAUX {true} \
    CONFIG.CHANNEL_ENABLE_VBRAM {true} \
    CONFIG.CHANNEL_ENABLE_VCCPINT {true} \
    CONFIG.CHANNEL_ENABLE_VCCPAUX {true} \
    CONFIG.CHANNEL_ENABLE_VCCDDRO {true} \
    CONFIG.CHANNEL_ENABLE_VP_VN {false}]

# Watchdog reset -> MCU subsystem only.
set_property CONFIG.C_AUX_RESET_HIGH {1} $rst_hkc
connect_bd_net [get_bd_pins hkc_wdt/wdt_reset] [get_bd_pins rst_hkc/aux_reset_in]

ext hkc_gpio/GPIO  sws
ext hkc_gpio/GPIO2 rgb_led6
ext hkc_uart/UART  hkc_uart
ext hkc_spi/SPI_0  can2_spi
set can2_int_n [create_bd_port -dir I -type intr can2_int_n]
set_property CONFIG.SENSITIVITY LEVEL_LOW $can2_int_n

#==============================================================================
# B2  AXI GPIO for Core 1 (axi_gpio_0: LD0-3 on ch1, BTN0-3 on ch2)
#==============================================================================
# Built after B3 on purpose: the MicroBlaze automation would otherwise reuse
# this reset block, and the HKC watchdog would reset the PS-side GPIO too.
set rst_ps [create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst_ps]
connect_bd_net $clk     [get_bd_pins rst_ps/slowest_sync_clk]
connect_bd_net $ps_rstn [get_bd_pins rst_ps/ext_reset_in]

set ps_axi [create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 ps_axi]
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {1}] $ps_axi
connect_bd_intf_net [get_bd_intf_pins ps7/M_AXI_GP0] [get_bd_intf_pins ps_axi/S00_AXI]
connect_bd_net $clk [get_bd_pins ps7/M_AXI_GP0_ACLK] [get_bd_pins ps_axi/aclk]
connect_bd_net [get_bd_pins rst_ps/interconnect_aresetn] [get_bd_pins ps_axi/aresetn]

set gpio0 [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio:2.0 axi_gpio_0]
set_property -dict [list \
    CONFIG.C_IS_DUAL {1} \
    CONFIG.C_GPIO_WIDTH {4}  CONFIG.C_ALL_OUTPUTS {1} \
    CONFIG.C_GPIO2_WIDTH {4} CONFIG.C_ALL_INPUTS_2 {1} \
    CONFIG.C_INTERRUPT_PRESENT {1} \
] $gpio0
connect_bd_intf_net [get_bd_intf_pins ps_axi/M00_AXI] [get_bd_intf_pins axi_gpio_0/S_AXI]
connect_bd_net $clk [get_bd_pins axi_gpio_0/s_axi_aclk]
connect_bd_net [get_bd_pins rst_ps/peripheral_aresetn] [get_bd_pins axi_gpio_0/s_axi_aresetn]
ext axi_gpio_0/GPIO  leds
ext axi_gpio_0/GPIO2 btns

#==============================================================================
# B6  Interrupts, address map
#==============================================================================
# HKC interrupt controller inputs (ICD hkc space): 0 wdt, 1 timer, 2 CAN #2 INT,
# 3 spi, 4 uart, 5 xadc, 6 gpio.
set hkc_irqs [get_bd_cells hkc_irqs]
set_property CONFIG.NUM_PORTS {7} $hkc_irqs
foreach {idx pin} {
    0 hkc_wdt/wdt_interrupt
    1 hkc_timer/interrupt
    3 hkc_spi/ip2intc_irpt
    4 hkc_uart/interrupt
    5 hkc_xadc/ip2intc_irpt
    6 hkc_gpio/ip2intc_irpt
} {
    connect_bd_net [get_bd_pins $pin] [get_bd_pins hkc_irqs/In$idx]
}
connect_bd_net $can2_int_n [get_bd_pins hkc_irqs/In2]

# PS IRQ_F2P[9:0] -> GIC IDs 61..68, 84, 85. Only [3] (ID 64, axi_gpio_0) is
# driven in hw-v1; [8]/[9] stay 0 because the cores pend them in software.
set f2p [create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilconcat:1.0 ps_irqs]
set_property CONFIG.NUM_PORTS {10} $f2p
set zero [create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilconstant:1.0 irq_zero]
set_property CONFIG.CONST_VAL {0} $zero
for {set i 0} {$i < 10} {incr i} {
    if {$i == 3} {
        connect_bd_net [get_bd_pins axi_gpio_0/ip2intc_irpt] [get_bd_pins ps_irqs/In3]
    } else {
        connect_bd_net [get_bd_pins irq_zero/dout] [get_bd_pins ps_irqs/In$i]
    }
}
connect_bd_net [get_bd_pins ps_irqs/dout] [get_bd_pins ps7/IRQ_F2P]

# Address map: exactly the ICD values.
delete_bd_objs [get_bd_addr_segs -excluded]
foreach seg [get_bd_addr_segs -of_objects [get_bd_addr_spaces]] { delete_bd_objs $seg }
proc place {space slave offset range} {
    assign_bd_address -target_address_space [get_bd_addr_spaces $space] \
        -offset $offset -range $range [get_bd_addr_segs $slave]
}
place ps7/Data            axi_gpio_0/S_AXI/Reg                  0x41200000 64K
place hkc_cpu/Data        hkc_gpio/S_AXI/Reg                    0x40000000 64K
place hkc_cpu/Data        hkc_uart/S_AXI/Reg                    0x40600000 64K
place hkc_cpu/Data        hkc_intc/S_AXI/Reg                    0x41200000 64K
place hkc_cpu/Data        hkc_wdt/S_AXI/Reg                     0x41A00000 64K
place hkc_cpu/Data        hkc_timer/S_AXI/Reg                   0x41C00000 64K
place hkc_cpu/Data        hkc_spi/AXI_LITE/Reg                  0x44A00000 64K
place hkc_cpu/Data        hkc_xadc/s_axi_lite/Reg               0x44A10000 64K
# LMB BRAM, data and instruction side; the range sets the BRAM size.
place hkc_cpu/Data        hkc_cpu_local_memory/dlmb_bram_if_cntlr/SLMB/Mem 0x00000000 128K
place hkc_cpu/Instruction hkc_cpu_local_memory/ilmb_bram_if_cntlr/SLMB/Mem 0x00000000 128K
foreach space [get_bd_addr_spaces] {
    foreach seg [get_bd_addr_segs -quiet -of_objects $space] {
        puts "ADDR $seg [get_property OFFSET $seg] [get_property RANGE $seg]"
    }
}

regenerate_bd_layout
validate_bd_design
save_bd_design

#==============================================================================
# Wrapper, constraints, bootloader in BRAM
#==============================================================================
set bd_file [get_files ${bd_name}.bd]
generate_target all $bd_file
add_files -norecurse [make_wrapper -files $bd_file -top]
set_property top ${bd_name}_wrapper [current_fileset]

add_files -fileset constrs_1 -norecurse [file join $hw_dir xdc satlink_zybo_z7_20.xdc]

# Copied into the project so the project folder is self-contained.
import_files -fileset sources_1 -norecurse $hkc_elf
set elf [get_files [file tail $hkc_elf]]
set_property SCOPED_TO_REF   $bd_name [get_files -all -of_objects [get_fileset sources_1] $elf]
set_property SCOPED_TO_CELLS {hkc_cpu} [get_files -all -of_objects [get_fileset sources_1] $elf]

update_compile_order -fileset sources_1
puts "INFO: project ready: [file join $proj_dir $proj_name.xpr]"
