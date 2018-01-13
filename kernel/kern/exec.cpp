#include "kernel/exec.h"
#include "kernel/lib.h"
#include "kernel/init.h"
#include "kernel/trace.h"
#include "kernel/vfs/dentry.h"

TRACE_SETUP;

static struct EXEC_FORMATS exec_formats; /* XXX do we need to lock this? */

static errorcode_t
exec_init()
{
	LIST_INIT(&exec_formats);
	return ananas_success();
}

errorcode_t
exec_load(VMSpace& vs, struct DENTRY* dentry, addr_t* exec_addr, register_t* exec_arg)
{
	// Start by taking an extra ref to the dentry; this is the ref which we'll hand over
	// to the handler, if all goes well
	dentry_ref(dentry);

	LIST_FOREACH(&exec_formats, ef, struct EXEC_FORMAT) {
		/* See if we can execute this... */
		errorcode_t err = ef->ef_handler(vs, dentry, exec_addr, exec_arg);
		if (ananas_is_failure(err)) {
			/* Execute failed; try the next one */
			continue;
		}

		return ananas_success();
	}

	/* Nothing worked... return our ref */
	dentry_deref(dentry);
	return ANANAS_ERROR(BAD_EXEC);
}

INIT_FUNCTION(exec_init, SUBSYSTEM_THREAD, ORDER_FIRST);

errorcode_t
exec_register_format(struct EXEC_FORMAT* ef)
{
	LIST_APPEND(&exec_formats, ef);
	return ananas_success();
}

errorcode_t
exec_unregister_format(struct EXEC_FORMAT* ef)
{
	LIST_REMOVE(&exec_formats, ef);
	return ananas_success();
}

/* vim:set ts=2 sw=2: */
