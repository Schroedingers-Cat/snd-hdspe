# Module metadata
PACKAGE_NAME := snd-hdspe
PACKAGE_VERSION := 1.0.1
DKMS_PATH := /usr/src/$(PACKAGE_NAME)-$(PACKAGE_VERSION)

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

default: depend
	$(MAKE) W=1 -C $(KDIR) M=$(PWD) modules

depend: dkms.conf

dkms.conf: dkms.conf.in
	sed -e "s/@PACKAGE_NAME@/$(PACKAGE_NAME)/g" \
	    -e "s/@PACKAGE_VERSION@/$(PACKAGE_VERSION)/g" \
	    $< > $@

clean:
	$(MAKE) W=1 -C $(KDIR) M=$(PWD) clean
	-rm -f *~ dkms.conf $(PACKAGE_NAME)-$(PACKAGE_VERSION)

insert: default
	-rmmod snd-hdspm
	insmod $(PACKAGE_NAME).ko

remove:
	rmmod $(PACKAGE_NAME)

install: default
	-rmmod snd-hdspm
	-rm -rf $(DKMS_PATH)
	mkdir -p $(DKMS_PATH)
	cp -r Makefile dkms.conf* sound $(DKMS_PATH)/
	dkms install $(PACKAGE_NAME)/$(PACKAGE_VERSION)

uninstall:
	@versions=$$(dkms status -m $(PACKAGE_NAME) \
	    | grep -E ",\s*$(KERNELRELEASE),.*installed" \
	    | grep -Po "^$(PACKAGE_NAME)/\K[^,]+"); \
	if [ -z "$$versions" ]; then \
	  echo "No $(PACKAGE_NAME) installed for kernel $(KERNELRELEASE)."; \
	else \
	  for ver in $$versions; do \
	    echo "Removing $(PACKAGE_NAME)/$$ver from kernel $(KERNELRELEASE)…"; \
	    sudo dkms remove $(PACKAGE_NAME)/$$ver -k $(KERNELRELEASE); \
	    sudo rm -rf "/usr/src/$(PACKAGE_NAME)-$$ver"; \
	  done; \
	fi

list-controls:
	-rm asound.state
	alsactl -f asound.state store

show-controls: list-controls
	less asound.state

enable-debug-log:
	echo 8 > /proc/sys/kernel/printk
