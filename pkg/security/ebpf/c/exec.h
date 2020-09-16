#ifndef _EXEC_H_
#define _EXEC_H_

#include "filters.h"
#include "syscalls.h"
#include "container.h"

struct exec_event_t {
    struct kevent_t event;
    struct proc_cache_t cache_entry;
    u32 pid;
    u32 padding;
};

struct exit_event_t {
    struct kevent_t event;
    u32 pid;
    u32 padding;
};

struct _tracepoint_sched_process_fork
{
    unsigned short common_type;
    unsigned char common_flags;
    unsigned char common_preempt_count;
    int common_pid;

    char parent_comm[16];
    pid_t parent_pid;
    char child_comm[16];
    pid_t child_pid;
};

void __attribute__((always_inline)) copy_proc_cache(struct proc_cache_t *dst, struct proc_cache_t *src) {
    dst->executable = src->executable;
    copy_container_id(dst->container.container_id, src->container.container_id);
    return;
}

struct bpf_map_def SEC("maps/proc_cache") proc_cache = {
    .type = BPF_MAP_TYPE_LRU_HASH,
    .key_size = sizeof(u32),
    .value_size = sizeof(struct proc_cache_t),
    .max_entries = 4095,
    .pinning = 0,
    .namespace = "",
};

struct bpf_map_def SEC("maps/pid_cookie") pid_cookie = {
    .type = BPF_MAP_TYPE_LRU_HASH,
    .key_size = sizeof(u32),
    .value_size = sizeof(u32),
    .max_entries = 4097,
    .pinning = 0,
    .namespace = "",
};

int __attribute__((always_inline)) trace__sys_execveat() {
    struct syscall_cache_t syscall = {
        .type = SYSCALL_EXEC,
    };

    cache_syscall(&syscall);

    return 0;
}

SYSCALL_KPROBE0(execve) {
    return trace__sys_execveat();
}

SYSCALL_KPROBE0(execveat) {
    return trace__sys_execveat();
}

struct proc_cache_t * __attribute__((always_inline)) get_pid_cache(u32 tgid) {
    struct proc_cache_t *entry = NULL;

    u32 *cookie = (u32 *) bpf_map_lookup_elem(&pid_cookie, &tgid);
    if (cookie) {
        // Select the old cache entry
        u32 cookie_key = *cookie;
        entry = bpf_map_lookup_elem(&proc_cache, &cookie_key);
    }
    return entry;
}

int __attribute__((always_inline)) handle_exec_event(struct pt_regs *ctx, struct syscall_cache_t *syscall) {
    struct file *file = (struct file *)PT_REGS_PARM1(ctx);
    struct inode *inode = (struct inode *)PT_REGS_PARM2(ctx);
    struct path *path = &file->f_path;

    syscall->open.dentry = get_file_dentry(file);
    syscall->open.path_key = get_inode_key_path(inode, &file->f_path);

    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 tgid = pid_tgid >> 32;

    u64 ino = get_path_ino(path);
    bpf_printk("IIIIIIIIIII: %d\n", ino);

    struct exec_event_t event = {
        .event.type = EVENT_EXEC,
        .pid = tgid,
        .cache_entry.executable = {
            .inode = get_path_ino(path),
            .overlay_numlower = get_overlay_numlower(get_path_dentry(path)),
            .mount_id = get_path_mount_id(path),
        },
        .cache_entry.container = {},
        .cache_entry.timestamp = bpf_ktime_get_ns(),
    };

    // select parent cache entry
    struct proc_cache_t *parent_entry = get_pid_cache(tgid);
    if (parent_entry) {
        // inherit container ID
        copy_container_id(event.cache_entry.container.container_id, parent_entry->container.container_id);
    }

    // insert new proc cache entry
    u32 cookie = bpf_get_prandom_u32();
    bpf_map_update_elem(&proc_cache, &cookie, &event.cache_entry, BPF_ANY);

    // insert pid <-> cookie mapping
    bpf_map_update_elem(&pid_cookie, &tgid, &cookie, BPF_ANY);

    // cache dentry
    struct dentry *dentry = get_path_dentry(path);
    struct path_key_t path_key = get_key(dentry, path);
    resolve_dentry(dentry, path_key, NULL);

    // send the entry to maintain userspace cache
    send_process_events(ctx, event);

    // pop syscall as not called from an open not need handle it in the ret
    pop_syscall(SYSCALL_EXEC);

    return 0;
}

SEC("tracepoint/sched/sched_process_fork")
int sched_process_fork(struct _tracepoint_sched_process_fork *args) {
    u32 pid = 0;
    u32 ppid = 0;
    bpf_probe_read(&pid, sizeof(pid), &args->child_pid);
    bpf_probe_read(&ppid, sizeof(ppid), &args->parent_pid);

    // Ensures pid and ppid point to the same cookie
    u32 *cookie = (u32 *) bpf_map_lookup_elem(&pid_cookie, &ppid);
    if (cookie) {
        // Select the old cache entry
        u32 cookie_key = *cookie;
        bpf_map_update_elem(&pid_cookie, &pid, &cookie_key, BPF_ANY);
    }
    return 0;
}

SEC("kprobe/do_exit")
int kprobe_do_exit(struct pt_regs *ctx) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 tgid = pid_tgid >> 32;
    u32 pid = pid_tgid;

    if (tgid == pid) {
        // send the entry to maintain userspace cache
        struct exit_event_t event = {
            .event.type = EVENT_EXIT,
            .pid = tgid
        };

        send_process_events(ctx, event);
    }
    return 0;
}

#endif
