SUMMARY = "SatLink-Z7 Core 1 modem firmware (FreeRTOS)"
DESCRIPTION = "Bare-metal FreeRTOS image for Cortex-A9 Core 1, built with the rtos-a9 CMake preset and installed as /lib/firmware/satlink-rtos.elf for the satlink_amp driver."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
# @implements SRS-AMP-001

# arm-none-eabi GCC from meta-arm-toolchain. FreeRTOS is downloaded by CMake's FetchContent in
# do_compile: externalsrc drops remote SRC_URI entries, so BitBake cannot fetch it for us.
DEPENDS = "gcc-arm-none-eabi-native cmake-native ninja-native python3-native"

inherit externalsrc allarch

SATLINK_REPO_ROOT := "${@os.path.normpath(d.getVar('THISDIR') + '/../../../..')}"
EXTERNALSRC = "${SATLINK_REPO_ROOT}"
EXTERNALSRC_BUILD = "${WORKDIR}/build"

# The image is for Core 1, not for the Linux user space of this machine.
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_SYSROOT_STRIP = "1"
INSANE_SKIP:${PN} += "arch"

do_configure[noexec] = "1"
# Fresh CMake build directory every time, so no stale CMakeCache.txt survives a fix.
do_compile[cleandirs] = "${B}"
# FetchContent downloads the FreeRTOS kernel (V11.1.0, pinned in firmware/rtos/CMakeLists.txt).
do_compile[network] = "1"

do_compile() {
    export PATH="${STAGING_BINDIR_NATIVE}/gcc-arm-none-eabi/bin:${PATH}"
    # Bare-metal build: drop the Linux target toolchain that BitBake exports.
    unset CC CXX CPP LD AR AS CFLAGS CXXFLAGS CPPFLAGS LDFLAGS ASFLAGS
    cmake -S ${EXTERNALSRC} -B ${B} -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=${EXTERNALSRC}/cmake/toolchains/arm-none-eabi-cortex-a9.cmake \
        -DCMAKE_BUILD_TYPE=MinSizeRel -DSATLINK_WARNINGS_AS_ERRORS=OFF
    cmake --build ${B} --target satlink_rtos
}

do_install() {
    install -d ${D}${nonarch_base_libdir}/firmware
    install -m 0644 ${B}/firmware/rtos/satlink_rtos.elf ${D}${nonarch_base_libdir}/firmware/satlink-rtos.elf
}

FILES:${PN} = "${nonarch_base_libdir}/firmware/satlink-rtos.elf"
