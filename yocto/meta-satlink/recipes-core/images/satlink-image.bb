SUMMARY = "SatLink-Z7 target image"
DESCRIPTION = "Embedded Linux for Core 0 of the Zynq-7020: bring-up, CAN, I2C, RT and debug tools."
LICENSE = "MIT"
# @implements SRS-BSP-003
# @implements SRS-BOOT-003

inherit core-image

IMAGE_FEATURES += "ssh-server-openssh"

IMAGE_INSTALL += " \
    packagegroup-core-boot \
    kernel-modules \
    satlink-ccsds-frame-accel \
    satlink-spec-tap \
    satlink-amp \
    satlink-tools \
    satlink-services \
    ${@'kernel-image-fitimage' if d.getVar('SATLINK_SECURE_BOOT') == '1' else ''} \
    can-utils \
    i2c-tools \
    iproute2 \
    ethtool \
    rt-tests \
    dtc \
    devmem2 \
    htop \
    iperf3 \
    tcpdump \
"
