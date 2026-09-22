SUMMARY = "U-Boot boot script for SatLink-Z7"
DESCRIPTION = "Loads the kernel and device tree from the SD boot partition and boots Linux."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
# @implements SRS-BOOT-001

DEPENDS = "u-boot-mkimage-native"

SRC_URI = "file://boot.cmd"
S = "${WORKDIR}"

inherit deploy nopackages

do_compile() {
    mkimage -A arm -O linux -T script -C none -n "SatLink-Z7 boot" -d ${S}/boot.cmd ${B}/boot.scr
}

do_deploy() {
    install -m 0644 ${B}/boot.scr ${DEPLOYDIR}/boot.scr
}
addtask deploy after do_compile before do_build
