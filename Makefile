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

# Debug and warning flags
DEBUG ?= 0
CONFIG_SND_DEBUG ?= 0
WARNINGS ?= 0

EXTRA_CFLAGS += $(if $(filter 1,$(DEBUG)),-DDEBUG,)
EXTRA_CFLAGS += $(if $(filter 1,$(CONFIG_SND_DEBUG)),-DCONFIG_SND_DEBUG,)
# W can only be W=1/2/3
WFLAG := $(if $(filter 1 2 3,$(WARNINGS)),W=$(WARNINGS),)

# Set default kernel directory and path variables.
KERNELRELEASE ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNELRELEASE)/build
PWD := $(shell pwd)

# --- Standard Build Targets ---
# These are the only targets needed for DKMS to work.
# They are also used for manual compilation.

all: modules

modules: dkms.conf
	$(MAKE) $(WFLAG) -C $(KDIR) M=$(PWD) modules

dkms.conf: dkms.conf.in
	@echo "Generating dkms.conf from template..."
	@sed -e "s/@PACKAGE_NAME@/$(PACKAGE_NAME)/g" \
	    -e "s/@PACKAGE_VERSION@/$(PACKAGE_VERSION)/g" \
	    $< > $@

clean:
	$(MAKE) $(WFLAG) -C $(KDIR) M=$(PWD) clean
	@rm -f *~ dkms.conf $(PACKAGE_NAME)-$(PACKAGE_VERSION)

insert: all remove-mainlined
	sudo insmod $(PACKAGE_NAME).ko

remove:
	sudo rmmod $(PACKAGE_NAME)

remove-mainlined:
	-sudo rmmod snd-hdspm

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

debug:
	$(MAKE) CONFIG_SND_DEBUG=1 DEBUG=1 WARNINGS=1
