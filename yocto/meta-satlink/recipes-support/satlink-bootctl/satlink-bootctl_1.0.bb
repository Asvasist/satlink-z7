SUMMARY = "SatLink-Z7 A/B boot slot control"
DESCRIPTION = "satlink-bootctl confirms or starts an A/B update through the U-Boot environment, and an init script confirms a healthy slot after boot."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
# @implements SRS-BOOT-003

SRC_URI = " \
    file://satlink-bootctl \
    file://satlink-boot-confirm \
    file://fw_env.config \
"
S = "${WORKDIR}"

RDEPENDS:${PN} = "libubootenv-bin"

inherit update-rc.d

INITSCRIPT_NAME = "satlink-boot-confirm"
INITSCRIPT_PARAMS = "defaults 99"

do_install() {
    install -d ${D}${sbindir} ${D}${sysconfdir}/init.d
    install -m 0755 ${S}/satlink-bootctl ${D}${sbindir}/satlink-bootctl
    install -m 0755 ${S}/satlink-boot-confirm ${D}${sysconfdir}/init.d/satlink-boot-confirm
    install -m 0644 ${S}/fw_env.config ${D}${sysconfdir}/fw_env.config
}
