SUMMARY = "SatLink-Z7 AMP driver"
DESCRIPTION = "Runs the FreeRTOS modem firmware on Cortex-A9 Core 1 and exposes the shared-memory message rings as /dev/satlink-amp. Built from linux/drivers/satlink_amp; also compiles libs/amp/src/ipc_ring.c."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"
# @implements SRS-AMP-001

inherit module externalsrc

SATLINK_REPO_ROOT := "${@os.path.normpath(d.getVar('THISDIR') + '/../../../..')}"
EXTERNALSRC = "${SATLINK_REPO_ROOT}/linux/drivers/satlink_amp"
EXTERNALSRC_BUILD = "${EXTERNALSRC}"

KERNEL_MODULE_AUTOLOAD += "satlink_amp"
RDEPENDS:${PN} += "satlink-rtos-firmware"
