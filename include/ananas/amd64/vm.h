#ifndef __AMD64_VM_H__
#define __AMD64_VM_H__

#include <ananas/types.h>

/*
 * Overview
 * --------
 *
 * On amd64, we can use the entire 64-bit address space - however, physical
 * addresses are 'just' 52 bits long. To cope, addresses are sign-extended.
 *
 * We use the following virtual memory map:
 *
 * 0x0000 0000 0000 0000               +--------------------------+
 *                                     | Unused                   |
 *                                     |                          |
 *              100 0000 (1MB)         +--------------------------+
 *                                     | Process memory           |
 *                                     |                          |
 *                                     +--------------------------+ ^
 * 0xffff 8800 0000 0000               | Kernel virtual addresses | | 64TB
 * 0xffff c7ff ffff ffff               +--------------------------+ v
 *                                     | Unused                   |
 *                                     +--------------------------+ ^
 * 0xffff ffff 8000 0000               | Kernel                   | | 2GB
 *                                     |                          | | 
 * 0xffff ffff ffff ffff               +--------------------------+ v
 *
 * The result is 1TB of kernel virtual addresses available.
 */

/* Convert a physical to a kernel virtual address */
#define PTOKV(x)		((x) | KMAP_KVA_START)

/* Convert a kernel virtual address to a physical address */
#define KVTOP(x)		((x) & ~KMAP_KVA_START)

/* Base kernel virtual address from which we'll make mappings */
#define KMAP_KVA_START		0xffff880000000000

/* Base kernel virtual address up to which mappings are made */
#define KMAP_KVA_END		0xffffc7ffffffffff

/* Direct mapped KVA start; we can use use KVTOP()/PTOKV() for it */
#define KMEM_DIRECT_START	0
#define KMEM_DIRECT_END		KERNBASE

/* Page entry flags */
#define PE_P		(1ULL <<  0)
#define PE_RW		(1ULL <<  1)
#define PE_US		(1ULL <<  2)
#define PE_PWT		(1ULL <<  3)
#define PE_PCD		(1ULL <<  4)
#define PE_A		(1ULL <<  5)
#define PE_D		(1ULL <<  6)
#define PE_PS		(1ULL <<  7)
#define PE_G		(1ULL <<  8)
#define PE_PAT		(1ULL << 12)
#define PE_NX		(1ULL << 63)

/* Segment Register privilege levels */
#define SEG_DPL_SUPERVISOR	0	/* Descriptor Privilege Level (kernel) */
#define SEG_DPL_USER		3	/* Descriptor Privilege Level (user) */
#define SEG_IGATE_TYPE		0xe	/* Interrupt gate type (disables interrupts) */
#define SEG_TGATE_TYPE 		0xf	/* Trap gate type (keeps interrupts intact) */

/* Machine Specific Registers */
#define MSR_EFER		0xc0000080
 #define MSR_EFER_SCE		(1 <<  0)	/* System Call Extensions */
 #define MSR_EFER_LME		(1 <<  8)	/* Long Mode Enable */
 #define MSR_EFER_LMA		(1 << 10)	/* Long Mode Active */
 #define MSR_EFER_NXE		(1 << 11)	/* No-Execute Enable */
 #define MSR_EFER_SVME		(1 << 12)	/* Secuure VM Enable */
 #define MSR_EFER_FFXSR		(1 << 14)	/* Fast FXSAVE/FXRSTOR */
#define MSR_STAR		0xc0000081
#define MSR_LSTAR		0xc0000082
#define MSR_CSTAR		0xc0000083
#define MSR_SFMASK		0xc0000084
#define MSR_FS_BASE		0xc0000100
#define MSR_GS_BASE		0xc0000101
#define MSR_KERNEL_GS_BASE	0xc0000102

/* CR0 specific flags */
#define CR0_TS			(1 << 3)	/* Task switched */

/* CR4 specific flags */
#define CR4_OSFXSR		(1 << 9)	/* OS saves/restores SSE state */
#define CR4_OSXMMEXCPT		(1 << 10)	/* OS will handle SIMD exceptions */

/*
 * GDT entry selectors, which are the offset in the GDT. We don't use indexes
 * here because the task entry is 16 bytes whereas everything else is 8 bytes.
 */
#define GDT_SEL_KERNEL_CODE	0x08
#define GDT_SEL_KERNEL_DATA	0x10
#define GDT_SEL_USER_DATA	0x18
#define GDT_SEL_USER_CODE	0x20
#define GDT_SEL_TASK		0x28
#define GDT_LENGTH		(GDT_SEL_TASK + 0x10)

#ifndef ASM

/* Maps relevant kernel addresses for a given thread */
void md_map_kernel(thread_t* t);

/* Maps num_pages at physical address phys to virtual address virt for thread t with flags flags */
void md_map_pages(thread_t* t, addr_t virt, addr_t phys, size_t num_pages, int flags);

/* Unmaps num_pages at virtual address virt for thread t */
void md_unmap_pages(thread_t* t, addr_t virt, size_t num_pages);

/* Frees the machine-dependant mapping structures for thread t */
void md_free_mappings(thread_t* t);

/* XXX removeme */
int md_get_mapping(thread_t* t, addr_t virt, int flags, addr_t* phys_addr);

#endif

#endif /* __AMD64_VM_H__ */
