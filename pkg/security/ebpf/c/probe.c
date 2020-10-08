#include <linux/compiler.h>

#include <linux/kconfig.h>
#include <linux/ptrace.h>
#include <linux/types.h>
#include <linux/version.h>

#include "defs.h"
#include "dentry.h"
#include "exec.h"
#include "process.h"
#include "container.h"
#include "setattr.h"
#include "mnt.h"
#include "filename.h"
#include "chmod.h"
#include "chown.h"
#include "mkdir.h"
#include "rmdir.h"
#include "unlink.h"
#include "rename.h"
#include "cgroup.h"
#include "open.h"
#include "utimes.h"
#include "mount.h"
#include "umount.h"
#include "link.h"
#include "raw_syscalls.h"
#include "getattr.h"
#include "setxattr.h"

void __attribute__((always_inline)) remove_inode_discarders(struct file_t *file) {
    struct path_key_t path_key = {
        .ino = file->inode,
        .mount_id = file->mount_id,
    };

#pragma unroll
    for (int i = 1; i < EVENT_MAX; i++) {
        remove_inode_discarder(i, path_key);
    }
}

void __attribute__((always_inline)) remove_pid_discarders(u32 tgid) {
#pragma unroll
    for (int i = 1; i < EVENT_MAX; i++) {
        remove_pid_discarder(i, tgid);
    }
}

__u32 _version SEC("version") = 0xFFFFFFFE;

char LICENSE[] SEC("license") = "GPL";
