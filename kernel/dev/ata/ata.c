#include <ananas/types.h>
#include <ananas/dev/ata.h>
#include <ananas/x86/io.h>
#include <ananas/bio.h>
#include <ananas/device.h>
#include <ananas/error.h>
#include <ananas/irq.h>
#include <ananas/mm.h>
#include <ananas/trace.h>
#include <ananas/lib.h>

TRACE_SETUP;

extern struct DRIVER drv_atadisk;
extern struct DRIVER drv_atacd;

void
ata_irq(device_t dev)
{
	struct ATA_PRIVDATA* priv = (struct ATA_PRIVDATA*)dev->privdata;

	int stat = inb(priv->io_port + ATA_REG_STATUS);

	/* If we were not doing a request, no point in continuing */
	if (QUEUE_EMPTY(&priv->requests)) {
		return;
	}

	/*
	 * Fetch the request and remove it from the queue; ATA may give extra interrupts, which we
	 * happily ignore as the queue is empty when they arrive.
	 */
	struct ATA_REQUEST_ITEM* item = QUEUE_HEAD(&priv->requests);
	QUEUE_POP_HEAD(&priv->requests);
	KASSERT(item->bio != NULL, "ata queue item without associated bio buffer!");

	/* If this is an ATAPI command, we may need to send the command bytes at this point */
	if (item->flags & ATA_ITEM_FLAG_ATAPI) {
		/*
		 * In ATAPI-land, we obtain the number of bytes that could actually be read - this much
		 * data is waiting for us.
		 */
		uint16_t len = (uint16_t)inb(priv->io_port + ATA_REG_CYL_HI) << 8 | 
		                         inb(priv->io_port + ATA_REG_CYL_LO);
		item->bio->length = len;
	}

	if (item->flags & ATA_ITEM_FLAG_DMA) {
		/*
		 * DMA request; this means we'll have to determine whether the request worked and
		 * flag the buffer as such - it should have already been filled.
		 */
		outb(priv->atapci.atapci_io + ATA_PCI_REG_PRI_COMMAND, 0);

		stat = inb(priv->atapci.atapci_io + ATA_PCI_REG_PRI_STATUS);
		if (stat & ATA_PCI_STAT_ERROR)
			bio_set_error(item->bio);

		/* Reset the status bits */
		outb(priv->atapci.atapci_io + ATA_PCI_REG_PRI_STATUS, stat);
	} else {
		/* Use old-style error checking first */
		if (stat & ATA_STAT_ERR) {
			kprintf("ata error %x ==> %x\n", stat,inb(priv->io_port + 1));
			bio_set_error(item->bio);
			kfree(item);
			return;
		}

		/*
		 * PIO request OK - fill the bio data XXX need to port 'rep insw'. We do
		 * this before updating the buffer status to prevent races.
		 */
		if (item->flags & ATA_ITEM_FLAG_READ) {
			uint8_t* bio_data = item->bio->data;
			for(int count = 0; count < item->bio->length / 2; count++) {
				uint16_t data = inw(priv->io_port + ATA_REG_DATA);
				*bio_data++ = data & 0xff;
				*bio_data++ = data >> 8;
			}
		}

		if (item->flags & ATA_ITEM_FLAG_WRITE) {
			/* Write completed - bio is no longer dirty XXX errors? */
			item->bio->flags &= ~BIO_FLAG_DIRTY;
		}
	}
	
	/* Current request is done. Sign it off and away it goes */
	bio_set_available(item->bio);
	kfree(item);
}

static uint8_t
ata_read_status(device_t dev) {
	struct ATA_PRIVDATA* priv = (struct ATA_PRIVDATA*)dev->privdata;
	return inb(priv->io_port2 + ATA_REG_ALTSTATUS);
}

/*
 * This handles identification of a master/slave device on a given ata bus. It
 * will attempt both the ATA IDENTIFY and ATAPI IDENTIFY commands.
 *
 * This function returns 0 on failure or the identify command code that worked
 * on success.
 */
static int
ata_identify(device_t dev, int unit, struct ATA_IDENTIFY* identify)
{
	struct ATA_PRIVDATA* priv = (struct ATA_PRIVDATA*)dev->privdata;
	uint8_t cmd;
	int stat; /* used to store the ATA_REG_STATUS value */

#define TINY_WAIT() \
	do { \
		inb(priv->io_port2 + ATA_REG_ALTSTATUS); \
		inb(priv->io_port2 + ATA_REG_ALTSTATUS); \
		inb(priv->io_port2 + ATA_REG_ALTSTATUS); \
		inb(priv->io_port2 + ATA_REG_ALTSTATUS); \
	} while(0);

#define HUGE_WAIT() \
	do { \
		for (int i = 0; i < 10000; i++) \
			inb(priv->io_port2 + ATA_REG_ALTSTATUS);  \
	} while(0)

	/* Perform a software reset (resets the entire channel) */
	outb(priv->io_port + ATA_REG_DEVICEHEAD, 0xa0);
	TINY_WAIT();
	outb(priv->io_port2 + ATA_REG_DEVCONTROL, ATA_DCR_nIEN | ATA_DCR_SRST);
	HUGE_WAIT();
	outb(priv->io_port2 + ATA_REG_DEVCONTROL, 0);
	HUGE_WAIT();
	(void)inb(priv->io_port + ATA_REG_ERROR);

	/* Select our drive */
	outb(priv->io_port + ATA_REG_DEVICEHEAD, 0xa0 | (unit << 4));
	TINY_WAIT();

	/*
	 * Now we wait for BSY to clear. If this times out, we assume there is no
	 * device.
	 */
	int timeout = 50000;
	while (timeout > 0) {
		stat = inb(priv->io_port + ATA_REG_STATUS);
		if ((stat & ATA_STAT_BSY) == 0)
			break;
		TINY_WAIT();
		timeout--;
	}
	if (timeout == 0) {
		device_printf(dev, "timeout waiting for unit %u", unit);
		return 0;
	}

	/* OK, now we can get the device type */
	int atapi = 0;
	uint8_t cl = inb(priv->io_port + ATA_REG_CYL_LO);
	uint8_t ch = inb(priv->io_port + ATA_REG_CYL_HI);
	if (cl == 0x14 && ch == 0xeb) {
		/* This is a magic identifier for ATAPI devices! */
		atapi++;
	} else if (cl == 0x69 && ch == 0x96) {
		/* This is a magic identifier for SATA-ATAPI devices! */
		atapi++;
	} else if (cl == 0x3c && ch == 0xc3) {
		/* This is a magic identifier for SATA devices! */
	} else if (cl == 0 && ch == 0) {
		/* Plain old ATA disk */
	} else {
		device_printf(dev, "unit %u does not report recognized type (got 0x%x), assuming disk",
			unit, ch << 8 | cl);
	}

	/* Use the correct identify command based on whether we think this is ATAPI or not */
	cmd = atapi ? ATA_CMD_IDENTIFY_PACKET : ATA_CMD_IDENTIFY;

	/*
	 * Select the device and ask it to identify itself; this is used to figure out
	 * whether it exists and what kind of device it is.
	 */
	outb(priv->io_port + ATA_REG_DEVICEHEAD, 0xa0 | (unit << 4));
	TINY_WAIT();
	outb(priv->io_port + ATA_REG_COMMAND, cmd);
	TINY_WAIT();

	/* Wait for result, BSY must be cleared... */
	timeout = 5000;
	while (timeout > 0) {
		stat = inb(priv->io_port + ATA_REG_STATUS);
		if ((stat & ATA_STAT_BSY) == 0)
			break;
		TINY_WAIT();
		timeout--;
	}
	/* ... and DRDY must be set */
	while (timeout > 0 && ((stat & ATA_STAT_DRDY) == 0)) {
		stat = inb(priv->io_port + ATA_REG_STATUS);
		TINY_WAIT();
		timeout--;
	}
	if (!timeout) {
		device_printf(dev, "timeout waiting for identification of unit %u", unit);
		return 0;
	}

	/* Grab the result of the identification command XXX implement insw() */
	char* buf = (char*)identify;
	for (int count = 0; count < SECTOR_SIZE; ) {
		uint16_t x = inw(priv->io_port + ATA_REG_DATA);
		buf[count++] = x >> 8;
		buf[count++] = x & 0xff; 
	}

	/* Chop off the trailing spaces off the identity */
	for (int i = sizeof(identify->model) - 1; i > 0; i--) {
		if (identify->model[i] != ' ')
			break;
		identify->model[i] = '\0';
	}

	return cmd;
}

#define ATA_DELAY() \
		inb(priv->io_port + 0x206); inb(priv->io_port + 0x206);  \
		inb(priv->io_port + 0x206); inb(priv->io_port + 0x206); 

static void
ata_start_pio(device_t dev, struct ATA_REQUEST_ITEM* item)
{
	struct ATA_PRIVDATA* priv = (struct ATA_PRIVDATA*)dev->privdata;

	if (item->command != ATA_CMD_PACKET) {
		/* Feed the request to the drive - disk */
		outb(priv->io_port + ATA_REG_DEVICEHEAD, 0xe0 | (item->unit ? 0x10 : 0) | ((item->lba >> 24) & 0xf));
		outb(priv->io_port + ATA_REG_SECTORCOUNT, item->count);
		outb(priv->io_port + ATA_REG_SECTORNUM, item->lba & 0xff);
		outb(priv->io_port + ATA_REG_CYL_LO, (item->lba >> 8) & 0xff);
		outb(priv->io_port + ATA_REG_CYL_HI, (item->lba >> 16) & 0xff);
		outb(priv->io_port + ATA_REG_COMMAND, item->command);

		/* If we need to write data, do so */
		if (item->flags & ATA_ITEM_FLAG_WRITE) {
			/* Wait until the command is accepted */
			uint8_t status;
			while (ata_read_status(dev) & ATA_STAT_BSY);
			while (1) {
				status = ata_read_status(dev);
				if (status & ATA_STAT_ERR) {
					/* Got an error - this means the request cannot be completed */
					bio_set_error(item->bio);
					return;
				}
				if (status & ATA_STAT_DRQ)
					break;
			}

			/* XXX We really need outsw() or similar */
			uint8_t* bio_data = item->bio->data;
			for(int i = 0; i < item->bio->length; i += 2) {
				uint16_t v = bio_data[0] | (uint16_t)bio_data[1] << 8;
				outw(priv->io_port + ATA_REG_DATA, v);
				bio_data += 2;
			}
		}
	} else {
		/* Feed the request to the device - ATAPI */
		outb(priv->io_port + ATA_REG_DEVICEHEAD, item->unit << 4);
		ATA_DELAY(); ATA_DELAY();
		outb(priv->io_port + ATA_REG_FEATURES, 0); /* no DMA yet! */
		outb(priv->io_port + ATA_REG_CYL_LO, item->count & 0xff); /* note: in bytes! */
		outb(priv->io_port + ATA_REG_CYL_HI, item->count >> 8);
		outb(priv->io_port + ATA_REG_COMMAND, item->command);

		/* Wait until the command is accepted */
		uint8_t status;
		while (ata_read_status(dev) & ATA_STAT_BSY);
		while (1) {
			status = ata_read_status(dev);
			if (status & ATA_STAT_ERR) {
				/* Got an error - this means the request cannot be completed */
				bio_set_error(item->bio);
				return;
			}
			if (status & ATA_STAT_DRQ)
				break;
		}
		for (unsigned int i = 0; i < 6; i++) {
			/* XXX We really need outsw() */
			outw(priv->io_port + ATA_REG_DATA, *(uint16_t*)(&item->atapi_command[i * 2]));
		}
	}
}

static void
ata_start_dma(device_t dev, struct ATA_REQUEST_ITEM* item)
{
	struct ATA_PRIVDATA* priv = (struct ATA_PRIVDATA*)dev->privdata;

	struct ATAPCI_PRDT* prdt = &priv->atapci.atapci_prdt[0];
	KASSERT(((addr_t)prdt & 3) == 0, "prdt not dword-aligned");

	/* XXX For now, we assume a single request per go */
	prdt->prdt_base = (addr_t)BIO_DATA(item->bio) & ~KERNBASE; /* XXX 32 bit */
	prdt->prdt_size = item->bio->length | ATA_PRDT_EOT;

	/* Program the DMA parts of the PCI bus */
	outl(priv->atapci.atapci_io + ATA_PCI_REG_PRI_PRDT, (uint32_t)prdt & ~KERNBASE); /* XXX 32 bit */
	outw(priv->atapci.atapci_io + ATA_PCI_REG_PRI_STATUS, ATA_PCI_STAT_IRQ | ATA_PCI_STAT_ERROR);

	/* Feed the request to the drive - disk */
	outb(priv->io_port + ATA_REG_DEVICEHEAD, 0xe0 | (item->unit ? 0x10 : 0) | ((item->lba >> 24) & 0xf));
	outb(priv->io_port + ATA_REG_SECTORCOUNT, item->count);
	outb(priv->io_port + ATA_REG_SECTORNUM, item->lba & 0xff);
	outb(priv->io_port + ATA_REG_CYL_LO, (item->lba >> 8) & 0xff);
	outb(priv->io_port + ATA_REG_CYL_HI, (item->lba >> 16) & 0xff);
	outb(priv->io_port + ATA_REG_COMMAND, ATA_CMD_DMA_READ_SECTORS);

	/* Go! */
	uint32_t cmd = ATA_PCI_CMD_START;
	if (item->flags & ATA_ITEM_FLAG_READ)
		cmd |= ATA_PCI_CMD_RW;
	outb(priv->atapci.atapci_io + ATA_PCI_REG_PRI_COMMAND, cmd);
}

void
ata_start(device_t dev)
{
	struct ATA_PRIVDATA* priv = (struct ATA_PRIVDATA*)dev->privdata;

	KASSERT(!QUEUE_EMPTY(&priv->requests), "ata_start() with empty queue");

	/* XXX locking */
	/* XXX only do a single item now */

	struct ATA_REQUEST_ITEM* item = QUEUE_HEAD(&priv->requests);
	KASSERT(item->unit >= 0 && item->unit <= 1, "corrupted item number");
	KASSERT(item->count > 0, "corrupted count number");

	if (item->flags & ATA_ITEM_FLAG_DMA)
		ata_start_dma(dev, item);
	else
		ata_start_pio(dev, item);

	/*
	 * Now, we must wait for the IRQ to handle it. XXX what about errors?
 	 */
}

errorcode_t
ata_attach(device_t dev, uint32_t io, uint32_t irq)
{
	struct ATA_PRIVDATA* priv = kmalloc(sizeof(struct ATA_PRIVDATA));
	priv->io_port = io;
	/* XXX this is a hack - at least, until we properly support multiple resources */
	if (priv->io_port == 0x170) {
		priv->io_port2 = (uint32_t)0x374;
	} else if (priv->io_port == 0x1f0) {
		priv->io_port2 = (uint32_t)0x3f4;
	} else {
		device_printf(dev, "couldn't determine second I/O range");
		return ANANAS_ERROR(NO_RESOURCE);
	}
	QUEUE_INIT(&priv->requests);
	dev->privdata = priv;

	/* Ensure there's something living at the I/O addresses */
	if (inb(priv->io_port + ATA_REG_STATUS) == 0xff) return 1;

	if (!irq_register(irq, dev, ata_irq))
		return ANANAS_ERROR(NO_RESOURCE);

	/* reset the control register - this ensures we receive interrupts */
	outb(priv->io_port + ATA_REG_DEVCONTROL, 0);
	return ANANAS_ERROR_OK;
}

void
ata_attach_children(device_t dev)
{
	struct ATA_IDENTIFY identify;

	for (int unit = 0; unit < 2; unit++) {
		int cmd = ata_identify(dev, unit, &identify);
		if (!cmd)
			continue;

		struct DRIVER* driver = NULL;
		if (cmd == ATA_CMD_IDENTIFY) {
			driver = &drv_atadisk;
		} else if (cmd == ATA_CMD_IDENTIFY_PACKET) {
			/*
			 * We found something that replied to ATAPI. First of all, do a sanity
			 * check to ensure it speaks valid ATAPI.
			 */
			uint16_t general_cfg = ATA_GET_WORD(identify.general_cfg);
			if ((general_cfg & ATA_GENCFG_NONATA) == 0 ||
					(general_cfg & ATA_GENCFG_NONATAPI) != 0)
				continue;

			/* Isolate the device type; this tells us which driver we need to use */
			uint8_t dev_type = (general_cfg >> 8) & 0x1f;
			switch (dev_type) {
				case ATA_TYPE_CDROM:
					driver = &drv_atacd;
					break;
				default:
					device_printf(dev, "detected unsupported device as unit %u, ignored", unit);
					continue;
			}
		}
		if (driver == NULL)
			continue;

		device_t new_dev = device_alloc(dev, driver);
		new_dev->privdata = (void*)&identify; /* XXX this is a hack; we should have an userpointer */
		device_add_resource(new_dev, RESTYPE_CHILDNUM, unit, 0);
		device_attach_single(new_dev);
	}
}

void
ata_enqueue(device_t dev, void* request)
{
	struct ATA_PRIVDATA* priv = (struct ATA_PRIVDATA*)dev->privdata;
	struct ATA_REQUEST_ITEM* item = (struct ATA_REQUEST_ITEM*)request;
	KASSERT(item->bio != NULL, "ata_enqueue(): request without bio data buffer");
	/*
	 * XXX Duplicate the request; this should be converted to a preallocated list
	 *     or something someday.
	 */
	struct ATA_REQUEST_ITEM* newitem = kmalloc(sizeof(struct ATA_REQUEST_ITEM));
	memcpy(newitem, request, sizeof(struct ATA_REQUEST_ITEM));
	QUEUE_ADD_TAIL(&priv->requests, newitem);
}

/* ATA itself will not be probed; ataisa/atapci take care of this */
static struct DRIVER drv_ata;
DRIVER_PROBE(ata)
DRIVER_PROBE_END()

/* vim:set ts=2 sw=2: */
