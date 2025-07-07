obj-m += sound/pci/hdsp/

# The runtime of DKMS has this environment variable to build for several versions of Linux kernel.
ifndef KERNELRELEASE
KERNELRELEASE := $(shell uname -r)
endif

KDIR    ?= /lib/modules/${KERNELRELEASE}/build
PWD     := $(shell pwd)
EXTRA_CFLAGS += -DDEBUG -DCONFIG_SND_DEBUG

# Force to build the module as loadable kernel module.
# Keep in mind that this configuration sound be in 'sound/pci/Kconfig' when upstreaming.
export CONFIG_SND_HDSPE=m

default: depend
	$(MAKE) W=1 -C $(KDIR) M=$(PWD) modules

# Generate compilation database for IDE integration
compiledb:
	@which bear > /dev/null || (echo "Error: 'bear' tool not found. Please install it for IDE integration." && exit 1)
	@if [ ! -f compile_commands.json ] || [ -n "$$(find . -newer compile_commands.json -name "*.c" -o -name "*.h")" ]; then \
		echo "Generating compilation database for IDE integration..."; \
		if [ -f compile_commands.json ]; then rm compile_commands.json; fi; \
		bear -- $(MAKE) W=1 -C $(KDIR) M=$(PWD) modules || true; \
	else \
		echo "Compilation database is up to date"; \
	fi

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

enable-debug-log:
	echo 8 > /proc/sys/kernel/printk

depend:
	gcc -MM sound/pci/hdsp/hdspe/hdspe*.c > deps
