/*-
 * SPDX-License-Identifier: Zlib
 *
 * Copyright (c) 2009-2018 Rink Springer <rink@rink.nu>
 * For conditions of distribution and use, see LICENSE file
 */
#include <ananas/types.h>
#include <ananas/errno.h>
#include <ananas/util/utility.h>
#include "kernel/kmem.h"
#include "kernel/lib.h"
#include "kernel/result.h"
#include "kernel/vmarea.h"
#include "kernel/vmspace.h"
#include "kernel/vmpage.h"
#include "kernel/vfs/core.h"
#include "kernel/vfs/dentry.h"
#include "kernel/vm.h"

#include "kernel/process.h"

namespace
{
    Result read_data(DEntry& dentry, void* buf, off_t offset, size_t len)
    {
        struct VFS_FILE f;
        memset(&f, 0, sizeof(f));
        f.f_dentry = &dentry;

        if (auto result = vfs_seek(&f, offset); result.IsFailure())
            return result;

        auto result = vfs_read(&f, buf, len);
        if (result.IsFailure())
            return result;

        auto numread = result.AsValue();
        if (numread != len)
            return Result::Failure(EIO);
        return Result::Success();
    }

    int DeterminePageFlagsFromVMArea(const VMArea& va)
    {
        int flags = 0;
        if ((va.va_flags & (VM_FLAG_READ | VM_FLAG_WRITE)) == VM_FLAG_READ) {
            flags |= vmpage::flag::ReadOnly;
        }
        return flags;
    }

    void AssignPageToVirtualAddress(VMSpace& vs, VMArea& va, const VAInterval& interval, const addr_t virt, VMPage& vmpage)
    {
        const auto page_index = (virt - interval.begin) / PAGE_SIZE;
        auto& vp = va.va_pages[page_index];
        if (vp && vp != &vmpage) {
            vp->Lock();
            vp->Deref();
        }
        vp = &vmpage;
        vmpage.Map(vs, va, virt);
    }

    util::locked<VMPage> GetDEntryBackedPage(VMArea& va, const off_t read_off)
    {
        auto vmpage = vmpage::LookupOrCreateINodePage(
                *va.va_dentry->d_inode, read_off,
                vmpage::flag::Pending | DeterminePageFlagsFromVMArea(va));
        if ((vmpage->vp_flags & vmpage::flag::Pending) == 0)
            return vmpage;

        // Read the page - note that we hold the vmpage lock while doing this
        Page* p;
        void* page = page_alloc_single_mapped(p, VM_FLAG_READ | VM_FLAG_WRITE);
        KASSERT(p != nullptr, "out of memory"); // XXX handle this

        size_t read_length = PAGE_SIZE;
        if (read_off + read_length > va.va_dentry->d_inode->i_sb.st_size) {
            // This inode is simply not long enough to cover our read - adjust XXX what when it
            // grows?
            read_length = va.va_dentry->d_inode->i_sb.st_size - read_off;
            // Zero out everything after the part we will read so we don't leak any data
            memset(static_cast<char*>(page) + read_length, 0, PAGE_SIZE - read_length);
        }

        const auto result = read_data(*va.va_dentry, page, read_off, read_length);
        kmem_unmap(page, PAGE_SIZE);
        KASSERT(result.IsSuccess(), "cannot deal with error %d", result.AsStatusCode()); // XXX

        // Update the vm page to contain our new address
        vmpage->vp_page = p;
        vmpage->vp_flags &= ~vmpage::flag::Pending;
        return vmpage;
    }

    VMPage& PromotePage(VMPage& vp)
    {
        auto& new_vp = vp.Promote();
        if (&new_vp != &vp) vp.Deref();
        return new_vp;
    }

    bool HandleDEntryBackedFault(VMSpace& vs, VMArea& va, const VAInterval& interval, const addr_t alignedVirt)
    {
        /*
         * The way dentries are mapped to virtual address is:
         *
         * 0       va_doffset                               file length
         * +------------+-------------+-------------------------------+
         * |            |XXXXXXXXXXXXX|                               |
         * |            |XXXXXXXXXXXXX|                               |
         * +------------+-------------+-------------------------------+
         *             /     |||      \ va_doffset + va_dlength
         *            /      vvv
         *     +-------------+---------------+
         *     |XXXXXXXXXXXXX|000000000000000|
         *     |XXXXXXXXXXXXX|000000000000000|
         *     +-------------+---------------+
         *     0            \
         *                   \
         *                    va_dlength
         */
        const auto read_off = alignedVirt - va.va_virt; // offset in area, still needs va_doffset added
        if (read_off >= va.va_dlength)
            return false; // outside of dentry; must be zero-filled

        // At least (part of) the page is to be read from the backing dentry -
        // this means we want the entire page
        auto vmpage = GetDEntryBackedPage(va, read_off + va.va_doffset);

        // If the mapping is page-aligned and read-only or shared, we can re-use the
        // mapping and avoid the entire copy
        const auto can_reuse_page_as_is =
            // Reusing means the page resides in the section...
            (read_off + PAGE_SIZE) <= va.va_dlength &&
            // ... and we have a page-aligned offset
            (va.va_doffset & (PAGE_SIZE - 1)) == 0;
        VMPage* new_vp;
        if (can_reuse_page_as_is) {
            // Just clone the page; it could both be an inode-backed page (if
            // this is a private COW) or vmspace-backed if we are COW-ing from a
            // parent
            new_vp = &vmpage->Duplicate();
        } else {
            // Cannot re-use; create a new VM page, with appropriate flags based on the va
            new_vp = &vmpage::AllocatePrivatePage(
                vmpage::flag::Private | DeterminePageFlagsFromVMArea(va));

            // Now copy the parts of the dentry-backed page
            size_t copy_len =
                va.va_dlength - read_off; // this is size-left after where we read
            if (copy_len > PAGE_SIZE)
                copy_len = PAGE_SIZE;
            vmpage->CopyExtended(*new_vp, copy_len);
        }
        vmpage.Unlock();

        AssignPageToVirtualAddress(vs, va, interval, alignedVirt, *new_vp);
        new_vp->Unlock();
        return true;
    }
} // unnamed namespace


void DumpVMSpace(VMSpace& vmspace)
{
    auto prev = 0;
    for (auto& [ interval, va ] : vmspace.vs_areamap) {
        kprintf(
            "[%p..%p) (%p..%p) %c%c%c\n",
            interval.begin,
            interval.end,
            reinterpret_cast<void*>(va->va_virt),
            reinterpret_cast<void*>(va->va_virt + va->va_len - 1),
            (va->va_flags & VM_FLAG_READ) ? 'r' : '-',
            (va->va_flags & VM_FLAG_WRITE) ? 'w' : '-',
            (va->va_flags & VM_FLAG_EXECUTE) ? 'x' : '-');
        if (prev == interval.begin) {
            panic("DUP");
        }
        prev = interval.begin;
    }
    kprintf("dump end\n");
}

Result VMSpace::HandleFault(addr_t virt, const int fault_flags)
{
    //kprintf(">> HandleFault(): vs=%p, virt=%p, flags=0x%x\n", this, virt, fault_flags);

    // Walk through the areas one by one
    for (auto& [interval, va ] : vs_areamap) {
        if (!(virt >= va->va_virt && (virt < (va->va_virt + va->va_len))))
            continue;

        // See if we have this page mapped
        const auto alignedVirt = virt & ~(PAGE_SIZE - 1);
        if (auto vp = va->LookupVAddrAndLock(alignedVirt); vp != nullptr) {
            if ((fault_flags & VM_FLAG_WRITE) && (va->va_flags & vmarea::flag::COW)) {
                // Write to a COW page; promote the page and re-map it
                KASSERT((vp->vp_flags & vmpage::flag::ReadOnly) == 0, "cowing r/o page");

                kprintf("%d: promoting page for %p\n", process::GetCurrent().p_pid, alignedVirt);
                auto& new_vp = PromotePage(*vp);
                AssignPageToVirtualAddress(*this, *va, interval, alignedVirt, new_vp);
                new_vp.Unlock();
                return Result::Success();
            }

            // Page is already mapped, but not COW. Bad, reject
            kprintf("write to non-cow, reject %p\n", alignedVirt);
            vp->Unlock();
            return Result::Failure(EFAULT);
        }

        // XXX we expect va_doffset to be page-aligned here (i.e. we can always use a page directly)
        // this needs to be enforced when making mappings!
        KASSERT(
            (va->va_doffset & (PAGE_SIZE - 1)) == 0, "doffset %x not page-aligned",
            (int)va->va_doffset);

        // If there is a dentry attached here, perhaps we may find what we need in the corresponding
        // inode
        if (va->va_dentry != nullptr && HandleDEntryBackedFault(*this, *va, interval, alignedVirt))
            return Result::Success();

        // We need a new VM page here; this is an anonymous mapping which we need to back
        auto& new_vp = vmpage::AllocatePrivatePage(vmpage::flag::Private);
        new_vp.Zero(*this, *va, alignedVirt);
        AssignPageToVirtualAddress(*this, *va, interval, alignedVirt, new_vp);
        new_vp.Unlock();
        return Result::Success();
    }

    return Result::Failure(EFAULT);
}
