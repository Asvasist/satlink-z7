SUMMARY = "SatLink-Z7 Core 1 modem firmware (FreeRTOS)"
DESCRIPTION = "Bare-metal FreeRTOS image for Cortex-A9 Core 1, built with the rtos-a9 CMake preset and installed as /lib/firmware/satlink-rtos.elf for the satlink_amp driver."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
# @implements SRS-AMP-001

# arm-none-eabi GCC from meta-arm-toolchain; FreeRTOS fetched by BitBake (do_compile has no
# network) and handed to CMake's FetchContent.
DEPENDS = "gcc-arm-none-eabi-native cmake-native ninja-native python3-native"
SRC_URI = "https://github.com/FreeRTOS/FreeRTOS-Kernel/archive/refs/tags/V11.1.0.tar.gz;downloadfilename=FreeRTOS-Kernel-V11.1.0.tar.gz;subdir=freertos"
SRC_URI[sha256sum] = "SET-ON-FIRST-BUILD"

inherit externalsrc allarch

SATLINK_REPO_ROOT := "${@os.path.normpath(d.getVar('THISDIR') + '/../../../..')}"
EXTERNALSRC = "${SATLINK_REPO_ROOT}"
EXTERNALSRC_BUILD = "${WORKDIR}/build"

# The image is for Core 1, not for the Linux user space of this machine.
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_SYSROOT_STRIP = "1"
INSANE_SKIP:${PN} += "arch"

do_configure[noexec] = "1"

do_compile() {
    export PATH="${STAGING_BINDIR_NATIVE}/gcc-arm-none-eabi/bin:${PATH}"
    cmake -S ${EXTERNALSRC} -B ${B} -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=${EXTERNALSRC}/cmake/toolchains/arm-none-eabi-cortex-a9.cmake \
        -DCMAKE_BUILD_TYPE=MinSizeRel -DSATLINK_WARNINGS_AS_ERRORS=OFF \
        -DFETCHCONTENT_SOURCE_DIR_FREERTOS_KERNEL=${WORKDIR}/freertos/FreeRTOS-Kernel-11.1.0
    cmake --build ${B} --target satlink_rtos
}

do_install() {
    install -d ${D}${nonarch_base_libdir}/firmware
    install -m 0644 ${B}/firmware/rtos/satlink_rtos.elf ${D}${nonarch_base_libdir}/firmware/satlink-rtos.elf
}

FILES:${PN} = "${nonarch_base_libdir}/firmware/satlink-rtos.elf"
