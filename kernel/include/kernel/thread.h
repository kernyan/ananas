#ifndef __THREAD_H__
#define __THREAD_H__

#include <ananas/types.h>
#include <ananas/util/list.h>
#include "kernel/handle.h"
#include "kernel/page.h"
#include "kernel/schedule.h" // for SchedulerPriv
#include "kernel/vfs/generic.h"
#include "kernel-md/thread.h"

typedef void (*kthread_func_t)(void*);
struct VFS_INODE;
struct VM_SPACE;
struct STACKFRAME;

#define THREAD_MAX_NAME_LEN 32
#define THREAD_EVENT_EXIT 1

struct ThreadWaiter : util::List<ThreadWaiter>::NodePtr {
	semaphore_t	tw_sem;
};

typedef util::List<ThreadWaiter> ThreadWaiterList;

struct Thread : public util::List<Thread>::NodePtr {
	/* Machine-dependant data - must be first */
	MD_THREAD_FIELDS

	spinlock_t t_lock;	/* Lock protecting the thread data */
	char t_name[THREAD_MAX_NAME_LEN + 1];	/* Thread name */

	refcount_t t_refcount;		/* Reference count of the thread, >0 */

	unsigned int t_flags;
#define THREAD_FLAG_ACTIVE	0x0001	/* Thread is scheduled somewhere */
#define THREAD_FLAG_SUSPENDED	0x0002	/* Thread is currently suspended */
#define THREAD_FLAG_ZOMBIE	0x0004	/* Thread has no more resources */
#define THREAD_FLAG_RESCHEDULE	0x0008	/* Thread desires a reschedule */
#define THREAD_FLAG_REAPING	0x0010	/* Thread will be reaped (destroyed by idle thread) */
#define THREAD_FLAG_MALLOC	0x0020	/* Thread is kmalloc()'ed */
#define THREAD_FLAG_TIMEOUT	0x0040	/* Timeout field is valid */
#define THREAD_FLAG_KTHREAD	0x8000	/* Kernel thread */

	struct STACKFRAME* t_frame;
	unsigned int t_md_flags;

	unsigned int t_terminate_info;
#define THREAD_MAKE_EXITCODE(a,b) (((a) << 24) | ((b) & 0x00ffffff))
#define THREAD_TERM_SYSCALL	0	/* euthanasia */
#define THREAD_TERM_SIGNAL 1	/* terminated by signal */

	struct PROCESS*		t_process;	/* associated process */

	int t_priority;			/* priority (0 highest) */
#define THREAD_PRIORITY_DEFAULT	200
#define THREAD_PRIORITY_IDLE	255
	int t_affinity;			/* thread CPU */
#define THREAD_AFFINITY_ANY -1

	/* Thread handles */
	handleindex_t	t_hidx_thread;	/* Handle identifying this thread */

	/* Waiters to signal on thread changes */
	ThreadWaiterList	t_waitqueue;

	/* Timeout, when it expires the thread will be scheduled in */
	tick_t			t_timeout;

	/* Scheduler specific information */
	SchedulerPriv		t_sched_priv;
};

typedef util::List<Thread> ThreadList;

/* Macro's to facilitate flag checking */
#define THREAD_IS_ACTIVE(t) ((t)->t_flags & THREAD_FLAG_ACTIVE)
#define THREAD_IS_SUSPENDED(t) ((t)->t_flags & THREAD_FLAG_SUSPENDED)
#define THREAD_IS_ZOMBIE(t) ((t)->t_flags & THREAD_FLAG_ZOMBIE)
#define THREAD_WANT_RESCHEDULE(t) ((t)->t_flags & THREAD_FLAG_RESCHEDULE)
#define THREAD_IS_KTHREAD(t) ((t)->t_flags & THREAD_FLAG_KTHREAD)

/* Machine-dependant callback to initialize a thread */
errorcode_t md_thread_init(Thread& thread, int flags);
errorcode_t md_kthread_init(Thread& thread, kthread_func_t func, void* arg);

/* Machine-dependant callback to free thread data */
void md_thread_free(Thread& thread);

errorcode_t kthread_init(Thread& t, const char* name, kthread_func_t func, void* arg);

#define THREAD_ALLOC_DEFAULT	0	/* Nothing special */
#define THREAD_ALLOC_CLONE	1	/* Thread is created for cloning */

errorcode_t thread_alloc(process_t* p, Thread*& dest, const char* name, int flags);
void thread_ref(Thread& t);
void thread_deref(Thread& t);
void thread_set_name(Thread& t, const char* name);

Thread& md_thread_switch(Thread& new_thread, Thread& old_thread);
void idle_thread(void*);

void md_thread_set_entrypoint(Thread& thread, addr_t entry);
void md_thread_set_argument(Thread& thread, addr_t arg);
void* md_thread_map(Thread& thread, void* to, void* from, size_t length, int flags);
errorcode_t thread_unmap(Thread& t, addr_t virt, size_t len);
void* md_map_thread_memory(Thread& thread, void* ptr, size_t length, int write);
void md_thread_clone(Thread& t, Thread& parent, register_t retval);
errorcode_t md_thread_unmap(Thread& thread, addr_t virt, size_t length);
void md_setup_post_exec(Thread& thread, addr_t exec_addr, register_t exec_arg);

void thread_suspend(Thread& t);
void thread_resume(Thread& t);
void thread_sleep(tick_t num_ticks);
void thread_exit(int exitcode);
void thread_dump(int num_args, char** arg);
errorcode_t thread_clone(process_t* proc, Thread*& dest);

void thread_signal_waiters(Thread& t);
void thread_wait(Thread& t);

#endif
