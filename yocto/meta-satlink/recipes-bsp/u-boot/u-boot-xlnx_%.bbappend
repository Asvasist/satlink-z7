# U-Boot configuration for the Zybo Z7-20.
# @implements SRS-BOOT-001
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " file://satlink-uboot.cfg"
# The environment moves to the boot partition for the A/B boot flow (SATLINK_AB=1).
SRC_URI:append = "${@' file://satlink-uboot-ab.cfg' if d.getVar('SATLINK_AB') == '1' else ''}"
