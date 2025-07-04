# Package information for DKMS
PACKAGE_NAME := alsa-hdspe
PACKAGE_VERSION := 0.0
DKMS_PATH := /usr/src/$(PACKAGE_NAME)-$(PACKAGE_VERSION)

# The runtime of DKMS has this environment variable to build for several versions of Linux kernel.
ifndef KERNELRELEASE
# Normal build
KERNELRELEASE := $(shell uname -r)
KDIR    ?= /lib/modules/${KERNELRELEASE}/build
PWD     := $(shell pwd)
EXTRA_CFLAGS += -DDEBUG -DCONFIG_SND_DEBUG

# Force to build the module as loadable kernel module.
# Keep in mind that this configuration sound be in 'sound/pci/Kconfig' when upstreaming.
export CONFIG_SND_HDSPE=m

default: depend
	$(MAKE) W=1 -C $(KDIR) M=$(PWD) modules

dkms.conf: dkms.conf.in
	sed -e "s/@PACKAGE_NAME@/$(PACKAGE_NAME)/g" \
	    -e "s/@PACKAGE_VERSION@/$(PACKAGE_VERSION)/g" \
	    $< > $@

clean:
	$(MAKE) W=1 -C $(KDIR) M=$(PWD) clean
	-rm -f *~ dkms.conf $(PACKAGE_NAME)-$(PACKAGE_VERSION)
	-touch deps

insert: default
	-rmmod snd-hdspm
	insmod sound/pci/hdsp/hdspe/snd-hdspe.ko

remove:
	rmmod snd-hdspe

install: default
	-rmmod snd-hdspm
	-ln -s $(pwd) /usr/src/$(PACKAGE_NAME)-$(PACKAGE_VERSION)
	dkms install $(PACKAGE_NAME)/$(PACKAGE_VERSION)

uninstall:
	dkms remove $(PACKAGE_NAME)/$(PACKAGE_VERSION) --all

list-controls:
	-rm asound.state
	alsactl -f asound.state store

show-controls: list-controls
	less asound.state

enable-debug-log:
	echo 8 > /proc/sys/kernel/printk

depend: dkms.conf
	gcc -MM sound/pci/hdsp/hdspe/hdspe*.c > deps
else
# Kernel build
obj-$(CONFIG_SND_HDSPE) += sound/pci/hdsp/hdspe/
endif
