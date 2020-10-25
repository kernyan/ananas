/*-
 * SPDX-License-Identifier: Zlib
 *
 * Copyright (c) 2009-2018 Rink Springer <rink@rink.nu>
 * For conditions of distribution and use, see LICENSE file
 */
/*
 * Threads have a current state which is contained in t_flags; possible
 * transitions are:
 *
 *  +-->[suspended]->-+
 *  |       |         |
 *  |       v         |
 *  +-<--[active]     |
 *          |         |
 *          v         |
 *       [zombie]<----+
 *          |
 *          v
 *       [(gone)]
 *
 * All transitions are managed by scheduler.cpp.
 */
#include <ananas/types.h>
#include "kernel/device.h"
#include "kernel/kmem.h"
#include "kernel/lib.h"
#include "kernel/mm.h"
#include "kernel/pcpu.h"
#include "kernel/process.h"
#include "kernel/reaper.h"
#include "kernel/result.h"
#include "kernel/schedule.h"
#include "kernel/time.h"
#include "kernel/trace.h"
#include "kernel/thread.h"
#include "kernel/vm.h"
#include "kernel-md/interrupts.h"
#include "kernel-md/md.h"
#include "options.h"

TRACE_SETUP;

namespace {
Spinlock spl_threadqueue;
thread::AllThreadsList allThreads;
}

Result thread_alloc(Process& p, Thread*& dest, const char* name, int flags)
{
    /* First off, allocate the thread itself */
    auto t = new Thread;
    memset(t, 0, sizeof(Thread));
    p.AddThread(*t);
    t->t_sched_flags = 0;
    t->t_flags = THREAD_FLAG_ALLOC;
    t->t_refcount = 1; /* caller */
    t->SetName(name);

    /* Set up CPU affinity and priority */
    t->t_priority = THREAD_PRIORITY_DEFAULT;
    t->t_affinity = THREAD_AFFINITY_ANY;

    /* Ask machine-dependant bits to initialize our thread data */
    md::thread::InitUserlandThread(*t, flags);

    /* If we don't yet have a main thread, this thread will become the main */
    if (p.p_mainthread == NULL)
        p.p_mainthread = t;

    /* Initialize scheduler-specific parts */
    scheduler::InitThread(*t);

    /* Add the thread to the thread queue */
    {
        SpinlockGuard g(spl_threadqueue);
        allThreads.push_back(*t);
    }

    dest = t;
    return Result::Success();
}

Result kthread_init(Thread& t, const char* name, kthread_func_t func, void* arg)
{
    /*
     * Kernel threads do not have an associated process, and thus no handles,
     * vmspace and the like.
     */
    memset(&t, 0, sizeof(Thread));
    t.t_sched_flags = 0;
    t.t_flags = THREAD_FLAG_KTHREAD;
    t.t_refcount = 1;
    t.t_priority = THREAD_PRIORITY_DEFAULT;
    t.t_affinity = THREAD_AFFINITY_ANY;
    t.SetName(name);

    /* Initialize MD-specifics */
    md::thread::InitKernelThread(t, func, arg);

    /* Initialize scheduler-specific parts */
    scheduler::InitThread(t);

    /* Add the thread to the thread queue */
    {
        SpinlockGuard g(spl_threadqueue);
        allThreads.push_back(t);
    }
    return Result::Success();
}

/*
 * thread_cleanup() informs everyone about the thread's demise.
 */
static void thread_cleanup(Thread& t)
{
    Process* p = t.t_process;
    KASSERT(!t.IsZombie(), "cleaning up zombie thread %p", &t);
    KASSERT(t.IsKernel() || p != nullptr, "thread without process");

    /*
     * Signal anyone waiting on the thread; the terminate information should
     * already be set at this point - note that handle_wait() will do additional
     * checks to ensure the thread is truly gone.
     */
    t.SignalWaiters();
}

/*
 * thread_destroy() take a zombie thread and completely frees it (the thread
 * will not be valid after calling this function and thus this can only be
 * called from a different thread)
 */
void Thread::Destroy()
{
    KASSERT(PCPU_GET(curthread) != this, "thread_destroy() on current thread");
    KASSERT(IsZombie(), "thread_destroy() on a non-zombie thread");

    /* Free the machine-dependant bits */
    md::thread::Free(*this);

    // Unregister ourselves with the owning process
    if (t_process != nullptr) {
        t_process->RemoveThread(*this);
    }

    /* If we aren't reaping the thread, remove it from our thread queue; it'll be gone soon */
    KASSERT((t_flags & THREAD_FLAG_REAPING) == 0, "delete-ing with reaper?");
    {
        SpinlockGuard g(spl_threadqueue);
        allThreads.remove(*this);
    }

    if (t_flags & THREAD_FLAG_ALLOC)
        delete this;
    else
        panic("TODO delete non-alloced threads");
}

#if 0
void Thread::Ref()
{
    KASSERT(t_refcount > 0, "reffing thread with invalid refcount %d", t_refcount);
    ++t_refcount;
}

void Thread::Deref()
{
    KASSERT(t_refcount > 0, "dereffing thread with invalid refcount %d", t_refcount);

    /*
     * Thread cleanup is quite involved - we need to take the following into account:
     *
     * 1) A thread itself calling Deref() can demote the thread to zombie-state
     *    This is because the owner decides to stop the thread, which means we can free
     *    most associated resources.
     * 2) The new refcount would become 0
     *    We can go to zombie-state if we aren't already. However:
     *    2a) If curthread == 't', we can't destroy because we are using the
     *        threads stack. We'll postpone destruction to another thread.
     *    2b) Otherwise, we can destroy 't' and be done.
     * 3) Otherwise, the refcount > 0 and we must let the thread live.
     */
    const bool is_self = PCPU_GET(curthread) == this;
    KASSERT(!is_self, "deref self?");
    if (--t_refcount > 0) {
        /* (3) - refcount isn't yet zero so nothing to destroy */
        return;
    }

#if 0
    if (is_self) {
        /*
         * (2a) - mark the thread as reaping, and increment the refcount
         *        so that we can just Deref() it
         */
        t_flags |= THREAD_FLAG_REAPING;
        t_refcount++;

        // Remove the thread from all threads queue
        {
            SpinlockGuard g(spl_threadqueue);
            allThreads.remove(*this);
        }

        /*
         * Ask the reaper to nuke the thread if it was a kernel thread; userland
         * stuff gets terminated using the wait() family of functions.
         */
        if (IsKernel())
            reaper_enqueue(*this);
        return;
    }
#endif

    /* (2b) Final ref - let's get rid of it */
    if (!IsZombie())
        thread_cleanup(*this);
    thread_destroy(*this);
}
#endif

void Thread::Suspend()
{
    TRACE(THREAD, FUNC, "t=%p", this);
    KASSERT(!IsSuspended(), "suspending suspended thread %p", this);
    KASSERT(this != PCPU_GET(idlethread), "suspending idle thread");
    scheduler::SuspendThread(*this);
}

void thread_sleep_ms(unsigned int ms)
{
    unsigned int msPerTick = 1000 / time::GetPeriodicyInHz();
    tick_t num_ticks = ms / msPerTick;
    if (num_ticks == 0)
        num_ticks = 1; // delay at least one tick

    Thread* t = PCPU_GET(curthread);
    t->t_timeout = time::GetTicks() + num_ticks;
    t->t_flags |= THREAD_FLAG_TIMEOUT;
    t->Suspend();
    scheduler::Schedule();
}

void Thread::Resume()
{
    TRACE(THREAD, FUNC, "t=%p", this);

    scheduler::ResumeThread(*this);
}

void Thread::Terminate(int exitcode)
{
    KASSERT(this == PCPU_GET(curthread), "terminate not on current thread");
    KASSERT(!IsZombie(), "exiting zombie thread");

    Process* p = t_process;
    if (p != nullptr) {
        // Grab the process lock; this ensures WaitAndLock() will be blocked until the
        // thread is completely forgotten by the scheduler
        p->Lock();

        // If we are the process' main thread, mark it as exiting
        if (p->p_mainthread == this)
            p->Exit(exitcode);
    }

    // ...
    thread_cleanup(*this);
    --t_refcount;

    // Ask the scheduler to exit the thread (this transitions to zombie-state)
    scheduler::ExitThread(*this);
    if (p != nullptr) {
        // Signal parent in case it is waiting for a child to exit
        p->SignalExit();
        p->Unlock();
    }

    scheduler::Schedule();
    /* NOTREACHED */
}

void Thread::SetName(const char* name)
{
    if (IsKernel()) {
        /* Surround kernel thread names by [ ] to clearly identify them */
        snprintf(t_name, THREAD_MAX_NAME_LEN, "[%s]", name);
    } else {
        strncpy(t_name, name, THREAD_MAX_NAME_LEN);
    }
    t_name[THREAD_MAX_NAME_LEN] = '\0';
}

Result thread_clone(Process& proc, Thread*& out_thread)
{
    TRACE(THREAD, FUNC, "proc=%p", &proc);
    Thread* curthread = PCPU_GET(curthread);

    Thread* t;
    if (auto result = thread_alloc(proc, t, curthread->t_name, THREAD_ALLOC_CLONE);
        result.IsFailure())
        return result;

    /*
     * Must copy the thread state over; note that this is the
     * result of a system call, so we want to influence the
     * return value.
     */
    md::thread::Clone(*t, *curthread, 0 /* child gets exit code zero */);

    /* Thread is ready to rock */
    out_thread = t;
    return Result::Success();
}

struct ThreadWaiter : util::List<ThreadWaiter>::NodePtr {
    Semaphore tw_sem{"thread-waiter", 0};
};

void Thread::SignalWaiters()
{
    SpinlockGuard g(t_lock);
    while (!t_waitqueue.empty()) {
        auto& tw = t_waitqueue.front();
        t_waitqueue.pop_front();

        tw.tw_sem.Signal();
    }
}

void Thread::Wait()
{
    ThreadWaiter tw;

    {
        SpinlockGuard g(t_lock);
        t_waitqueue.push_back(tw);
    }

    tw.tw_sem.Wait();
    /* 'tw' will be removed by SignalWaiters() */
}

void idle_thread(void*)
{
    while (1) {
        md::interrupts::Relax();
    }
}
