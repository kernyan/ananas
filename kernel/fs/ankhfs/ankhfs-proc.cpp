#include <ananas/types.h>
#include <ananas/errno.h>
#include <ananas/procinfo.h>
#include "kernel/lib.h"
#include "kernel/process.h"
#include "kernel/result.h"
#include "kernel/trace.h"
#include "kernel/vfs/core.h"
#include "kernel/vfs/dentry.h"
#include "kernel/vfs/generic.h"
#include "kernel/vm.h"
#include "kernel/vmspace.h"
#include "proc.h"
#include "support.h"

TRACE_SETUP;

namespace process {

extern Mutex process_mtx;
extern process::ProcessList process_all;

} // namespace process

namespace Ananas {
namespace AnkhFS {

namespace {

constexpr unsigned int subName = 1;
constexpr unsigned int subVmSpace = 2;

struct DirectoryEntry proc_entries[] = {
	{ "name", make_inum(SS_Proc, 0, subName) },
	{ "vmspace", make_inum(SS_Proc, 0, subVmSpace) },
	{ NULL, 0 }
};

Result
HandleReadDir_Proc_Root(struct VFS_FILE* file, void* dirents, size_t* len)
{
	struct FetchEntry : IReadDirCallback {
		bool FetchNextEntry(char* entry, size_t maxLength, ino_t& inum) override {
			if (currentProcess == process::process_all.end())
				return false;

			// XXX we should lock currentProcess here
			snprintf(entry, maxLength, "%d", static_cast<int>(currentProcess->p_pid));
			inum = make_inum(SS_Proc, currentProcess->p_pid, 0);
			++currentProcess;
			return true;
		}

		process::ProcessList::iterator currentProcess = process::process_all.begin();
	};

	// Fill the root directory with one directory per process ID
	MutexGuard g(process::process_mtx);
	FetchEntry entryFetcher;
	return HandleReadDir(file, dirents, len, entryFetcher);
}

class ProcSubSystem : public IAnkhSubSystem
{
public:
	Result HandleReadDir(struct VFS_FILE* file, void* dirents, size_t* len) override
	{
		INode& inode = *file->f_dentry->d_inode;
		ino_t inum = inode.i_inum;

		if (inum_to_id(inum) == 0)
			return HandleReadDir_Proc_Root(file, dirents, len);

		return AnkhFS::HandleReadDir(file, dirents, len, proc_entries[0], inum_to_id(inum));
	}

	Result FillInode(INode& inode, ino_t inum) override
	{
		if (inum_to_sub(inum) == 0) {
			inode.i_sb.st_mode |= S_IFDIR;
		} else {
			inode.i_sb.st_mode |= S_IFREG;
		}
		return Result::Success();
	}

	Result HandleRead(struct VFS_FILE* file, void* buf, size_t* len) override
	{
		ino_t inum = file->f_dentry->d_inode->i_inum;

		pid_t pid = static_cast<pid_t>(inum_to_id(inum));
		Process* p = process_lookup_by_id_and_ref(pid);
		if (p == nullptr)
			return RESULT_MAKE_FAILURE(EIO);

		char result[256]; // XXX
		strcpy(result, "???");
		switch(inum_to_sub(inum)) {
			case subName: {
				if (p->p_info != nullptr)
					strncpy(result, p->p_info->pi_args, sizeof(result));
				break;
			}
			case subVmSpace: {
				if (p->p_vmspace != nullptr) {
					// XXX shouldn't we lock something here?'
					char* r = result;
					for(const auto& va: p->p_vmspace->vs_areas) {
						snprintf(r, sizeof(result) - (r - result), "%p %p %c%c%c\n",
						 reinterpret_cast<void*>(va.va_virt), reinterpret_cast<void*>(va.va_len),
						 (va.va_flags & VM_FLAG_READ) ? 'r' : '-',
						 (va.va_flags & VM_FLAG_WRITE) ? 'w' : '-',
						 (va.va_flags & VM_FLAG_EXECUTE) ? 'x' : '-');
						r += strlen(r);
					}
				}
				break;
			}
		}
		result[sizeof(result) - 1] = '\0';
		p->Deref();
		return AnkhFS::HandleRead(file, buf, len, result);
	}
};

} // unnamed namespace

IAnkhSubSystem& GetProcSubSystem()
{
	static ProcSubSystem procSubSystem;
	return procSubSystem;
}

} // namespace AnkhFS
} // namespace Ananas

/* vim:set ts=2 sw=2: */
