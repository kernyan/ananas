#include <ananas/types.h>
#include <ananas/error.h>
#include "kernel/trace.h"

TRACE_SETUP;

struct Thread;

errorcode_t
sys_unlink(Thread* t, const char* path)
{
	TRACE(SYSCALL, FUNC, "t=%p, path='%s'", t, path);

	/* TODO */
	return ANANAS_ERROR(BAD_OPERATION);
}

