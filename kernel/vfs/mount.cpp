#include <ananas/types.h>
#include <ananas/error.h>
#include "kernel/bio.h"
#include "kernel/device.h"
#include "kernel/kdb.h"
#include "kernel/lib.h"
#include "kernel/mm.h"
#include "kernel/trace.h"
#include "kernel/vfs/core.h"
#include "kernel/vfs/dentry.h"
#include "kernel/vfs/icache.h"
#include "options.h"

TRACE_SETUP;

namespace Ananas {
namespace VFS {
namespace {
const size_t Max_Mounted_FS = 16;
} // unnamed namespace

spinlock_t spl_mountedfs = SPINLOCK_DEFAULT_INIT;
struct VFS_MOUNTED_FS mountedfs[Max_Mounted_FS];

spinlock_t spl_fstypes = SPINLOCK_DEFAULT_INIT;
struct VFS_FILESYSTEMS fstypes;

size_t GetMaxMountedFilesystems()
{
	return Max_Mounted_FS;
}

} // namespace VFS
} // namespace Ananas

void
vfs_init_mount()
{
	memset(Ananas::VFS::mountedfs, 0, sizeof(Ananas::VFS::mountedfs));
//	LIST_INIT(&fstypes);
}

static struct VFS_MOUNTED_FS*
vfs_get_availmountpoint()
{
	spinlock_lock(&Ananas::VFS::spl_mountedfs);
	for (unsigned int n = 0; n < Ananas::VFS::Max_Mounted_FS; n++) {
		struct VFS_MOUNTED_FS* fs = &Ananas::VFS::mountedfs[n];
		if ((fs->fs_flags & VFS_FLAG_INUSE) == 0) {
			fs->fs_flags |= VFS_FLAG_INUSE;
			spinlock_unlock(&Ananas::VFS::spl_mountedfs);
			return fs;
		}
	}
	spinlock_unlock(&Ananas::VFS::spl_mountedfs);
	return NULL;
}

errorcode_t
vfs_mount(const char* from, const char* to, const char* type, void* options)
{
	errorcode_t err;

	/*
	 * Attempt to locate the filesystem type; so we know what to call to mount
	 * it.
	 */
	struct VFS_FILESYSTEM_OPS* fsops = NULL;
	spinlock_lock(&Ananas::VFS::spl_fstypes);
	LIST_FOREACH(&Ananas::VFS::fstypes, curfs, struct VFS_FILESYSTEM) {
		if (strcmp(curfs->fs_name, type) == 0) {
			/* Match */
			fsops = curfs->fs_fsops;
			break;
		}
	}
	spinlock_unlock(&Ananas::VFS::spl_fstypes);
	if (fsops == NULL)
		return ANANAS_ERROR(BAD_TYPE);

	/* Locate the device to mount from */
	Ananas::Device* dev = NULL;
	if (from != NULL) {
		dev = Ananas::DeviceManager::FindDevice(from);
		if (dev == NULL)
			return ANANAS_ERROR(NO_FILE);
	}

	/* Locate an available mountpoint and hook it up */
	struct VFS_MOUNTED_FS* fs = vfs_get_availmountpoint();
	if (fs == NULL)
		return ANANAS_ERROR(OUT_OF_HANDLES);
	fs->fs_device = dev;
	fs->fs_fsops = fsops;

	struct VFS_INODE* root_inode = NULL;
	err = fs->fs_fsops->mount(fs, &root_inode);
	if (ananas_is_failure(err)) {
		memset(fs, 0, sizeof(*fs));
		return err;
	}
	TRACE(VFS, INFO, "to='%s',fs=%p,rootinode=%p", to, fs, root_inode);

	KASSERT(root_inode != NULL, "successful mount without a root inode");
	KASSERT(S_ISDIR(root_inode->i_sb.st_mode), "root inode isn't a directory");
	KASSERT(root_inode->i_refcount == 1, "bad refcount of root inode (must be 1, is %u)", root_inode->i_refcount);
	fs->fs_mountpoint = strdup(to);
	fs->fs_root_dentry = &dcache_create_root_dentry(fs);
	fs->fs_root_dentry->d_inode = root_inode; /* don't deref - we're giving the ref to the root dentry */

	/*	
	 * Now perform a root entry lookup; if this fails, we'll have to abort everything we have done before
	 * XXX Maybe special-case once we have a root filesystem?
	 */
	if (ananas_is_failure(err)) {
		memset(fs, 0, sizeof(*fs));
		return err;
	}

	/*
	 * Override the root path dentry with our root inode; this effectively hooks
	 * our filesystem to the parent. XXX I wonder if this is correct; we should
	 * always just hook our path to the fs root dentry... need to think about it
	 */
	DEntry* dentry_root = fs->fs_root_dentry;
	if (ananas_is_success(vfs_lookup(NULL, dentry_root, to)) &&
	    dentry_root != fs->fs_root_dentry) {
		if (dentry_root->d_inode != NULL)
			vfs_deref_inode(dentry_root->d_inode);
		vfs_ref_inode(root_inode);
		dentry_root->d_inode = root_inode;
	}
	
	return ananas_success();
}

void
vfs_abandon_device(Ananas::Device& device)
{
	spinlock_lock(&Ananas::VFS::spl_mountedfs);
	for (unsigned int n = 0; n < Ananas::VFS::Max_Mounted_FS; n++) {
		struct VFS_MOUNTED_FS* fs = &Ananas::VFS::mountedfs[n];
		if ((fs->fs_flags & VFS_FLAG_INUSE) == 0 || (fs->fs_flags & VFS_FLAG_ABANDONED) || fs->fs_device != &device)
			continue;

		/*
		 * This filesystem can no longer operate sanely - ensure all requests for it are
		 * rejected instead of serviced.
		 *
		 * XXX We should at least start the unmount process here.
		 */
		 fs->fs_flags |= VFS_FLAG_ABANDONED;
		 fs->fs_device = nullptr;
	}
	spinlock_unlock(&Ananas::VFS::spl_mountedfs);
}

errorcode_t
vfs_unmount(const char* path)
{
	panic("fix me");

	spinlock_lock(&Ananas::VFS::spl_mountedfs);
	for (unsigned int n = 0; n < Ananas::VFS::Max_Mounted_FS; n++) {
		struct VFS_MOUNTED_FS* fs = &Ananas::VFS::mountedfs[n];
		if ((fs->fs_flags & VFS_FLAG_INUSE) && (fs->fs_mountpoint != NULL) && (strcmp(fs->fs_mountpoint, path) == 0)) {
			/* Got it; disown it immediately. XXX What about pending inodes etc? */
			fs->fs_mountpoint = NULL;
			spinlock_unlock(&Ananas::VFS::spl_mountedfs);
			/* XXX Ask filesystem politely to unmount */
			fs->fs_flags = 0; /* Available */
			return ananas_success();
		}
	}
	spinlock_unlock(&Ananas::VFS::spl_mountedfs);
	return ANANAS_ERROR(BAD_HANDLE); /* XXX */
}

struct VFS_MOUNTED_FS*
vfs_get_rootfs()
{
	/* XXX the root fs should have a flag marking it as such */
	spinlock_lock(&Ananas::VFS::spl_mountedfs);
	for (unsigned int n = 0; n < Ananas::VFS::Max_Mounted_FS; n++) {
		struct VFS_MOUNTED_FS* fs = &Ananas::VFS::mountedfs[n];
		if ((fs->fs_flags & VFS_FLAG_INUSE) && (fs->fs_mountpoint != NULL) && (strcmp(fs->fs_mountpoint, "/") == 0)) {
			spinlock_unlock(&Ananas::VFS::spl_mountedfs);
			return fs;
		}
	}
	spinlock_unlock(&Ananas::VFS::spl_mountedfs);
	return NULL;
}

errorcode_t
vfs_register_filesystem(struct VFS_FILESYSTEM* fs)
{
	spinlock_lock(&Ananas::VFS::spl_fstypes);
	/* Ensure the filesystem is not already registered */
	LIST_FOREACH(&Ananas::VFS::fstypes, curfs, struct VFS_FILESYSTEM) {
		if (strcmp(curfs->fs_name, fs->fs_name) == 0) {
			/* Duplicate filesystem type; refuse to register */
			spinlock_unlock(&Ananas::VFS::spl_fstypes);
			return ANANAS_ERROR(FILE_EXISTS);
		}
	}

	/* Filesystem is clear; hook it up */
	LIST_APPEND(&Ananas::VFS::fstypes, fs);
	spinlock_unlock(&Ananas::VFS::spl_fstypes);
	return ananas_success();
}

errorcode_t
vfs_unregister_filesystem(struct VFS_FILESYSTEM* fs)
{
	spinlock_lock(&Ananas::VFS::spl_fstypes);
	LIST_REMOVE(&Ananas::VFS::fstypes, fs);
	spinlock_unlock(&Ananas::VFS::spl_fstypes);
	return ananas_success();
}

#ifdef OPTION_KDB
KDB_COMMAND(mounts, NULL, "Shows current mounts")
{
	spinlock_lock(&Ananas::VFS::spl_mountedfs);
	for (unsigned int n = 0; n < Ananas::VFS::Max_Mounted_FS; n++) {
		struct VFS_MOUNTED_FS* fs = &Ananas::VFS::mountedfs[n];
		if ((fs->fs_flags & VFS_FLAG_INUSE) == 0)
			continue;
		kprintf(">> vfs=%p, flags=0x%x, mountpoint='%s'\n", fs, fs->fs_flags, fs->fs_mountpoint);
	}
	spinlock_unlock(&Ananas::VFS::spl_mountedfs);
}
#endif /* KDB */

/* vim:set ts=2 sw=2: */
