#include "kernel/irq.h"
#include "kernel/x86/io.h"
#include "kernel/x86/pic.h"

namespace {

void
x86_pic_wire()
{
	/*
	 * Remap the interrupts; by default, IRQ 0-7 are mapped to interrupts 0x08 -
	 * 0x0f and IRQ 8-15 to 0x70 - 0x77. We remap IRQ 0-15 to 0x20-0x2f (since
	 * 0..0x1f is reserved by Intel).
	 */
#define IO_WAIT() do { int i = 10; while (i--) /* NOTHING */ ; } while (0);

	/* Start initialization: the PIC will wait for 3 command bytes */
	outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); IO_WAIT();
	outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); IO_WAIT();
	/* Data byte 1 is the interrupt vector offset - program for interrupt 0x20-0x2f */
	outb(PIC1_DATA, 0x20); IO_WAIT();
	outb(PIC2_DATA, 0x28); IO_WAIT();
	/* Data byte 2 tells the PIC how they are wired */
	outb(PIC1_DATA, 0x04); IO_WAIT();
	outb(PIC2_DATA, 0x02); IO_WAIT();
	/* Data byte 3 contains environment flags */
	outb(PIC1_DATA, ICW4_8086); IO_WAIT();
	outb(PIC2_DATA, ICW4_8086); IO_WAIT();
	/* Enable all interrupts */
	outb(PIC1_DATA, 0);
	outb(PIC2_DATA, 0);

#undef IO_WAIT
}

struct PIC: IRQSource
{
	PIC();

	void Mask(int no) override;
	void Unmask(int no) override;
	void Acknowledge(int no) override;
};

PIC::PIC()
	: IRQSource(0, 16)
{
}

void
PIC::Mask(int no)
{
	int port = PIC1_DATA;
	if (no >= 8) {
		port = PIC2_DATA;
		no -= 8;
	}

	outb(port, inb(port) | (1 << no));
}

void
PIC::Unmask(int no)
{
	int port = PIC1_DATA;
	if (no >= 8) {
		port = PIC2_DATA;
		no -= 8;
	}

	outb(port, inb(port) & ~(1 << no));
}

void
PIC::Acknowledge(int no)
{
	outb(0x20, 0x20);
	if (no >= 8)
		outb(0xa0, 0x20);
}

PIC pic;

} // unnamed namespace

void
x86_pic_init()
{
	/* Wire the PIC */
	x86_pic_wire();

	/* Register the PIC as interrupt source */
	irqsource_register(pic);
}

void
x86_pic_mask_all()
{
	/*
	 * Reset the PIC by re-initalizating it; this is needed because we want to reset
	 * the IRR register, but there is no way to directly do that.
	 *
	 * XXX This is only needed in Bochs, not qemu - what does real hardware do?
	 */
	x86_pic_wire();

	/*
	 * Mask all PIC interrupts; mainly used so that we get the PIC to a known
	 * state before we decide whether to use the PIC or APIC.
	 */
	outb(PIC1_DATA, 0xff);
	outb(PIC2_DATA, 0xff);
}

int
md_interrupts_enabled()
{
	int i;

	__asm(
		"pushfq\n"
		"pop 	%%rax\n"
	: "=a" (i));
	return (i & 0x200);
}

/* vim:set ts=2 sw=2: */
