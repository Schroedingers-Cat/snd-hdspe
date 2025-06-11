// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA driver for RME HDSPe MADI/AES/RayDAT/AIO/AIO Pro audio interface(s)
 *
 *      Copyright (c) 2003 Winfried Ritsch (IEM)
 *      code based on hdsp.c   Paul Davis
 *                             Marcus Andersson
 *                             Thomas Charbonnel
 *      Modified 2006-06-01 for AES support by Remy Bruno
 *                                               <remy.bruno@trinnov.com>
 *
 *      Modified 2009-04-13 for proper metering by Florian Faber
 *                                               <faber@faberman.de>
 *
 *      Modified 2009-04-14 for native float support by Florian Faber
 *                                               <faber@faberman.de>
 *
 *      Modified 2009-04-26 fixed bug in rms metering by Florian Faber
 *                                               <faber@faberman.de>
 *
 *      Modified 2009-04-30 added hw serial number support by Florian Faber
 *
 *      Modified 2011-01-14 added S/PDIF input on RayDATs by Adrian Knoth
 *
 *	Modified 2011-01-25 variable period sizes on RayDAT/AIO by Adrian Knoth
 *
 *      Modified 2019-05-23 fix AIO single speed ADAT capture and playback
 *      by Philippe.Bekaert@uhasselt.be
 *
 *      Modified 2021-07 ... 2021-12 AIO Pro support, fixes, register 
 *      documentation, clean up, refactoring, updated user space API,
 *      renamed hdspe, updated control elements, TCO LTC output, double/quad
 *      speed AIO / AIO Pro fixes, ...
 *      by Philippe.Bekaert@uhasselt.be
 */

#include "hdspe.h"
#include "hdspe_core.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <sound/pcm.h>
#include <sound/initval.h>

#include <linux/version.h> 

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	  /* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	  /* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;/* Enable this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for RME HDSPE interface.");

module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for RME HDSPE interface.");

module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable/disable specific HDSPE soundcards.");


MODULE_AUTHOR
(
	"Winfried Ritsch <ritsch_AT_iem.at>, "
	"Paul Davis <paul@linuxaudiosystems.com>, "
	"Marcus Andersson, Thomas Charbonnel <thomas@undata.org>, "
	"Remy Bruno <remy.bruno@trinnov.com>, "
	"Florian Faber <faberman@linuxproaudio.org>, "
	"Adrian Knoth <adi@drcomp.erfurt.thur.de>, "
	"Philippe Bekaert <Philippe.Bekaert@uhasselt.be> "
);
MODULE_DESCRIPTION("RME HDSPe");
MODULE_LICENSE("GPL");

// This driver can obsolete old snd-hdspm driver.
MODULE_ALIAS("snd-hdspm");


/* RME PCI vendor ID as it is reported by the RME AIO PRO card */
#ifndef PCI_VENDOR_ID_RME
#define PCI_VENDOR_ID_RME 0x1d18
#endif /*PCI_VENDOR_ID_RME*/

static const struct pci_device_id snd_hdspe_ids[] = {
	{.vendor = PCI_VENDOR_ID_XILINX,
	 .device = PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP_MADI,
	 .subvendor = PCI_ANY_ID,
	 .subdevice = PCI_ANY_ID,
	 .class = 0,
	 .class_mask = 0,
	 .driver_data = 0},
	{.vendor = PCI_VENDOR_ID_RME,
	 .device = PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP_MADI,
	 .subvendor = PCI_ANY_ID,
	 .subdevice = PCI_ANY_ID,
	 .class = 0,
	 .class_mask = 0,
	 .driver_data = 0},
	{0,}	
};

MODULE_DEVICE_TABLE(pci, snd_hdspe_ids);


/* interrupt handler */
static irqreturn_t snd_hdspe_interrupt(int irq, void *dev_id)
{
	struct hdspe *hdspe = (struct hdspe *) dev_id;
	int i, audio, midi, schedule = 0;
	// if (hdspe->irq_count % 1000 == 0) {
	// 	dev_dbg(hdspe->card->dev, "Interrupt #%d received\n", hdspe->irq_count);
	// }

	hdspe->reg.status0 = hdspe_read_status0_nocache(hdspe);

	audio = hdspe->reg.status0.common.IRQ;
	midi = hdspe->reg.status0.raw & hdspe->midiIRQPendingMask;

#ifdef TIME_INTERRUPT_INTERVAL
	u64 now = ktime_get_raw_fast_ns();
	dev_dbg(hdspe->card->dev, "snd_hdspe_interrupt %llu us LAT=%d BUF_ID=%u BUF_PTR=%05u %s%s%s%s%s\n",
		(now - hdspe->last_interrupt_time) / 1000,
		hdspe->reg.control.common.LAT,
		hdspe->reg.status0.common.BUF_ID,
		le16_to_cpu(hdspe->reg.status0.common.BUF_PTR)<<6,
		audio ? "AUDIO " : "",
		hdspe->midiPorts>0 && (hdspe->reg.status0.raw & hdspe->midi[0].irq) ? "MIDI1 " : "",
		hdspe->midiPorts>1 && (hdspe->reg.status0.raw & hdspe->midi[1].irq) ? "MIDI2 " : "",
		hdspe->midiPorts>2 && (hdspe->reg.status0.raw & hdspe->midi[2].irq) ? "MIDI3 " : "",
		hdspe->midiPorts>3 && (hdspe->reg.status0.raw & hdspe->midi[3].irq) ? "MIDI4 " : ""
		);
	hdspe->last_interrupt_time = now;
#endif /*TIME_INTERRUPT_INTERVAL*/

	if (!audio && !midi)
		return IRQ_NONE;

	if (audio) {
		// if (hdspe->irq_count % 1000 == 0) {
		// 	dev_dbg(hdspe->card->dev, "Audio interrupt \n");
		// }

		hdspe_write(hdspe, HDSPE_interruptConfirmation, 0);
		hdspe->irq_count++;
		
		hdspe_update_frame_count(hdspe);

		if (hdspe->tco) {
			/* LTC In update must happen before user
			 * space is notified of a new period */
			hdspe_tco_period_elapsed(hdspe);
		}

		if (hdspe->capture_substream)
		{
			snd_pcm_period_elapsed(hdspe->capture_substream);
			// if (hdspe->capture_buffer && hdspe->irq_count % 1000 == 0)
			// {
			//     dev_dbg(hdspe->card->dev, "Capture Buffer %d \n", *hdspe->capture_buffer);
			// }
		}

		if (hdspe->playback_substream)
		{
			snd_pcm_period_elapsed(hdspe->playback_substream);
			// if (hdspe->playback_buffer && hdspe->irq_count % 1000 == 0)
			// {
			// 	dev_dbg(hdspe->card->dev, "Playback Buffer %d \n", *hdspe->playback_buffer);
			// }
		}

		/* status polling at user controlled rate */
		if (hdspe->status_polling > 0 &&
		    jiffies >= hdspe->last_status_jiffies
		    + HZ/hdspe->status_polling) {
			hdspe->last_status_jiffies = jiffies;
			schedule_work(&hdspe->status_work);
		}
	}

	if (midi) {
		if (hdspe->irq_count % 1000 == 0) {
			dev_dbg(hdspe->card->dev, "MIDI interrupt \n");
		}

		schedule = 0;
		for (i = 0; i < hdspe->midiPorts; i++) {
			if ((hdspe_read(hdspe,
					hdspe->midi[i].statusIn) & 0xff) &&
			    (hdspe->reg.status0.raw & hdspe->midi[i].irq)) {
				/* we disable interrupts for this input until
				 * processing is done */
				hdspe->reg.control.raw &= ~hdspe->midi[i].ie;
				hdspe->midi[i].pending = 1;
				schedule = 1;
			}
		}

		if (schedule) {
			hdspe_write_control(hdspe);
			queue_work(system_highpri_wq, &hdspe->midi_work);
		}
	}
	
	return IRQ_HANDLED;
}

/* Start audio and TCO MTC interrupts. Other MIDI interrupts
 * are enabled when the MIDI devices are created. */
static void hdspe_start_interrupts(struct hdspe* hdspe)
{
	if (hdspe->tco) {
		/* TCO MTC port is always the last one */
		struct hdspe_midi *m = &hdspe->midi[hdspe->midiPorts-1];
	
		dev_dbg(hdspe->card->dev,
			"%s: enabling TCO MTC input port %d '%s'.\n",
			__func__, m->id, m->portname);
		hdspe->reg.control.raw |= m->ie;	
	}

	hdspe->reg.control.common.START    = true;
	hdspe->reg.control.common.IE_AUDIO = true;

	hdspe_write_control(hdspe);
	dev_dbg(hdspe->card->dev, "Interrupts have been started\n");
}

static void hdspe_stop_interrupts(struct hdspe* hdspe)
{
	/* stop the audio, and cancel all interrupts */
	hdspe->reg.control.common.START    = false;
	hdspe->reg.control.common.IE_AUDIO = false;
	hdspe->reg.control.raw            &= ~hdspe->midiInterruptEnableMask;

	hdspe_write_control(hdspe);
	dev_dbg(hdspe->card->dev, "Interrupts have been stopped\n");
}

/* Create ALSA devices, after hardware initialization */
static int snd_hdspe_create_alsa_devices(struct snd_card *card,
					 struct hdspe *hdspe)
{
	int err, i;

	dev_dbg(card->dev, "Create ALSA PCM devices ...\n");
	err = snd_hdspe_create_pcm(card, hdspe);
	if (err < 0)
		return err;

	dev_dbg(card->dev, "Create ALSA MIDI devices ...\n");
	for (i = 0; i < hdspe->midiPorts; i++) {
		err = snd_hdspe_create_midi(card, hdspe, i);
		if (err < 0)
			return err;
	}

	dev_dbg(card->dev, "Create ALSA hwdep ...\n");		
	err = snd_hdspe_create_hwdep(card, hdspe);
	if (err < 0)
		return err;

	dev_dbg(card->dev, "Create ALSA controls ...\n");	
	err = snd_hdspe_create_controls(card, hdspe);
	if (err < 0)
		return err;
	
	dev_dbg(card->dev, "Init proc interface...\n");
	snd_hdspe_proc_init(hdspe);

	dev_dbg(card->dev, "Initializing complete?\n");

	err = snd_card_register(card);
	if (err < 0) {
		dev_err(card->dev, "error registering card.\n");
		return err;
	}
	
	dev_dbg(card->dev, "... yes now\n");

	return 0;
}

/* Initialize struct hdspe fields beyond PCI info, hardware vars, firmware
 * revision and build, serial no, io_type, mixer and TCO. */
static int hdspe_init(struct hdspe* hdspe)
{
	hdspe->pcm = NULL;
	hdspe->hwdep = NULL;
	hdspe->capture_substream = hdspe->playback_substream = NULL;
	hdspe->capture_buffer = hdspe->playback_buffer = NULL;
	hdspe->capture_pid = hdspe->playback_pid = -1;
	hdspe->running = false;
	hdspe->irq_count = 0;

	// Initialize hardware registers and their cache, card_name, methods,
	// and tables.
	hdspe->reg.control.raw = hdspe->reg.settings.raw =
		hdspe->reg.pll_freq = hdspe->reg.status0.raw = 0;

	hdspe->reg.control.common.LAT = 6;
	hdspe->reg.control.common.freq = HDSPE_FREQ_44_1KHZ;
	hdspe->reg.control.common.LineOut = true;
	hdspe_write_control(hdspe);

	switch (hdspe->io_type) {
	case HDSPE_MADI    :
	case HDSPE_MADIFACE: hdspe_init_madi(hdspe); break;
	case HDSPE_AES     : hdspe_init_aes(hdspe); break;
	case HDSPE_RAYDAT  :
	case HDSPE_AIO     :
	case HDSPE_AIO_PRO : hdspe_init_raio(hdspe); break;
	default            : snd_BUG();
	}

	hdspe_read_status0_nocache(hdspe);          // init reg.status0
	hdspe_write_internal_pitch(hdspe, 1000000); // init reg.pll_freq

	// Set the channel map according the initial speed mode */
	hdspe_set_channel_map(hdspe, hdspe_speed_mode(hdspe));

	return 0;
}

static void hdspe_terminate(struct hdspe* hdspe)
{
	switch (hdspe->io_type) {
	case HDSPE_MADI    :
	case HDSPE_MADIFACE: hdspe_terminate_madi(hdspe); break;
	case HDSPE_AES     : hdspe_terminate_aes(hdspe); break;
	case HDSPE_RAYDAT  :
	case HDSPE_AIO     :
	case HDSPE_AIO_PRO : hdspe_terminate_raio(hdspe); break;
	default            : snd_BUG();
	}
}

/* get card serial number - for older cards */
static uint32_t snd_hdspe_get_serial_rev1(struct hdspe* hdspe)
{
	uint32_t serial = 0;
	if (hdspe->io_type == HDSPE_MADIFACE)
		return 0;
	
	serial = (hdspe_read(hdspe, HDSPE_midiStatusIn0)>>8) & 0xFFFFFF;
	/* id contains either a user-provided value or the default
	 * NULL. If it's the default, we're safe to
	 * fill card->id with the serial number.
	 *
	 * If the serial number is 0xFFFFFF, then we're dealing with
	 * an old PCI revision that comes without a sane number. In
	 * this case, we don't set card->id to avoid collisions
	 * when running with multiple cards.
	 */
	if (id[hdspe->dev] || serial == 0xFFFFFF) {
		serial = 0;
	}
	return serial;
}

/* get card serial number - for newer cards */
static uint32_t snd_hdspe_get_serial_rev2(struct hdspe* hdspe)
{
	uint32_t serial = 0;

	// TODO: test endianness issues
	/* get the serial number from the RD_BARCODE{0,1} registers */
	int i;
	union {
		__le32 dw[2];
		char c[8];
	} barcode;
	
	barcode.dw[0] = hdspe_read(hdspe, HDSPE_RD_BARCODE0);
	barcode.dw[1] = hdspe_read(hdspe, HDSPE_RD_BARCODE1);
	
	for (i = 0; i < 8; i++) {
		int c = barcode.c[i];
		if (c >= '0' && c <= '9')
			serial = serial * 10 + (c - '0');
	}

	return serial;
}

/* Get card model. TODO: check against Mac and windows driver */
static enum hdspe_io_type hdspe_get_io_type(int pci_vendor_id, int firmware_rev)
{
	switch (firmware_rev) {
	case HDSPE_RAYDAT_REV:
		return HDSPE_RAYDAT;
	case HDSPE_AIO_REV:
		return (pci_vendor_id == PCI_VENDOR_ID_RME) ?
			HDSPE_AIO_PRO : HDSPE_AIO;
	case HDSPE_MADIFACE_REV:
		return HDSPE_MADIFACE;
	default:
		if ((firmware_rev == 0xf0) ||
		    ((firmware_rev >= 0xe6) &&
		     (firmware_rev <= 0xea))) {
			return HDSPE_AES;
		} else if ((firmware_rev == 0xd2) ||
			   ((firmware_rev >= 0xc8)  &&
			    (firmware_rev <= 0xcf))) {
			return HDSPE_MADI;
		}
	}
	return HDSPE_IO_TYPE_INVALID;
}

static void hdspe_work_start(struct hdspe *hdspe)
{
	spin_lock_init(&hdspe->lock);
	INIT_WORK(&hdspe->midi_work, hdspe_midi_work);
	INIT_WORK(&hdspe->status_work, hdspe_status_work);
}

static int hdspe_init_all(struct hdspe *hdspe)
{
	int err;

	/* Mixer */
	err = hdspe_init_mixer(hdspe);
	if (err < 0)
		return err;

	/* TCO */
	err = hdspe_init_tco(hdspe);
	if (err < 0)
		return err;

	/* Methods, tables, registers */
	err = hdspe_init(hdspe);
	if (err < 0)
		return err;

	return 0;
}

static int snd_hdspe_create(struct hdspe *hdspe)
{
	struct snd_card *card = hdspe->card;
	struct pci_dev *pci = hdspe->pci;
	int err;
	unsigned long io_extent;

	hdspe->irq = -1;
	hdspe->port = 0;
	hdspe->iobase = NULL;

	hdspe_work_start(hdspe);

	pci_read_config_word(hdspe->pci,
			PCI_CLASS_REVISION, &hdspe->firmware_rev);
	hdspe->vendor_id = pci->vendor;

	dev_dbg(card->dev,
		"PCI vendor %04x, device %04x, class revision %x\n",
		pci->vendor, pci->device, hdspe->firmware_rev);
	
	strcpy(card->mixername, "RME HDSPe");
	strcpy(card->driver, "HDSPe");

	/* Determine card model */
	hdspe->io_type = hdspe_get_io_type(hdspe->vendor_id,
					   hdspe->firmware_rev);
	if (hdspe->io_type == HDSPE_IO_TYPE_INVALID) {
		dev_err(card->dev,
			"unknown firmware revision %d (0x%x)\n",
			hdspe->firmware_rev, hdspe->firmware_rev);
		return -ENODEV;
	}

	/* PCI */
	err = pci_enable_device(pci);
	if (err < 0)
		return err;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0) 
	err = dma_set_mask(&pci->dev, DMA_BIT_MASK(32));
#else 
	err = pci_set_dma_mask(pci, DMA_BIT_MASK(32));
#endif

	if (!err) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0) 
		err = dma_set_coherent_mask(&pci->dev, DMA_BIT_MASK(32));
#else
		err = pci_set_consistent_dma_mask(pci, DMA_BIT_MASK(32));
#endif
	}

	if (err != 0) {
		dev_err(card->dev, "No suitable DMA addressing support.\n");
		return -ENODEV;
	}

	pci_set_master(hdspe->pci);

	/* TODO: mac driver sets PCI latency timer to 255 ??? */
	
	err = pci_request_regions(pci, "hdspe");
	if (err < 0)
		return err;

	hdspe->port = pci_resource_start(pci, 0);
	io_extent = pci_resource_len(pci, 0);

	dev_dbg(card->dev, "grabbed memory region 0x%lx-0x%lx\n",
			hdspe->port, hdspe->port + io_extent - 1);

	hdspe->iobase = ioremap(hdspe->port, io_extent);
	if (!hdspe->iobase) {
		dev_err(card->dev, "unable to remap region 0x%lx-0x%lx\n",
				hdspe->port, hdspe->port + io_extent - 1);
		return -EBUSY;
	}
	dev_dbg(card->dev, "remapped region (0x%lx) 0x%lx-0x%lx\n",
			(unsigned long)hdspe->iobase, hdspe->port,
			hdspe->port + io_extent - 1);

	if (request_irq(pci->irq, snd_hdspe_interrupt,
			IRQF_SHARED, KBUILD_MODNAME, hdspe)) {
		dev_err(card->dev, "unable to use IRQ %d\n", pci->irq);
		return -EBUSY;
	}

	dev_dbg(card->dev, "use IRQ %d\n", pci->irq);

	hdspe->irq = pci->irq;
	card->sync_irq = hdspe->irq;

	/* Firmware build */
	hdspe->fw_build = le32_to_cpu(hdspe_read(hdspe, HDSPE_RD_FLASH)) >> 12;
	dev_dbg(card->dev, "firmware build %d\n", hdspe->fw_build);

	/* Serial number */
	if (pci->vendor == PCI_VENDOR_ID_RME || hdspe->fw_build >= 200)
		hdspe->serial = snd_hdspe_get_serial_rev2(hdspe);
	else
		hdspe->serial = snd_hdspe_get_serial_rev1(hdspe);
	dev_dbg(card->dev, "serial nr %08d\n", hdspe->serial);

	/* Card ID */
	if (hdspe->serial != 0) { /* don't set ID if no serial (old PCI card) */
		snprintf(card->id, sizeof(card->id), "HDSPe%08d",
			 hdspe->serial);
		snd_card_set_id(card, card->id);
	} else {
		dev_warn(card->dev, "Card ID not set: no serial number.\n");
	}

	/* Init all HDSPe things like TCO, methods, tables, registers ... */
	err = hdspe_init_all(hdspe);
	if (err < 0)
		return err;

	/* Create ALSA devices */
	err = snd_hdspe_create_alsa_devices(card, hdspe);
	if (err < 0)
		return err;

	if (hdspe->io_type != HDSPE_MADIFACE && hdspe->serial != 0) {
		snprintf(card->shortname, sizeof(card->shortname), "%s_%08d",
			hdspe->card_name, hdspe->serial);
		snprintf(card->longname, sizeof(card->longname),
			 "%s S/N %08d at 0x%lx irq %d",
			 hdspe->card_name, hdspe->serial,
			 hdspe->port, hdspe->irq);
	} else {
		// TODO: MADIFACE really has no serial nr?
		snprintf(card->shortname, sizeof(card->shortname), "%s",
			 hdspe->card_name);
		snprintf(card->longname, sizeof(card->longname),
			 "%s at 0x%lx irq %d",
			 hdspe->card_name, hdspe->port, hdspe->irq);
	}
	
	return 0;
}

static void hdspe_work_stop(struct hdspe *hdspe)
{
	if (hdspe->port) 
	{
		hdspe_stop_interrupts(hdspe);
		cancel_work_sync(&hdspe->midi_work);
		cancel_work_sync(&hdspe->status_work);
	}
}

static void hdspe_deinit_all(struct hdspe *hdspe)
{
	if (hdspe->port) 
	{
		hdspe_terminate(hdspe);
		hdspe_terminate_tco(hdspe);
		hdspe_terminate_mixer(hdspe);
	}
}

static int snd_hdspe_free(struct hdspe * hdspe)
{
	hdspe_work_stop(hdspe);
	hdspe_deinit_all(hdspe);

	if (hdspe->irq >= 0)
		free_irq(hdspe->irq, (void *) hdspe);

	if (hdspe->iobase)
		iounmap(hdspe->iobase);

	if (hdspe->port)
		pci_release_regions(hdspe->pci);

	if (pci_is_enabled(hdspe->pci))
		pci_disable_device(hdspe->pci);

	return 0;
}

static void snd_hdspe_card_free(struct snd_card *card)
{
	struct hdspe *hdspe = card->private_data;

	if (hdspe)
		snd_hdspe_free(hdspe);
}

static int snd_hdspe_probe(struct pci_dev *pci,
			   const struct pci_device_id *pci_id)
{
	static int dev;
	struct hdspe *hdspe;
	struct snd_card *card;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = snd_card_new(&pci->dev, index[dev], id[dev],
			   THIS_MODULE, sizeof(*hdspe), &card);
	if (err < 0)
		return err;

	card->private_free = snd_hdspe_card_free;

	hdspe = card->private_data;
	hdspe->card = card;
	hdspe->dev = dev;
	hdspe->pci = pci;

	err = snd_hdspe_create(hdspe);
	if (err < 0)
		goto free_card;

	err = snd_card_register(card);
	if (err < 0)
		goto free_card;

	pci_set_drvdata(pci, card);

	dev++;

	hdspe_start_interrupts(hdspe);
	
	return 0;

free_card:
	snd_card_free(card);
	return err;
}

static void snd_hdspe_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
}

static void print_registers(struct hdspe *hdspe)
{
	dev_dbg(hdspe->card->dev, "Registers have the following states: Control: %x Settings: %x pll_freq: %x status0: %x\n", hdspe->reg.control.raw, hdspe->reg.settings.raw, hdspe->reg.pll_freq, hdspe->reg.status0.raw);
	dev_dbg(hdspe->card->dev, "Control Register Start: %d\n", hdspe->reg.control.common.START);
	dev_dbg(hdspe->card->dev, "Control Register Freq: %d in DS: %d or QS: %d\n", hdspe->reg.control.common.freq, hdspe->reg.control.common.ds, hdspe->reg.control.common.qs);
	dev_dbg(hdspe->card->dev, "Control Register Clock Mode: %d\n", hdspe->reg.control.madi.Master);
	dev_dbg(hdspe->card->dev, "Control Register tx_64ch: %d\n", hdspe->reg.control.madi.tx_64ch);
	dev_dbg(hdspe->card->dev, "Control Register AutoInput: %d\n", hdspe->reg.control.madi.AutoInp);
	dev_dbg(hdspe->card->dev, "Control Register SyncRef: %d\n", hdspe->reg.control.madi.SyncRef);
	dev_dbg(hdspe->card->dev, "Settings Register Master: %d\n", hdspe->reg.settings.raio.Master);
	dev_dbg(hdspe->card->dev, "Settings Register SyncRef: %d\n", hdspe->reg.settings.raio.SyncRef);
	dev_dbg(hdspe->card->dev, "Settings Register DS_DoubleWire: %d\n", hdspe->reg.settings.raio.DS_DoubleWire);
	dev_dbg(hdspe->card->dev, "Settings Register QS_DoubleWire: %d\n", hdspe->reg.settings.raio.QS_DoubleWire);
	dev_dbg(hdspe->card->dev, "Settings Register QS_QuadWire: %d\n", hdspe->reg.settings.raio.QS_QuadWire);
	dev_dbg(hdspe->card->dev, "Settings Register Madi_64_Channels: %d\n", hdspe->reg.settings.raio.Madi_64_Channels);
	dev_dbg(hdspe->card->dev, "Settings Register Madi_AutoInput: %d\n", hdspe->reg.settings.raio.Madi_AutoInput);
	dev_dbg(hdspe->card->dev, "Settings Register Input: %d\n", hdspe->reg.settings.raio.Input);
	dev_dbg(hdspe->card->dev, "Settings Register Input: %d\n", hdspe->reg.settings.raio.Input);
	dev_dbg(hdspe->card->dev, "status0 Register rx_64ch: %d\n", hdspe->reg.status0.madi.rx_64ch);
	dev_dbg(hdspe->card->dev, "status0 Register AB_int: %d\n", hdspe->reg.status0.madi.AB_int);
	dev_dbg(hdspe->card->dev, "status0 Register madi_lock: %d\n", hdspe->reg.status0.madi.madi_lock);
	dev_dbg(hdspe->card->dev, "status0 Register sync_in_lock: %d\n", hdspe->reg.status0.madi.sync_in_lock);
	dev_dbg(hdspe->card->dev, "status0 Register sync_in_sync: %d\n", hdspe->reg.status0.madi.sync_in_sync);
	dev_dbg(hdspe->card->dev, "status0 Register madi_sync: %d\n", hdspe->reg.status0.madi.madi_sync);
	dev_dbg(hdspe->card->dev, "status0 Register madi_freq: %d\n", hdspe->reg.status0.madi.madi_freq);

	dev_dbg(hdspe->card->dev, "Saved registers have the following states: Control: %x Settings: %x pll_freq: %x status0: %x\n", hdspe->savedRegisters.control.raw, hdspe->savedRegisters.settings.raw, hdspe->savedRegisters.pll_freq, hdspe->savedRegisters.status0.raw);
	dev_dbg(hdspe->card->dev, "Saved Control Register Start: %d\n", hdspe->savedRegisters.control.common.START);
	dev_dbg(hdspe->card->dev, "Saved Control Register Freq: %d in DS: %d or QS: %d\n", hdspe->savedRegisters.control.common.freq, hdspe->savedRegisters.control.common.ds, hdspe->savedRegisters.control.common.qs);
	dev_dbg(hdspe->card->dev, "Saved Control Register Clock Mode: %d\n", hdspe->savedRegisters.control.madi.Master);
	dev_dbg(hdspe->card->dev, "Saved Control Register tx_64ch: %d\n", hdspe->savedRegisters.control.madi.tx_64ch);
	dev_dbg(hdspe->card->dev, "Saved Control Register AutoInput: %d\n", hdspe->savedRegisters.control.madi.AutoInp);
	dev_dbg(hdspe->card->dev, "Saved Control Register SyncRef: %d\n", hdspe->savedRegisters.control.madi.SyncRef);
}

static int __maybe_unused snd_hdspe_suspend(struct pci_dev *dev, pm_message_t state)
{
	/* (1) Accessing HDSPe data */
	struct snd_card *card = pci_get_drvdata(dev);
	if (!card) {
		return -ENODEV;
	}

	struct hdspe *hdspe = card->private_data;
	if (!hdspe) {
		return -ENODEV;
	}

	dev_dbg(hdspe->card->dev, "Suspending HDSPe driver to state %x\n", hdspe->t.supported_power_state);

	/* (2) Change ALSA power state */
	snd_power_change_state(card, hdspe->t.supported_power_state);
	// Alternatively, do something like this??
	/*
	u16 pm_cap;
	pci_read_config_word(pdev, PCI_PM_CTRL, &pm_cap);

	// Check if the device supports D1 state
	if (pm_cap & PCI_PM_CAP_D1) 
	{

	}

	// Check if the device supports D2 state
	if (pm_cap & PCI_PM_CAP_D2) 
	{

	}
	*/

	/* (3) Save register values */
	/* Save the necessary register values in hdspe struct */
	spin_lock_irq(&hdspe->lock);
	// without savedRegisters, it's 104e9 vs 104c8 for the control register -> because of the interrupts (START & IE_AUDIO)
	hdspe->savedRegisters = hdspe->reg;
	// print_registers(hdspe);
	spin_unlock_irq(&hdspe->lock);

	/* (4) Stop hardware operations */
	/* Stop interrupts and halt any ongoing operations */
	hdspe_work_stop(hdspe);
	// snd_hdspe_deinit_all(hdspe);

	/* (5) Enter low-power state */
	/* Place the hardware into a low-power mode, not sure if that is available for HDSPe? */

	dev_dbg(&dev->dev, "Suspending HDSPe driver ended\n");
	return 0;
}

static int __maybe_unused snd_hdspe_resume(struct pci_dev *dev)
{
	/* (1) Accessing HDSPe data */
	struct snd_card *card = pci_get_drvdata(dev);
	if (!card) {
		return -ENODEV;
	}

	struct hdspe *hdspe = card->private_data;
	if (!hdspe) {
		return -ENODEV;
	}

	dev_dbg(hdspe->card->dev, "Resuming HDSPe driver\n");

	/* (2) Reinitialize the chip */
	/* Perform any necessary reinitialization steps after resume */
	/* Unclear what HDSPe needs to have reinitialized? */
	/* Init all HDSPe things like TCO, methods, tables, registers ... */
	hdspe_work_start(hdspe);


	// int err;
	// err = hdspe_init_all(hdspe);
	// if (err < 0)
	// 	return err;

	/* (3) Restore saved register values */
	/* Restore the register values saved during suspend */
	spin_lock_irq(&hdspe->lock);
	hdspe->reg = hdspe->savedRegisters;
	// print_registers(hdspe);
	spin_unlock_irq(&hdspe->lock);

	/* (4) Update hardware with restored register values */
	/* Write restored register values to the hardware */
	hdspe_write_settings(hdspe);
	hdspe_write_control(hdspe);
	hdspe_write_pll_freq(hdspe);
	// hdspe_set_channel_map(hdspe, hdspe_speed_mode(hdspe));

	/* Set PLL frequency from restored registers */
	dev_dbg(hdspe->card->dev, "Restoring PLL freq\n");
	hdspe_write_pll_freq(hdspe);

	/* Resume mixer? hdspe_init_mixer just allocates memory ... */

	/* (5) Restart the chip or hardware */
	/* Restart any halted hardware or operations */
	// Technically, this redundantly sets START and IE_AUDIO in 
	// reg.control.common to true, which already happened via 
	// hdspe->savedRegisters
	hdspe_start_interrupts(hdspe);

	/* (6) Return ALSA to full power state */
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);

	dev_dbg(&dev->dev, "Resuming HDSPe driver ended\n");
	dev_dbg(&dev->dev, "HDSPe running status:%d\n", hdspe_is_running(hdspe));
	return 0;
}


static struct pci_driver hdspe_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_hdspe_ids,
	.probe = snd_hdspe_probe,
	.remove = snd_hdspe_remove,
#ifdef CONFIG_PM
	.suspend = snd_hdspe_suspend,
	.resume = snd_hdspe_resume,
#endif
};

module_pci_driver(hdspe_driver);
