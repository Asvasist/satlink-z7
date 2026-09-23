# Kernel configuration for SatLink-Z7, kept as reviewable fragments.
# @implements SRS-BSP-002
FILESEXTRAPATHS:prepend := "${THISDIR}/files:${THISDIR}/../../../../linux/dts:"

SRC_URI:append = " file://satlink.cfg"
SRC_URI:append = "${@' file://preempt-rt.cfg' if d.getVar('SATLINK_RT') == '1' else ''}"

# Board device tree with the PL nodes (SATLINK_PL_DT=1). The sources live in linux/dts of this
# repository; they are copied next to the mainline Zybo Z7 tree and added to its Makefile so the
# kernel builds them like any other device tree.
SRC_URI:append = "${@' file://zynq-zybo-z7-satlink.dts file://zybo-z7-satlink-ps.dtsi file://zybo-z7-satlink-pl.dtsi' if d.getVar('SATLINK_PL_DT') == '1' else ''}"

do_configure:prepend() {
    if [ "${SATLINK_PL_DT}" = "1" ]; then
        dtsdir="${S}/arch/arm/boot/dts/xilinx"
        install -m 0644 "${WORKDIR}/zynq-zybo-z7-satlink.dts" "${WORKDIR}/zybo-z7-satlink-ps.dtsi" \
            "${WORKDIR}/zybo-z7-satlink-pl.dtsi" "$dtsdir/"
        grep -q zynq-zybo-z7-satlink "$dtsdir/Makefile" || \
            echo 'dtb-$(CONFIG_ARCH_ZYNQ) += zynq-zybo-z7-satlink.dtb' >> "$dtsdir/Makefile"
    fi
}
