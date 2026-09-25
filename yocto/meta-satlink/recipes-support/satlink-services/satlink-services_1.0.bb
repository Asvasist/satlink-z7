SUMMARY = "SatLink-Z7 system services"
DESCRIPTION = "A/B boot confirmation and update script, CAN bus bring-up, housekeeping controller firmware loader, payload manager and its IP endpoints."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
# @implements SRS-BOOT-002
# @implements SRS-HKC-006
# @implements SRS-PLM-003

SRC_URI = " \
    file://fw_env.config \
    file://satlink-boot-ok.service \
    file://satlink-update.sh \
    file://satlink-can0.service \
    file://satlink-hkc-loader.service \
    file://satlink-payloadd.service \
    file://satlink-net-setup.sh \
"
S = "${WORKDIR}"

inherit systemd allarch

SYSTEMD_SERVICE:${PN} = "satlink-boot-ok.service satlink-can0.service satlink-hkc-loader.service \
                          satlink-payloadd.service"

# HKC application image (hkc_app.slhk from the hkc-riscv CMake preset). Point this at a built
# image to include it; without it the loader service is skipped (ConditionPathExists).
SATLINK_HKC_IMAGE ?= ""

do_install() {
    install -d ${D}${sysconfdir} ${D}${bindir} ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/fw_env.config ${D}${sysconfdir}/fw_env.config
    install -m 0755 ${S}/satlink-update.sh ${D}${bindir}/satlink-update
    install -m 0755 ${S}/satlink-net-setup.sh ${D}${bindir}/satlink-net-setup
    install -m 0644 ${S}/satlink-boot-ok.service ${S}/satlink-can0.service \
        ${S}/satlink-hkc-loader.service ${S}/satlink-payloadd.service \
        ${D}${systemd_system_unitdir}/
    if [ -n "${SATLINK_HKC_IMAGE}" ]; then
        install -d ${D}${nonarch_base_libdir}/firmware/satlink
        install -m 0644 ${SATLINK_HKC_IMAGE} ${D}${nonarch_base_libdir}/firmware/satlink/hkc_app.slhk
    fi
}

FILES:${PN} += "${nonarch_base_libdir}/firmware/satlink"
RDEPENDS:${PN} = "satlink-tools libubootenv-bin iproute2 iproute2-ip"
