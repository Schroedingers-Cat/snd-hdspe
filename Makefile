# Module metadata
PACKAGE_NAME := snd-hdspe
PACKAGE_VERSION := 1.0.1

# Direct module definition
obj-m += $(PACKAGE_NAME).o

# List all object files explicitly for better DKMS compatibility
$(PACKAGE_NAME)-y := \
	sound/pci/hdsp/hdspe/hdspe_core.o \
	sound/pci/hdsp/hdspe/hdspe_common.o \
	sound/pci/hdsp/hdspe/hdspe_control.o \
	sound/pci/hdsp/hdspe/hdspe_hwdep.o \
	sound/pci/hdsp/hdspe/hdspe_mixer.o \
	sound/pci/hdsp/hdspe/hdspe_pcm.o \
	sound/pci/hdsp/hdspe/hdspe_proc.o \
	sound/pci/hdsp/hdspe/hdspe_madi.o \
	sound/pci/hdsp/hdspe/hdspe_aes.o \
	sound/pci/hdsp/hdspe/hdspe_raio.o \
	sound/pci/hdsp/hdspe/hdspe_midi.o \
	sound/pci/hdsp/hdspe/hdspe_tco.o \
	sound/pci/hdsp/hdspe/hdspe_ltc_math.o

# Add include path for header files
ccflags-y += -I$(src)/sound/pci/hdsp/hdspe

# Set default kernel directory and path variables.
KERNELRELEASE ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNELRELEASE)/build
PWD := $(shell pwd)
EXTRA_CFLAGS += -DDEBUG -DCONFIG_SND_DEBUG

# --- Standard Build Targets ---
# These are the only targets needed for DKMS to work.
# They are also used for manual compilation.

all: modules

modules: dkms.conf
	$(MAKE) W=1 -C $(KDIR) M=$(PWD) modules

dkms.conf: dkms.conf.in
	@echo "Generating dkms.conf from template..."
	@sed -e "s/@PACKAGE_NAME@/$(PACKAGE_NAME)/g" \
	    -e "s/@PACKAGE_VERSION@/$(PACKAGE_VERSION)/g" \
	    $< > $@

clean:
	$(MAKE) W=1 -C $(KDIR) M=$(PWD) clean
	@rm -f *~ dkms.conf $(PACKAGE_NAME)-$(PACKAGE_VERSION)

insert: all remove-mainlined
	insmod $(PACKAGE_NAME).ko

remove:
	rmmod $(PACKAGE_NAME)

remove-mainlined:
	-rmmod snd-hdspm

# --- DKMS Convenience Targets for Manual Installation ---
# These targets are helpful for developers.
DKMS_SRC_PATH := /usr/src/$(PACKAGE_NAME)-$(PACKAGE_VERSION)

install: all remove-mainlined
	@echo "Installing module into DKMS tree for manual use..."
	-sudo rm -rf $(DKMS_SRC_PATH)
	sudo mkdir -p $(DKMS_SRC_PATH)
	sudo cp -r Makefile dkms.conf* sound $(DKMS_SRC_PATH)/
	sudo dkms add -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION)
	sudo dkms build -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION)
	sudo dkms install -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION)

uninstall:
	@echo "Removing module from DKMS tree..."
	sudo dkms remove -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION) --all
	sudo rm -rf $(DKMS_SRC_PATH)

list-controls:
	-rm asound.state
	alsactl -f asound.state store

show-controls: list-controls
	less asound.state

enable-debug-log:
	echo 8 > /proc/sys/kernel/printk
