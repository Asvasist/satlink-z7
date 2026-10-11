# run_selftest.tcl - hw-v1 bring-up over JTAG (board jumper JP5 on JTAG).
#
#   1. programs the bitstream; the HKC bootloader starts from BRAM once ps7_init
#      enables FCLK0 (console on Pmod JE7/JE8, RGB LED6 blue -> red without PmodCAN)
#   2. initialises the PS (DDR, clocks, MIO) from the exported ps7_init.tcl
#   3. loads the FreeRTOS firmware onto Core 1, which finds no Linux and runs its
#      standalone self-test (console on Pmod JE1/JE2, "SELFTEST PASS" after ~15 s)
#
# Usage, from the repository root (Vitis 2025.2):
#   xsdb hw/scripts/run_selftest.tcl
#   xsdb hw/scripts/run_selftest.tcl <bitstream> <ps7_init.tcl> <satlink_rtos.elf>

set repo [file normalize [file join [file dirname [info script]] .. ..]]
set proj [file join $repo hw vivado satlink_hw]
set bit  [expr {[llength $argv] > 0 ? [lindex $argv 0] : [file join $proj satlink_hw.runs impl_1 satlink_bd_wrapper.bit]}]
set init [expr {[llength $argv] > 1 ? [lindex $argv 1] : [file join $proj satlink_hw.gen sources_1 bd satlink_bd ip satlink_bd_ps7_0 ps7_init.tcl]}]
set elf  [expr {[llength $argv] > 2 ? [lindex $argv 2] : [file join $repo build rtos-a9 firmware rtos satlink_rtos.elf]}]
foreach f [list $bit $init $elf] {
    if {![file exists $f]} { error "missing: $f" }
}

connect
targets -set -nocase -filter {name =~ "APU*"}
rst -system
after 1000

puts "programming $bit"
fpga -file $bit

targets -set -nocase -filter {name =~ "*Cortex-A9 MPCore #0"}
source $init
ps7_init
ps7_post_config

# The firmware treats a valid AMP control block as "Linux is up". DDR keeps it
# across a JTAG reset, so clear it to force the standalone self-test.
mwr 0x39000000 0

targets -set -nocase -filter {name =~ "*Cortex-A9 MPCore #1"}
rst -processor
dow $elf
con
puts "Core 1 running $elf - watch the console on Pmod JE1/JE2 (115200 8N1)"
