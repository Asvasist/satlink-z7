SUMMARY = "SatLink-Z7 CCSDS frame accelerator platform driver"
DESCRIPTION = "Out-of-tree driver for the ccsds_frame_accel PL block, built from linux/drivers/ccsds_frame_accel in this repository."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"
# @implements SRS-DRV-001

inherit module externalsrc

# The source is in this repository, not fetched, so externalsrc points at it: four levels up from
# the recipe directory is the repository root.
SATLINK_REPO_ROOT := "${@os.path.normpath(d.getVar('THISDIR') + '/../../../..')}"
EXTERNALSRC = "${SATLINK_REPO_ROOT}/linux/drivers/ccsds_frame_accel"
EXTERNALSRC_BUILD = "${EXTERNALSRC}"

KERNEL_MODULE_AUTOLOAD += "ccsds_frame_accel_drv"
