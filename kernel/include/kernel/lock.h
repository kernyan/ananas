/*-
 * SPDX-License-Identifier: Zlib
 *
 * Copyright (c) 2009-2019 Rink Springer <rink@rink.nu>
 * For conditions of distribution and use, see LICENSE file
 */
#ifndef __LOCK_H__
#define __LOCK_H__

#include <ananas/types.h>
#include <ananas/util/atomic.h>
#include <ananas/util/list.h>
#include "kernel/sleepqueue.h"

struct Thread;

/*
 * Spinlocks are simply locks that just keep the CPU busy waiting if they can't
 * be acquired. They can be used in any context and are designed to be light-
 * weight. They come in a normal and preemptible flavor; the latter will disable
 * interrupts. XXX It's open to debate whether this should always be the case
 */
class Spinlock final
{
  public:
    Spinlock();
    Spinlock(const Spinlock&) = delete;
    Spinlock& operator=(const Spinlock&) = delete;

    // Ordinary spinlocks which can be preempted at any time */
    void Lock();
    void Unlock();

    // Unpremptible spinlocks disable the interrupt flag while they are active
    register_t LockUnpremptible();
    void UnlockUnpremptible(register_t state);

    void AssertLocked();
    void AssertUnlocked();

  private:
    util::atomic<int> sl_var;
};

namespace detail
{
    template<typename T>
    class LockGuard final
    {
      public:
        LockGuard(T& lock) : lg_Lock(lock) { lg_Lock.Lock(); }

        ~LockGuard() { lg_Lock.Unlock(); }

        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;

      private:
        T& lg_Lock;
    };

} // namespace detail

// Simple guard for the spinlock
class SpinlockUnpremptibleGuard final
{
  public:
    SpinlockUnpremptibleGuard(Spinlock& lock) : sug_Lock(lock)
    {
        sug_State = sug_Lock.LockUnpremptible();
    }
    ~SpinlockUnpremptibleGuard() { sug_Lock.UnlockUnpremptible(sug_State); }

    SpinlockUnpremptibleGuard(const SpinlockUnpremptibleGuard&) = delete;
    SpinlockUnpremptibleGuard& operator=(const SpinlockUnpremptibleGuard&) = delete;

  private:
    Spinlock& sug_Lock;
    register_t sug_State;
};

/*
 * Semaphores are sleepable locks which guard an amount of units of a
 * particular resource.
 */
class Semaphore final
{
  public:
    Semaphore(const char* name, int count);
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    void Signal();
    void Wait();
    bool TryWait();
    void WaitAndDrain();

  private:
    util::atomic<int> sem_count;
    SleepQueue sem_sleepq;
};

/*
 * Mutexes are sleepable locks that will suspend the current thread when the
 * lock is already being held. They cannot be used from interrupt context; they
 * are implemented as binary semaphores.
 */
struct Mutex final {
  public:
    Mutex(const char* name);
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void Lock();
    void Unlock();

    bool TryLock();
    void AssertLocked();
    void AssertUnlocked();

  private:
    const char* mtx_name;
    util::atomic<Thread*> mtx_owner{nullptr};
    SleepQueue mtx_sleepq;
};

using SpinlockGuard = detail::LockGuard<Spinlock>;
using MutexGuard = detail::LockGuard<Mutex>;

#endif /* __LOCK_H__ */
