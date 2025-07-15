obj-m += sound/pci/hdsp/

# The runtime of DKMS has this environment variable to build for several versions of Linux kernel.
ifndef KERNELRELEASE
KERNELRELEASE := $(shell uname -r)
endif

KDIR    ?= /lib/modules/${KERNELRELEASE}/build
PWD     := $(shell pwd)

# Debug and warning flags
DEBUG ?= 0
CONFIG_SND_DEBUG ?= 0

EXTRA_CFLAGS += $(if $(filter 1,$(DEBUG)),-DDEBUG,)
EXTRA_CFLAGS += $(if $(filter 1,$(CONFIG_SND_DEBUG)),-DCONFIG_SND_DEBUG,)

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

debug:
	$(MAKE) CONFIG_SND_DEBUG=1 DEBUG=1

depend:
	gcc -MM sound/pci/hdsp/hdspe/hdspe*.c > deps
