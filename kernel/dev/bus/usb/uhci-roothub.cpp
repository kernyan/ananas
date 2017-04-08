/*
 * UHCI root hub
 */
#include <ananas/types.h>
#include <ananas/bus/pci.h>
#include <ananas/x86/io.h>
#include <ananas/error.h>
#include <ananas/trace.h>
#include <ananas/thread.h>
#include <ananas/schedule.h>
#include <ananas/time.h>
#include <ananas/lib.h>
#include <ananas/mm.h>
#include <machine/param.h> /* for PAGE_SIZE */
#include "descriptor.h"
#include "uhci-reg.h"
#include "uhci-hcd.h"
#include "uhci-roothub.h"
#include "usb-bus.h"
#include "usb-core.h"
#include "usb-device.h"
#include "usb-transfer.h"

TRACE_SETUP;

namespace Ananas {
namespace USB {
namespace UHCIRootHub {

#if 0
# define DPRINTF hcd->Printf
#else
# define DPRINTF(...)
#endif

namespace {

const struct USB_DESCR_DEVICE uhci_rh_device = {
	.dev_length = sizeof(struct USB_DESCR_DEVICE),
	.dev_type = 0,
	.dev_version = 0x101,
	.dev_class = USB_DESCR_CLASS_HUB,
	.dev_subclass = 0,
	.dev_protocol = 0,
	.dev_maxsize0 = 8,
	.dev_vendor = 0,
	.dev_product = 0,
	.dev_release = 0,
	.dev_manufactureridx = 2,
	.dev_productidx = 1,
	.dev_serialidx = 0,
	.dev_num_configs = 1,
};

const struct uhci_rh_string {
	uint8_t s_len, s_type;
	uint16_t s_string[13];
} uhci_rh_strings[] = {
	/* supported languages */
	{
		.s_len = 4,
		.s_type = USB_DESCR_TYPE_STRING,
		.s_string = {
			1033
		}
	},
	/* Product ID */
	{
		.s_len = 28,
		.s_type = USB_DESCR_TYPE_STRING,
		.s_string = {
			'U', 'H', 'C', 'I', ' ',
			'r', 'o', 'o', 't', ' ',
			'h', 'u', 'b'
		}
	},
	/* Vendor ID */
	{
		.s_len = 14,
		.s_type = USB_DESCR_TYPE_STRING,
		.s_string = {
			'A', 'n', 'a', 'n', 'a', 's'
		}
	},
};

struct {
	struct USB_DESCR_CONFIG d_config;
	struct USB_DESCR_INTERFACE d_interface;
	struct USB_DESCR_ENDPOINT d_endpoint;
} __attribute__((packed)) const uhci_rh_config = {
	/* Configuration */
	{
		.cfg_length = sizeof(struct USB_DESCR_CONFIG),
		.cfg_type = USB_DESCR_TYPE_CONFIG, 
		.cfg_totallen = sizeof(uhci_rh_config),
		.cfg_numinterfaces = 1,
		.cfg_identifier = 0,
		.cfg_stringidx = 0,
		.cfg_attrs = 0x40, /* self-powered */
		.cfg_maxpower = 0,
	},
	/* Interface */
	{
		.if_length = sizeof(struct USB_DESCR_INTERFACE),
		.if_type = USB_DESCR_TYPE_INTERFACE,
		.if_number = 1,
		.if_altsetting = 0,
		.if_numendpoints = 1,
		.if_class = USB_IF_CLASS_HUB,
		.if_subclass = 0,
		.if_protocol = 1,
		.if_interfaceidx = 0,
	},
	/* Endpoint */
	{
		.ep_length = sizeof(struct USB_DESCR_ENDPOINT),
		.ep_type = USB_DESCR_TYPE_ENDPOINT,
		.ep_addr = USB_EP_ADDR_IN | USB_EP_ADDR(1),
		.ep_attr = USB_PE_ATTR_TYPE_INTERRUPT,
		.ep_maxpacketsz = 8,
		.ep_interval = 255,
	}
};

errorcode_t
ControlTransfer(Transfer& xfer)
{
	struct USB_CONTROL_REQUEST* req = &xfer.t_control_req;
	auto hcd = static_cast<UHCI_HCD*>(xfer.t_device.ud_bus.d_Parent);
	errorcode_t err = ANANAS_ERROR(BAD_OPERATION);

#define MASK(x) ((x) & (UHCI_PORTSC_SUSP | UHCI_PORTSC_RESET | UHCI_PORTSC_RD	| UHCI_PORTSC_PORTEN))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

	switch(USB_REQUEST_MAKE(req->req_type, req->req_request)) {
		case USB_REQUEST_STANDARD_GET_DESCRIPTOR:
			switch(req->req_value >> 8) {
				case USB_DESCR_TYPE_DEVICE: {
					int amount = MIN(uhci_rh_device.dev_length, req->req_length);
					memcpy(xfer.t_data, &uhci_rh_device, amount);
					xfer.t_result_length = amount;
					err = ananas_success();
					break;
				}
				case USB_DESCR_TYPE_STRING: {
					int string_id = req->req_value & 0xff;
					if (string_id >= 0 && string_id < sizeof(uhci_rh_strings) / sizeof(uhci_rh_strings[0])) {
						int amount = MIN(uhci_rh_strings[string_id].s_len, req->req_length);
						memcpy(xfer.t_data, &uhci_rh_strings[string_id], amount);
						xfer.t_result_length = amount;
						err = ananas_success();
					}
					break;
				}
				case USB_DESCR_TYPE_CONFIG: {
					int amount = MIN(uhci_rh_config.d_config.cfg_totallen, req->req_length);
					memcpy(xfer.t_data, &uhci_rh_config, amount);
					xfer.t_result_length = amount;
					err = ananas_success();
					break;
				}
			}
			break;
		case USB_REQUEST_STANDARD_SET_ADDRESS:
			DPRINTF("set address: %d", req->req_value);
			err = ananas_success();
			break;
		case USB_REQUEST_STANDARD_SET_CONFIGURATION:
			DPRINTF("set config: %d", req->req_value);
			err = ananas_success();
			break;
		case USB_REQUEST_CLEAR_HUB_FEATURE:
			break;
		case USB_REQUEST_SET_HUB_FEATURE:
			break;
		case USB_REQUEST_GET_BUS_STATE:
			break;
		case USB_REQUEST_GET_HUB_DESCRIPTOR: {
			/* First step is to construct our hub descriptor */
			int port_len = (hcd->uhci_rh_numports + 7) / 8;
			struct USB_DESCR_HUB hd;
			memset(&hd, 0, sizeof(hd));
			hd.hd_length = sizeof(hd) - (HUB_MAX_PORTS + 7) / 8 + port_len;
			hd.hd_type = USB_DESCR_TYPE_HUB;
			hd.hd_numports = hcd->uhci_rh_numports;
			hd.hd_max_current = 0;

			hd.hd_flags = USB_HD_FLAG_PS_INDIVIDUAL;
			hd.hd_poweron2good = 50; /* 100ms */
			/* All ports are removable; no need to do anything */
			/* Copy the descriptor we just created */
			int amount = MIN(hd.hd_length, req->req_length);
			memcpy(xfer.t_data, &hd, amount);
			xfer.t_result_length = amount;
			err = ananas_success();
			break;
		}
		case USB_REQUEST_GET_HUB_STATUS: {
			if (req->req_value == 0 && req->req_index == 0 && req->req_length == 4) {
				uint32_t hs = 0;
				/* XXX over-current */
				memcpy(xfer.t_data, &hs, sizeof(hs));
				xfer.t_result_length = sizeof(hs);
				err = ananas_success();
			}
			break;
		}
		case USB_REQUEST_GET_PORT_STATUS: {
			if (req->req_value == 0 && req->req_index >= 1 && req->req_index <= hcd->uhci_rh_numports && req->req_length == 4) {
				int port_io = hcd->uhci_io + UHCI_REG_PORTSC1 + (req->req_index - 1) * 2;

				struct USB_HUB_PORTSTATUS ps;
				ps.ps_portstatus = USB_HUB_PS_PORT_POWER; /* always powered */
				ps.ps_portchange = 0;

				int portstat = inw(port_io);
				if (portstat & UHCI_PORTSC_SUSP)     ps.ps_portstatus |= USB_HUB_PS_PORT_SUSPEND;
				if (portstat & UHCI_PORTSC_RESET)    ps.ps_portstatus |= USB_HUB_PS_PORT_RESET;
				if (portstat & UHCI_PORTSC_LOWSPEED) ps.ps_portstatus |= USB_HUB_PS_PORT_LOW_SPEED;
				if (portstat & UHCI_PORTSC_PECHANGE) ps.ps_portchange |= USB_HUB_PC_C_PORT_ENABLE;
				if (portstat & UHCI_PORTSC_PORTEN)   ps.ps_portstatus |= USB_HUB_PS_PORT_ENABLE;
				if (portstat & UHCI_PORTSC_CSCHANGE) ps.ps_portchange |= USB_HUB_PC_C_PORT_CONNECTION;
				if (portstat & UHCI_PORTSC_CONNSTAT) ps.ps_portstatus |= USB_HUB_PS_PORT_CONNECTION;
				if (hcd->uhci_c_portreset) {
					/* C_PORT_RESET is emulated manually */
					ps.ps_portchange |= USB_HUB_PC_C_PORT_RESET;
					hcd->uhci_c_portreset = 0;
				}
				/* XXX We don't do overcurrent */

				memcpy(xfer.t_data, &ps, sizeof(ps));
				xfer.t_result_length = sizeof(ps);
				err = ananas_success();
			}
			break;
		}
		case USB_REQUEST_SET_PORT_FEATURE: {
			unsigned int port = req->req_index;
			if (port >= 1 && port <= hcd->uhci_rh_numports) {
				port = hcd->uhci_io + UHCI_REG_PORTSC1 + (req->req_index - 1) * 2;
				err = ananas_success();

				switch(req->req_value) {
					case HUB_FEATURE_PORT_RESET: {
						DPRINTF("set port reset, port %d", req->req_index);

						/* First step is to reset the port */
						outw(port, MASK(inw(port)) | UHCI_PORTSC_RESET);
						delay(200); /* port reset delay */
						outw(port, MASK(inw(port)) & (~UHCI_PORTSC_RESET));
						delay(100); /* device ready delay */

						/* Now enable the port (required per 11.16.2.6.1.2) */
						outw(port, MASK(inw(port)) | UHCI_PORTSC_PORTEN);

						/* Now see if the port becomes stable */
						int n = 10;
						for (/* nothing */; n > 0; n--) {
							delay(50); /* port reset delay */

							int stat = inw(port);
							if ((stat & UHCI_PORTSC_CONNSTAT) == 0)
								break; /* Device removed during reset; give up */

							if (stat & (UHCI_PORTSC_PECHANGE | UHCI_PORTSC_CSCHANGE)) {
								/* Port enable / connect state changed; acknowledge them both */
								outw(port, MASK(inw(port)) | (UHCI_PORTSC_PECHANGE | UHCI_PORTSC_CSCHANGE));
								continue;
							}

							if (stat & UHCI_PORTSC_PORTEN)
								break; /* port is enabled; we are done */

							/* Try harder to enable the port */
							outw(port, MASK(inw(port)) | UHCI_PORTSC_PORTEN);
						}
						if (n == 0) {
							kprintf("port %u not responding to reset", n);
							err = ANANAS_ERROR(NO_DEVICE);
						} else {
							/* Used to emulate 'port reset changed'-bit */
							hcd->uhci_c_portreset = 1;
						}
						break;
					}
					case HUB_FEATURE_PORT_SUSPEND:
						DPRINTF("set port suspend, port %d", req->req_index);
						outw(port, MASK(inw(port)) | UHCI_PORTSC_SUSP);
						err = ananas_success();
						break;
					case HUB_FEATURE_PORT_ENABLE:
						/*
						 * 11.16.2.6.1.2 states the hub's response to a
						 * SetPortFeature(PORT_ENABLE) is not specified; we'll never issue
						 * it and thus will reject the request (resetting ports must enable
						 * them).
					 	 */
						break;
					case HUB_FEATURE_PORT_POWER:
						/* No-op, power is always enabled for us */
						break;
					default:
						err = ANANAS_ERROR(BAD_OPERATION);
						break;
				}
			}
			break;
		}
		case USB_REQUEST_CLEAR_PORT_FEATURE: {
			unsigned int port = req->req_index;
			if (port >= 1 && port <= hcd->uhci_rh_numports) {
				port = hcd->uhci_io + UHCI_REG_PORTSC1 + (req->req_index - 1) * 2;
				err = ananas_success();
				switch(req->req_value) {
					case HUB_FEATURE_PORT_ENABLE:
						DPRINTF("HUB_FEATURE_PORT_ENABLE: port %d", req->req_index);
						outw(port, MASK(inw(port)) & ~UHCI_PORTSC_PORTEN);
						break;
					case HUB_FEATURE_PORT_SUSPEND:
						DPRINTF("HUB_FEATURE_PORT_SUSPEND: port %d", req->req_index);
						outw(port, MASK(inw(port)) & ~UHCI_PORTSC_SUSP);
						break;
					case HUB_FEATURE_C_PORT_CONNECTION:
						DPRINTF("HUB_FEATURE_C_PORT_CONNECTION: port %d", req->req_index);
						outw(port, MASK(inw(port)) | UHCI_PORTSC_CSCHANGE);
						break;
					case HUB_FEATURE_C_PORT_RESET:
						DPRINTF("HUB_FEATURE_C_PORT_RESET: port %d", req->req_index);
						hcd->uhci_c_portreset = 0;
						break;
					case HUB_FEATURE_C_PORT_ENABLE:
						DPRINTF("HUB_FEATURE_C_PORT_ENABLE: port %d", req->req_index);
						outw(port, MASK(inw(port)) | UHCI_PORTSC_PECHANGE);
						break;
					default:
						err = ANANAS_ERROR(BAD_OPERATION);
						break;
				}
			}
			break;
		}
		default:
			err = ANANAS_ERROR(BAD_TYPE);
			break;
	}

#undef MIN
#undef MASK

	if (ananas_is_failure(err)) {
		kprintf("oroothub: error %d\n", err);
		xfer.t_flags |= TRANSFER_FLAG_ERROR;
	}

	/* Immediately mark the transfer as completed */
	CompleteTransfer_Locked(xfer);
	return err;
}

static void
uroothub_update_status(USBDevice& usb_dev)
{
	auto hcd = static_cast<UHCI_HCD*>(usb_dev.ud_bus.d_Parent);

	/* Walk through every port, hungry for updates... */
	uint8_t hub_update = 0; /* max 7 ports, hub itself = 8 bits */
	int num_updates = 0;
	for (unsigned int n = 1; n <= hcd->uhci_rh_numports; n++) {
		int st = inw(hcd->uhci_io + UHCI_REG_PORTSC1 + (n - 1) * 2);
		if (st & (UHCI_PORTSC_PECHANGE | UHCI_PORTSC_CSCHANGE)) {
			/* A changed event was triggered - need to report this */
			hub_update |= 1 << (n % 8);
			num_updates++;
		}
	}

	if (num_updates > 0) {
		int update_len = 1; /* always a single byte since we max the port count */

		/* Alter all entries in the transfer queue */
		usb_dev.Lock();
		LIST_FOREACH_IP(&usb_dev.ud_transfers, pending, xfer, Transfer) {
			if (xfer->t_type != TRANSFER_TYPE_INTERRUPT)
				continue;
			memcpy(&xfer->t_data, &hub_update, update_len);
			xfer->t_result_length = update_len;
			CompleteTransfer_Locked(*xfer);
		}
		usb_dev.Unlock();
	}
}

static void
uroothub_thread(void* ptr)
{
	auto usb_dev = static_cast<USBDevice*>(ptr);
	auto hcd = static_cast<UHCI_HCD*>(usb_dev->ud_bus.d_Parent);
	KASSERT(hcd->uhci_roothub != NULL, "no root hub?");

	while (1) {
		uroothub_update_status(*hcd->uhci_roothub);

		/* XXX we need some sensible sleep mechanism */
		for (int n = 0; n < 100; n++) {
			delay(10);
			reschedule();
		}
	}
}

} // unnamed namespace

errorcode_t
HandleTransfer(Transfer& xfer)
{
	switch(xfer.t_type) {
		case TRANSFER_TYPE_CONTROL:
			return ControlTransfer(xfer);
		case TRANSFER_TYPE_INTERRUPT:
			/* Transfer has been added to the queue; no need to do anything else here */
			return ananas_success();
	}
	panic("unsupported transfer type %d", xfer.t_type);
}

void
Start(UHCI_HCD& hcd, USBDevice& usb_dev)
{
	/* Create a kernel thread to monitor status updates and process requests */
	kthread_init(&hcd.uhci_rh_pollthread, "uroothub", &uroothub_thread, &usb_dev);
	thread_resume(&hcd.uhci_rh_pollthread);
}

} // namespace UHCIRootHub
} // namespace USB
} // namespace Ananas

/* vim:set ts=2 sw=2: */
