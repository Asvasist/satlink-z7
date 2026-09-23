SUMMARY = "SatLink-Z7 user-space tools"
DESCRIPTION = "satlink-diag, the diagnostic command line for the frame accelerator, spectrum tap and audio codec."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
# @implements SRS-DIAG-001

inherit cmake externalsrc

# The source is this repository (four levels up from the recipe directory), built with the
# "linux" target. Warnings are not errors here: the compiler in the SDK differs from the one CI
# pins, and CI is what enforces a warning-free build.
SATLINK_REPO_ROOT := "${@os.path.normpath(d.getVar('THISDIR') + '/../../../..')}"
EXTERNALSRC = "${SATLINK_REPO_ROOT}"
EXTERNALSRC_BUILD = "${WORKDIR}/build"

EXTRA_OECMAKE = "-DSATLINK_TARGET=linux -DSATLINK_BUILD_TESTS=OFF -DSATLINK_WARNINGS_AS_ERRORS=OFF"
