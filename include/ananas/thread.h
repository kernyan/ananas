#include <ananas/types.h>
#include <ananas/dqueue.h>
#include <ananas/vfs.h>
#include <ananas/limits.h>
#include <ananas/handle.h>
#include <ananas/init.h>
#include <ananas/page.h>
#include <ananas/schedule.h>
#include <machine/thread.h>

#ifndef __THREAD_H__
#define __THREAD_H__

/* Maximum number of handles per thread */
#define THREAD_MAX_HANDLES 64

typedef void (*kthread_func_t)(void*);
struct THREADINFO;
struct VFS_INODE;
struct VM_SPACE;

#define THREAD_EVENT_EXIT 1

DQUEUE_DEFINE(THREAD_QUEUE, thread_t);

struct THREAD_WAITER {
	semaphore_t	tw_sem;
	DQUEUE_FIELDS(struct THREAD_WAITER);
};

DQUEUE_DEFINE(THREAD_WAIT_QUEUE, struct THREAD_WAITER);

struct THREAD {
	/* Machine-dependant data - must be first */
	MD_THREAD_FIELDS

	spinlock_t t_lock;	/* Lock protecting the thread data */

	refcount_t t_refcount;		/* Reference count of the thread, >0 */

	unsigned int t_flags;
#define THREAD_FLAG_ACTIVE	0x0001	/* Thread is scheduled somewhere */
#define THREAD_FLAG_SUSPENDED	0x0002	/* Thread is currently suspended */
#define THREAD_FLAG_ZOMBIE	0x0004	/* Thread has no more resources */
#define THREAD_FLAG_RESCHEDULE	0x0008	/* Thread desires a reschedule */
#define THREAD_FLAG_REAPING	0x0010	/* Thread will be reaped (destroyed by idle thread) */
#define THREAD_FLAG_MALLOC	0x0020	/* Thread is kmalloc()'ed */
#define THREAD_FLAG_KTHREAD	0x8000	/* Kernel thread */

	unsigned int t_terminate_info;
#define THREAD_MAKE_EXITCODE(a,b) (((a) << 24) | ((b) & 0x00ffffff))
#define THREAD_TERM_SYSCALL	0x1	/* euthanasia */
#define THREAD_TERM_FAULT	0x2	/* programming fault */
#define THREAD_TERM_FAILURE	0x3	/* generic failure */

	struct VM_SPACE*	t_vmspace;	/* thread's memory space */

	int t_priority;			/* priority (0 highest) */
#define THREAD_PRIORITY_DEFAULT	200
#define THREAD_PRIORITY_IDLE	255
	int t_affinity;			/* thread CPU */
#define THREAD_AFFINITY_ANY -1

	struct PAGE* t_threadinfo_page;
	struct THREADINFO* t_threadinfo;	/* Thread startup information */

	/* Thread handles */
	handleindex_t	t_hidx_thread;	/* Handle identifying this thread */
	handleindex_t	t_hidx_path;	/* Current path */

	/* Handles */
	struct HANDLE* t_handle[THREAD_MAX_HANDLES];

	/* Waiters to signal on thread changes */
	struct THREAD_WAIT_QUEUE t_waitqueue;

	/* Scheduler specific information */
	struct SCHED_PRIV t_sched_priv;

	DQUEUE_FIELDS(thread_t);
};

/* Macro's to facilitate flag checking */
#define THREAD_IS_ACTIVE(t) ((t)->t_flags & THREAD_FLAG_ACTIVE)
#define THREAD_IS_SUSPENDED(t) ((t)->t_flags & THREAD_FLAG_SUSPENDED)
#define THREAD_IS_ZOMBIE(t) ((t)->t_flags & THREAD_FLAG_ZOMBIE)
#define THREAD_WANT_RESCHEDULE(t) ((t)->t_flags & THREAD_FLAG_RESCHEDULE)
#define THREAD_IS_KTHREAD(t) ((t)->t_flags & THREAD_FLAG_KTHREAD)

/* Machine-dependant callback to initialize a thread */
errorcode_t md_thread_init(thread_t* thread, int flags);
errorcode_t md_kthread_init(thread_t* thread, kthread_func_t func, void* arg);

/* Machine-dependant callback to free thread data */
void md_thread_free(thread_t* thread);

errorcode_t kthread_init(thread_t* t, kthread_func_t func, void* arg);

#define THREAD_ALLOC_DEFAULT	0	/* Nothing special */
#define THREAD_ALLOC_CLONE	1	/* Thread is created for cloning */

errorcode_t thread_alloc(thread_t* parent, thread_t** dest, int flags);
void thread_ref(thread_t* t);
void thread_deref(thread_t* t);
errorcode_t thread_set_args(thread_t* t, const char* args, size_t args_len);
errorcode_t thread_set_environment(thread_t* t, const char* env, size_t env_len);

thread_t* md_thread_switch(thread_t* new, thread_t* old);
void idle_thread();

void md_thread_set_entrypoint(thread_t* thread, addr_t entry);
void md_thread_set_argument(thread_t* thread, addr_t arg);
void* md_thread_map(thread_t* thread, void* to, void* from, size_t length, int flags);
errorcode_t thread_unmap(thread_t* t, addr_t virt, size_t len);
void* md_map_thread_memory(thread_t* thread, void* ptr, size_t length, int write);
void md_thread_clone(thread_t* t, thread_t* parent, register_t retval);
errorcode_t md_thread_unmap(thread_t* thread, addr_t virt, size_t length);
int md_thread_peek_32(thread_t* thread, addr_t virt, uint32_t* val);
int md_thread_is_mapped(thread_t* thread, addr_t virt, int flags, addr_t* va);

void thread_suspend(thread_t* t);
void thread_resume(thread_t* t);
void thread_exit(int exitcode);
void thread_dump(int num_args, char** arg);
errorcode_t thread_clone(thread_t* parent, int flags, thread_t** dest);

void thread_signal_waiters(thread_t* t);
void thread_wait(thread_t* t);

/*
 * Thread callback functions are provided so that modules can take action upon
 * creating or destroying of threads.
 */
typedef errorcode_t (*thread_callback_t)(thread_t* thread, thread_t* parent);
struct THREAD_CALLBACK {
	thread_callback_t tc_func;
	DQUEUE_FIELDS(struct THREAD_CALLBACK);
};
DQUEUE_DEFINE(THREAD_CALLBACKS, struct THREAD_CALLBACK);

errorcode_t thread_register_init_func(struct THREAD_CALLBACK* fn);
errorcode_t thread_register_exit_func(struct THREAD_CALLBACK* fn);
errorcode_t thread_unregister_init_func(struct THREAD_CALLBACK* fn);
errorcode_t thread_unregister_exit_func(struct THREAD_CALLBACK* fn);

#define REGISTER_THREAD_INIT_FUNC(fn) \
	static struct THREAD_CALLBACK cb_init_##fn = { .tc_func = fn }; \
	static errorcode_t register_##fn() { \
		return thread_register_init_func(&cb_init_##fn); \
	} \
	static errorcode_t unregister_##fn() { \
		return thread_unregister_init_func(&cb_init_##fn); \
	} \
	INIT_FUNCTION(register_##fn, SUBSYSTEM_THREAD, ORDER_FIRST); \
	EXIT_FUNCTION(unregister_##fn)

#define REGISTER_THREAD_EXIT_FUNC(fn) \
	static struct THREAD_CALLBACK cb_exit_##fn = { .tc_func = fn }; \
	static errorcode_t register_##fn() { \
		return thread_register_exit_func(&cb_exit_##fn); \
	} \
	static errorcode_t unregister_##fn() { \
		return thread_unregister_exit_func(&cb_exit_##fn); \
	} \
	INIT_FUNCTION(register_##fn, SUBSYSTEM_THREAD, ORDER_FIRST); \
	EXIT_FUNCTION(unregister_##fn)

#endif
