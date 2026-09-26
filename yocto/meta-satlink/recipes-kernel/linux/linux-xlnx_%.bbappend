# Kernel configuration for SatLink-Z7, kept as reviewable fragments.
# @implements SRS-BSP-002
FILESEXTRAPATHS:prepend := "${THISDIR}/files:${THISDIR}/../../../../linux/dts:"

SRC_URI:append = " file://satlink.cfg"
SRC_URI:append = "${@' file://preempt-rt.cfg' if d.getVar('SATLINK_RT') == '1' else ''}"

# Board device trees from linux/dts of this repository: zynq-zybo-z7-satlink-amp (PS only, the
# default) and zynq-zybo-z7-satlink (with the PL nodes, SATLINK_PL_DT=1). They are copied next to
# the mainline Zybo Z7 tree and added to its Makefile, so the kernel builds them like any other.
SRC_URI:append = " file://zynq-zybo-z7-satlink.dts file://zynq-zybo-z7-satlink-amp.dts \
                   file://zybo-z7-satlink-ps.dtsi file://zybo-z7-satlink-pl.dtsi \
                   file://zybo-z7-satlink-amp.dtsi"

do_configure:prepend() {
    dtsdir="${S}/arch/arm/boot/dts/xilinx"
    install -m 0644 "${WORKDIR}/zynq-zybo-z7-satlink.dts" "${WORKDIR}/zynq-zybo-z7-satlink-amp.dts" \
        "${WORKDIR}/zybo-z7-satlink-ps.dtsi" "${WORKDIR}/zybo-z7-satlink-pl.dtsi" \
        "${WORKDIR}/zybo-z7-satlink-amp.dtsi" "$dtsdir/"
    for dtb in zynq-zybo-z7-satlink zynq-zybo-z7-satlink-amp; do
        grep -q "$dtb.dtb" "$dtsdir/Makefile" || \
            echo "dtb-\$(CONFIG_ARCH_ZYNQ) += $dtb.dtb" >> "$dtsdir/Makefile"
    done
}
