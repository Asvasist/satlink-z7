# U-Boot configuration for the Zybo Z7-20.
# SATLINK_SECURE_BOOT = "1" adds the A/B environment, boot counting and signed-FIT-only boot.
# @implements SRS-BOOT-001
# @implements SRS-BOOT-002
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " file://satlink-uboot.cfg"
SRC_URI:append = "${@' file://satlink-ab.cfg file://satlink-env.txt' if d.getVar('SATLINK_SECURE_BOOT') == '1' else ''}"

# CONFIG_DEFAULT_ENV_FILE is resolved relative to the source tree.
do_configure:prepend() {
    if [ "${SATLINK_SECURE_BOOT}" = "1" ]; then
        install -m 0644 ${WORKDIR}/satlink-env.txt ${S}/satlink-env.txt
    fi
}
