# U-Boot configuration for the Zybo Z7-20.
# @implements SRS-BOOT-001
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " file://satlink-uboot.cfg"
