# SPDX-License-Identifier: GPL-2.0
# Out-of-tree build against the Yocto eSDK (see yocto/README.md for `... -c populate_sdk`).
#
#   source /opt/satlink/5.x/environment-setup-cortexa9t2hf-neon-satlink-linux-gnueabi
#   make -C linux/drivers/ccsds_frame_accel
#
# KERNEL_SRC is set by the eSDK environment script; override it to point at a built kernel tree
# for a manual build instead.

KERNEL_SRC ?= $(KERNEL_SRC)
PWD := $(shell pwd)

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean

.PHONY: modules modules_install clean
