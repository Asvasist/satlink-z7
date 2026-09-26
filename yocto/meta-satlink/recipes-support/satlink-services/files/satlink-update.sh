#!/bin/sh
# satlink-update: install a root file system image into the slot that is not running and put
# it on trial for the next boot. U-Boot verifies the signed kernel in it and rolls back
# automatically if the new slot does not confirm itself.
#
#   satlink-update satlink-image-zybo-z7-20.rootfs.ext4[.gz] [--reboot]
#
# @implements SRS-BOOT-002
set -eu

image="${1:?usage: satlink-update IMAGE.ext4[.gz] [--reboot]}"
target="$(satlink-bootctl inactive)"

case "$(cat /proc/cmdline)" in
    *satlink.slot=golden*) echo "satlink-update: running the golden image; refusing" >&2; exit 1 ;;
esac
if grep -q "^${target} " /proc/mounts; then
    echo "satlink-update: ${target} is mounted" >&2
    exit 1
fi

echo "satlink-update: writing ${image} to ${target}"
case "${image}" in
    *.gz) gzip -dc "${image}" | dd of="${target}" bs=4M conv=fsync ;;
    *)    dd if="${image}" of="${target}" bs=4M conv=fsync ;;
esac

# The image carries the kernel; check it is there before switching.
mnt="$(mktemp -d)"
mount -o ro "${target}" "${mnt}"
ok=0
[ -s "${mnt}/boot/fitImage" ] && ok=1
umount "${mnt}"
rmdir "${mnt}"
if [ "${ok}" != 1 ]; then
    echo "satlink-update: no /boot/fitImage in the new root file system; slot not switched" >&2
    exit 1
fi

satlink-bootctl prepare-update
if [ "${2:-}" = "--reboot" ]; then
    reboot
fi
