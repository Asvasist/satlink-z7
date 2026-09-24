# SatLink-Z7 U-Boot boot script with A/B slots and boot counting (SATLINK_AB=1).
#
# Slot state lives in the U-Boot environment, stored as uboot.env on the FAT boot partition:
#
#   boot_slot           a or b: the slot to boot
#   prev_slot           the slot to fall back to
#   bootcount           boot attempts of a slot that is on trial
#   bootlimit           attempts a trial slot gets before it is abandoned (1..9: setexpr prints hex)
#   upgrade_available   1 while boot_slot is on trial, 0 once it has been confirmed
#
# The decision below is, step for step, the reference model libs/boot/src/ab_slot.c
# (satlink_ab_select), and tools/boot/tests runs this very script against the same scenarios.
# Linux confirms a healthy slot and starts updates with satlink-bootctl.
#
# Partition layout (wic/satlink-sd-ab.wks): 1 boot (FAT), 2 root a, 3 root b, 4 data.
# The kernel and device tree of each slot are files on the boot partition: zImage.a, zImage.b,
# system-a.dtb, system-b.dtb.
#
# @implements SRS-BOOT-003

# First boot after flashing: create the state.
if test -z "${boot_slot}"; then
    setenv boot_slot a
    setenv prev_slot a
    setenv bootcount 0
    setenv upgrade_available 0
    saveenv
fi
if test -z "${bootlimit}"; then
    setenv bootlimit 3
    saveenv
fi

# A slot on trial gets bootlimit attempts; after that go back to the previous slot.
if test "${upgrade_available}" = "1"; then
    if test ${bootcount} -ge ${bootlimit}; then
        echo "slot ${boot_slot} was not confirmed after ${bootlimit} attempts, falling back to ${prev_slot}"
        setenv boot_slot ${prev_slot}
        setenv upgrade_available 0
        setenv bootcount 0
    else
        setexpr bootcount ${bootcount} + 1
    fi
    saveenv
fi

if test "${boot_slot}" = "b"; then
    setenv rootpart 3
else
    setenv rootpart 2
fi

echo "booting slot ${boot_slot} (attempt ${bootcount}, trial ${upgrade_available})"
setenv bootargs "console=ttyPS0,115200 earlycon root=/dev/mmcblk0p${rootpart} rw rootwait satlink.slot=${boot_slot}"
fatload mmc 0:1 ${kernel_addr_r} zImage.${boot_slot}
fatload mmc 0:1 ${fdt_addr_r} system-${boot_slot}.dtb
bootz ${kernel_addr_r} - ${fdt_addr_r}
