#include <ananas/types.h>
#include "kernel/lib.h"
#include "kernel/lock.h"
#include "kernel/mm.h"
#include "kernel/vm.h"
#include "kernel-md/vm.h"

static Mutex mtx_mm("mm");

extern "C" {
void* dlmalloc(size_t);
void dlfree(void*);
}

void*
kmalloc(size_t len)
{
	MutexGuard g(mtx_mm);
	return dlmalloc(len);
}

void
kfree(void* addr)
{
	MutexGuard g(mtx_mm);
	dlfree(addr);
}

void*
operator new(size_t len)
{
	return kmalloc(len);
}

void*
operator new[](size_t len)
{
	return kmalloc(len);
}

void
operator delete(void* p) noexcept
{
	kfree(p);
}

void
operator delete[](void* p) noexcept
{
	kfree(p);
}


void
kmem_chunk_reserve(addr_t chunk_start, addr_t chunk_end, addr_t reserved_start, addr_t reserved_end, addr_t* out_start, addr_t* out_end)
{
	/*
	 * This function takes a chunk [ chunk_start .. chunk_end ] and returns up to
	 * two chunks [ out_start[n] .. out_end[n] ] such that [ reserved_start ..
	 * reserved_end ] will not have overlap in the returned chunks. This is
	 * useful when adding memory zones as the kernel parts must be removed.
	 * 
	 * There are no less than 6 cases, where:
	 *   cs = chunk_start, ce = chunk_end
	 *   rs = reserved_start, re = reserved_end
	 *
	 *                 cs            ce
   *                 +==============+
   *  (1) rs +------------------------------+ re
   *  (2)            |   rs +---------------+ re
   *  (3) rs +------------+ re      |
   *  (4)            |  rs +---+ re |
   *  (5) rs +--+ re |              |
   *  (6)            |              | rs +--+ re
	 *
	 */
	out_start[0] = chunk_start;
	out_end[0] = chunk_end;
	out_start[1] = 0;
	out_end[1] = 0;

  if (/* 5 */ chunk_start >= reserved_end ||
	    /* 6 */ chunk_end <= reserved_start) {
		/* nothing to split */
		return; 
	}

	if (/* 1 */ chunk_start >= reserved_start && chunk_end <= reserved_end) {
		out_start[0] = 0;
		out_end[0] = 0;
		return;
	}
	if (/* 2 */ chunk_start < reserved_start && chunk_end <= reserved_end) {
		out_end[0] = reserved_start;
		return;
	}
	if (/* 3 */ chunk_start >= reserved_start && chunk_end > reserved_end) {
		out_start[0] = reserved_end;
		return;
	}

	/* 4 is the tricky case - two outputs! */
	KASSERT(chunk_start <= reserved_start && chunk_end >= reserved_end, "missing case c=%x/%x r=%x/%x", chunk_start, chunk_end, reserved_start, reserved_end);
	out_end[0] = reserved_start;
	out_start[1] = reserved_end;
	out_end[1] = chunk_end;
}

/* vim:set ts=2 sw=2: */
