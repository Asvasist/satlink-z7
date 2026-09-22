# Kernel configuration for SatLink-Z7, kept as reviewable fragments.
# @implements SRS-BSP-002
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append = " file://satlink.cfg"
SRC_URI:append = "${@' file://preempt-rt.cfg' if d.getVar('SATLINK_RT') == '1' else ''}"
