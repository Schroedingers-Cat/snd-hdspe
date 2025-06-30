obj-m += sound/pci/hdsp/

# The runtime of DKMS has this environment variable to build for several versions of Linux kernel.
ifndef KERNELRELEASE
KERNELRELEASE := $(shell uname -r)
endif

KDIR    ?= /lib/modules/${KERNELRELEASE}/build
PWD     := $(shell pwd)

# Debug options

# TODO: It seems DEBUG is not used. Make sure it's not used implicitly somehow.
EXTRA_CFLAGS += -DDEBUG

# Controls debug information via /proc interface
CONFIG_SND_DEBUG ?= 0
ifeq ($(CONFIG_SND_DEBUG),1)
  EXTRA_CFLAGS += -DCONFIG_SND_DEBUG
endif

# Force to build the module as loadable kernel module.
# Keep in mind that this configuration sound be in 'sound/pci/Kconfig' when upstreaming.
export CONFIG_SND_HDSPE=m

default: depend
	$(MAKE) W=1 -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) W=1 -C $(KDIR) M=$(PWD) clean
	-rm *~
	-touch deps

insert: default
	-rmmod snd-hdspm
	insmod sound/pci/hdsp/hdspe/snd-hdspe.ko

remove:
	rmmod snd-hdspe

install: default
	-rmmod snd-hdspm
	-ln -s $(pwd) /usr/src/alsa-hdspe-0.0
	dkms install alsa-hdspe/0.0

uninstall:
	dkms remove alsa-hdspe/0.0 --all

list-controls:
	-rm asound.state
	alsactl -f asound.state store

show-controls: list-controls
	less asound.state

# TODO: It seems several errors are logged using dev_info, which is not logged by default.
# TODO: This makefile option changes kernel settings. We should not do that and instead put that info into the README.md
enable-debug-log:
	echo 8 > /proc/sys/kernel/printk

depend:
	gcc -MM sound/pci/hdsp/hdspe/hdspe*.c > deps
