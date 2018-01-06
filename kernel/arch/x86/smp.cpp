#include <ananas/types.h>
#include <ananas/error.h>
#include "kernel/init.h"
#include "kernel/kmem.h"
#include "kernel/lib.h"
#include "kernel/mm.h"
#include "kernel/pcpu.h"
#include "kernel/schedule.h"
#include "kernel/thread.h"
#include "kernel/trace.h"
#include "kernel/vm.h"
#include "kernel/x86/io.h"
#include "kernel/x86/acpi.h"
#include "kernel/x86/apic.h"
#include "kernel/x86/ioapic.h"
#include "kernel/x86/smp.h"
#include "kernel-md/interrupts.h"
#include "kernel-md/macro.h"
#include "kernel-md/param.h"
#include "kernel-md/vm.h"
#include "options.h"

#define SMP_DEBUG 0

TRACE_SETUP;

/* Application Processor's entry point and end */
extern "C" void *__ap_entry, *__ap_entry_end;

void md_remove_low_mappings();
void smp_destroy_ap_pagetable();

struct X86_SMP_CONFIG smp_config;

static struct PAGE* ap_page;
static int can_smp_launch = 0;
extern "C" volatile int num_smp_launched = 1; /* BSP is always launched */

static struct IRQ_SOURCE ipi_source = {
	.is_first = SMP_IPI_FIRST,
	.is_count = SMP_IPI_COUNT,
	.is_mask = NULL,
	.is_unmask = NULL,
	.is_ack = ioapic_ack
};

static void
delay(int n) {
	while (n-- > 0) {
		int x = 1000;
		while (x--);
	}
}

uint32_t
get_num_cpus()
{
	return smp_config.cfg_num_cpus;
}

static irqresult_t
smp_ipi_schedule(Ananas::Device*, void* context)
{
	/* Flip the reschedule flag of the current thread; this makes the IRQ reschedule us as needed */
	thread_t* curthread = PCPU_GET(curthread);
	curthread->t_flags |= THREAD_FLAG_RESCHEDULE;
	return IRQ_RESULT_PROCESSED;
}

static irqresult_t
smp_ipi_panic(Ananas::Device*, void* context)
{
	md_interrupts_disable();
	while (1) {
		__asm("hlt");
	}

	/* NOTREACHED */
	return IRQ_RESULT_PROCESSED;
}

void
smp_prepare_config(struct X86_SMP_CONFIG* cfg)
{
	/* Prepare the CPU structure. CPU #0 is always the BSP */
	cfg->cfg_cpu = static_cast<struct X86_CPU*>(kmalloc(sizeof(struct X86_CPU) * cfg->cfg_num_cpus));
	memset(cfg->cfg_cpu, 0, sizeof(struct X86_CPU) * cfg->cfg_num_cpus);
	for (int i = 0; i < cfg->cfg_num_cpus; i++) {
		struct X86_CPU* cpu = &cfg->cfg_cpu[i];

		/*
		 * Note that we don't need to allocate anything extra for the BSP.
		 */
		if (i == 0)
			continue;

# define GDT_SIZE (GDT_NUM_ENTRIES * 16)

		/*
		 * Allocate one buffer and place all necessary administration in there.
		 * Each AP needs an own GDT because it contains the pointer to the per-CPU
		 * data and the TSS must be distinct too.
		 */
		char* buf = static_cast<char*>(kmalloc(GDT_SIZE + sizeof(struct TSS) + sizeof(struct PCPU)));
		cpu->gdt = buf;
extern void* gdt; /* XXX */
		memcpy(cpu->gdt, &gdt, GDT_SIZE);
		struct TSS* tss = (struct TSS*)(buf + GDT_SIZE);
		memset(tss, 0, sizeof(struct TSS));
		GDT_SET_TSS64(cpu->gdt, GDT_SEL_TASK, 0, (addr_t)tss, sizeof(struct TSS));
		cpu->tss = (char*)tss;

		/* Initialize per-CPU data */
		struct PCPU* pcpu = (struct PCPU*)(buf + GDT_SIZE + sizeof(struct TSS));
		memset(pcpu, 0, sizeof(struct PCPU));
		pcpu->cpuid = i;
		pcpu->tss = (addr_t)cpu->tss;
		pcpu_init(pcpu);
		cpu->pcpu = pcpu;

		/* Use the idle thread stack to execute from; we're becoming the idle thread anyway */
		cpu->stack = reinterpret_cast<char*>(pcpu->idlethread->md_rsp);
	}

	/* Prepare the IOAPIC structure */
	cfg->cfg_ioapic = new X86_IOAPIC[cfg->cfg_num_ioapics];
	memset(cfg->cfg_ioapic, 0, sizeof(struct X86_IOAPIC) * cfg->cfg_num_ioapics);

	/* Prepare the BUS structure */
	cfg->cfg_bus = new X86_BUS[cfg->cfg_num_busses];
	memset(cfg->cfg_bus, 0, sizeof(struct X86_BUS) * cfg->cfg_num_busses);

	/* Prepare the INTERRUPTS structure */
	cfg->cfg_int = new X86_INTERRUPT[cfg->cfg_num_ints];
	memset(cfg->cfg_int, 0, sizeof(struct X86_INTERRUPT) * cfg->cfg_num_ints);
}

/*
 * Prepares SMP-specific memory allocations; this is separate to ensure we'll
 * have enough lower memory.
 */
void
smp_prepare()
{
	ap_page = page_alloc_single();
	KASSERT(page_get_paddr(ap_page) < 0x100000, "ap code must be below 1MB"); /* XXX crude */
}

/*
 * Called on the Boot Strap Processor, in order to prepare the system for
 * multiprocessing.
 */
errorcode_t
smp_init()
{
	/*
	 * The AP's start in real mode, so we need to provide them with a stub so
	 * they can run in protected mode. This stub must be located in the lower
	 * 1MB (which is why it is allocated as soon as possible in smp_prepare())
	 *
	 * i386: Note that the low memory mappings will not be removed in the SMP
	 *       case, so that the AP's can correctly switch to protected mode and
	 *       enable paging.  Once this is all done, the mapping can safely be removed.
	 */
	KASSERT(ap_page != NULL, "smp_prepare() not called");
	void* ap_code = kmem_map(page_get_paddr(ap_page), PAGE_SIZE, VM_FLAG_READ | VM_FLAG_WRITE | VM_FLAG_EXECUTE);
	memcpy(ap_code, &__ap_entry, (addr_t)&__ap_entry_end - (addr_t)&__ap_entry);
	kmem_unmap(ap_code, PAGE_SIZE);

	int bsp_apic_id;
	if (ananas_is_failure(acpi_smp_init(&bsp_apic_id))) {
		/* SMP not present or not usable */
		page_free(ap_page);
		smp_destroy_ap_pagetable();
		return ANANAS_ERROR(NO_DEVICE);
	}

	/* Program the I/O APIC - we currently just wire all ISA interrupts */
	for (int i = 0; i < smp_config.cfg_num_ints; i++) {
		struct X86_INTERRUPT* interrupt = &smp_config.cfg_int[i];
#if SMP_DEBUG
		kprintf("int %u: source=%u, dest=%u, bus=%x, apic=%x\n",
		 i, interrupt->source_no, interrupt->dest_no, interrupt->bus, interrupt->ioapic);
#endif

		if (interrupt->bus == NULL || interrupt->bus->type != BUS_TYPE_ISA)
			continue;
		if (interrupt->ioapic == NULL)
			continue;
		if (interrupt->dest_no < 0)
			continue;

		/* XXX For now, route all interrupts to the BSP */
		uint32_t reg = IOREDTBL + (interrupt->dest_no * 2);
		ioapic_write(interrupt->ioapic, reg, TRIGGER_EDGE | DESTMOD_PHYSICAL | DELMOD_FIXED | (interrupt->source_no + 0x20));
		ioapic_write(interrupt->ioapic, reg + 1, bsp_apic_id << 24);
	}

	/*
	 * Register an interrupt source for the IPI's; they appear as normal
 	 * interrupts and this lets us process them as such.
	 */
	irqsource_register(&ipi_source);
	if (ananas_is_failure(irq_register(SMP_IPI_PANIC, NULL, smp_ipi_panic, IRQ_TYPE_IPI, NULL)))
		panic("can't register ipi");
	if (ananas_is_failure(irq_register(SMP_IPI_SCHEDULE, NULL, smp_ipi_schedule, IRQ_TYPE_IPI, NULL)))
		panic("can't register ipi");

	/*
	 * Initialize the SMP launch variable; every AP will just spin and check this value. We don't
	 * care about races here, because it doesn't matter if an AP needs a few moments to determine
	 * that it needs to run.
	 */
	can_smp_launch = 0;

	return ananas_success();
}

/*
 * Called on the Boot Strap Processor, in order to fully launch the AP's.
 */
static errorcode_t
smp_launch()
{
	can_smp_launch++;

	/*
	 * Broadcast INIT-SIPI-SIPI-IPI to all AP's; this will wake them up and cause
	 * them to run the AP entry code.
	 */
	addr_t lapic_base = PTOKV(LAPIC_BASE);
	*((volatile uint32_t*)(lapic_base + LAPIC_ICR_LO)) = LAPIC_ICR_DEST_ALL_EXC_SELF | LAPIC_ICR_LEVEL_ASSERT | LAPIC_ICR_DELIVERY_INIT;
	delay(10);
	*((volatile uint32_t*)(lapic_base + LAPIC_ICR_LO)) = LAPIC_ICR_DEST_ALL_EXC_SELF | LAPIC_ICR_LEVEL_ASSERT | LAPIC_ICR_DELIVERY_SIPI | page_get_paddr(ap_page) >> 12;
	delay(200);
	*((volatile uint32_t*)(lapic_base + LAPIC_ICR_LO)) = LAPIC_ICR_DEST_ALL_EXC_SELF | LAPIC_ICR_LEVEL_ASSERT | LAPIC_ICR_DELIVERY_SIPI | page_get_paddr(ap_page) >> 12;
	delay(200);

	kprintf("SMP: %d CPU(s) found, waiting for %d CPU(s)\n", smp_config.cfg_num_cpus, smp_config.cfg_num_cpus - num_smp_launched);
	while(num_smp_launched < smp_config.cfg_num_cpus)
		/* wait for it ... */ ;

	/* All done - we can throw away the AP code and mappings */
	page_free(ap_page);
#ifdef __i386_
	md_remove_low_mappings();
#elif defined(__amd64__)
	smp_destroy_ap_pagetable();
#endif

	return ananas_success();
}

INIT_FUNCTION(smp_launch, SUBSYSTEM_SCHEDULER, ORDER_MIDDLE);

void
smp_panic_others()
{	
	if (num_smp_launched > 1)
		*((volatile uint32_t*)(PTOKV(LAPIC_BASE) + LAPIC_ICR_LO)) = LAPIC_ICR_DEST_ALL_EXC_SELF | LAPIC_ICR_LEVEL_ASSERT | LAPIC_ICR_DELIVERY_FIXED | SMP_IPI_PANIC;
}

void
smp_broadcast_schedule()
{
	*((volatile uint32_t*)(PTOKV(LAPIC_BASE) + LAPIC_ICR_LO)) = LAPIC_ICR_DEST_ALL_INC_SELF | LAPIC_ICR_LEVEL_ASSERT | LAPIC_ICR_DELIVERY_FIXED | SMP_IPI_SCHEDULE;
}

/*
 * Called by mp_stub.S for every Application Processor. Should not return.
 */
extern "C" void
mp_ap_startup(uint32_t lapic_id)
{
	/* Switch to our idle thread */
	thread_t* idlethread = PCPU_GET(idlethread);
  PCPU_SET(curthread, idlethread);
  scheduler_add_thread(idlethread);

	/* Reset destination format to flat mode */
	addr_t lapic_base = PTOKV(LAPIC_BASE);
	*(volatile uint32_t*)(lapic_base + LAPIC_DF) = 0xffffffff;
	/* Ensure we are the logical destination of our local APIC */
	volatile uint32_t* v = (volatile uint32_t*)(lapic_base + LAPIC_LD);
	*v = (*v & 0x00ffffff) | 1 << (lapic_id + 24);
	/* Clear Task Priority register; this enables all LAPIC interrupts */
	*(volatile uint32_t*)(lapic_base + LAPIC_TPR) &= ~0xff;
	/* Finally, enable the APIC */
	*(volatile uint32_t*)(lapic_base + LAPIC_SVR) |= LAPIC_SVR_APIC_EN;

	/* Wait for it ... */
	while (!can_smp_launch)
		/* nothing */ ;

	/* We're up and running! Increment the launched count */
	__asm("lock incl (num_smp_launched)");
	
	/* Enable interrupts and become the idle thread; this doesn't return */
	md_interrupts_enable();
	idle_thread(nullptr);
}

/* vim:set ts=2 sw=2: */
