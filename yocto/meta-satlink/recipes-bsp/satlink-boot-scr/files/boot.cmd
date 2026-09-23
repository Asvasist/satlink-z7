# SatLink-Z7 U-Boot boot script, compiled to boot.scr by satlink-boot-scr.
# @implements SRS-BOOT-001
setenv bootargs "console=ttyPS0,115200 earlycon root=/dev/mmcblk0p2 rw rootwait"
fatload mmc 0:1 ${kernel_addr_r} zImage
fatload mmc 0:1 ${fdt_addr_r} system.dtb
bootz ${kernel_addr_r} - ${fdt_addr_r}
