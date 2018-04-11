#include <ananas/types.h>
#include "kernel/console.h"
#include "kernel/lib.h"
#include "kernel/pcpu.h"
#include "kernel/process.h"
#include "kernel/thread.h"
#include "kernel/trace.h"

#define TRACE_PRINTF_BUFSIZE 256

uint32_t trace_subsystem_mask[TRACE_SUBSYSTEM_LAST + 1];

void
tracef(int fileid, const char* func, const char* fmt, ...)
{
	uint32_t timestamp = 0;
#if defined(__amd64__)
	/* XXX This should be generic somehow */
	extern uint32_t x86_get_ms_since_boot();
	timestamp = x86_get_ms_since_boot();
#endif

	char buf[TRACE_PRINTF_BUFSIZE];

	Thread* curthread = PCPU_GET(curthread);
	const char* tname = curthread->t_name;
	pid_t pid = curthread->t_process != NULL ? curthread->t_process->p_pid : -1;

	snprintf(buf, sizeof(buf), "[%4u.%03u] (%d:%s) %s: ", timestamp / 1000, timestamp % 1000, (int)pid, tname, func);

	va_list va;
	va_start(va, fmt);
	vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf) - 2, fmt, va);
	buf[sizeof(buf) - 2] = '\0';
	strcat(buf, "\n");
	va_end(va);

	console_putstring(buf);
}

/* vim:set ts=2 sw=2: */
