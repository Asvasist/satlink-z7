# SPDX-License-Identifier: GPL-2.0
# Out-of-tree build. KERNEL_SRC must point at a configured kernel build tree, for example the
# one from the Yocto SDK, together with the SDK's cross-compiler environment:
#
#   make -C linux/drivers/<driver> KERNEL_SRC=/path/to/kernel-build ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-

ifeq ($(KERNEL_SRC),)
$(error KERNEL_SRC is not set: point it at a configured kernel build tree)
endif

PWD := $(shell pwd)

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean

.PHONY: modules modules_install clean
