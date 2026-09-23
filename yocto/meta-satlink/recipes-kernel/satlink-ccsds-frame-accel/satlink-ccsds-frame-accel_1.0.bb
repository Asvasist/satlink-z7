SUMMARY = "SatLink-Z7 CCSDS frame accelerator platform driver"
DESCRIPTION = "Out-of-tree driver for the ccsds_frame_accel PL block, built from the monorepo \
under linux/drivers/ccsds_frame_accel instead of a separate fetch."
LICENSE = "GPL-2.0-only"
# md5 below is unverified (written without a Yocto build to check it against); bitbake will
# print the correct value on first parse if it's wrong - update it then.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"
# @implements SRS-DRV-001

inherit module externalsrc

# This recipe builds source that lives in the monorepo, not a separate fetched tarball/git repo,
# so it uses externalsrc instead of SRC_URI + S: EXTERNALSRC points straight at the driver
# directory two-and-out from meta-satlink (meta-satlink/recipes-kernel/<this>/.. -> yocto -> repo
# root), which keeps the driver source as the single copy instead of duplicating it under
# recipe-owned files://.
SATLINK_REPO_ROOT := "${@os.path.normpath(d.getVar('THISDIR') + '/../../../..')}"
EXTERNALSRC = "${SATLINK_REPO_ROOT}/linux/drivers/ccsds_frame_accel"
EXTERNALSRC_BUILD = "${EXTERNALSRC}"

KERNEL_MODULE_AUTOLOAD += "ccsds_frame_accel_drv"
