static u8 clks_syscall_fs_io_bounce[CLKS_SYSCALL_FS_IO_CHUNK_LEN];

#ifndef CLKS_SYSCALL_FS_WHOLE_WRITE_MAX
#define CLKS_SYSCALL_FS_WHOLE_WRITE_MAX (1024ULL * 1024ULL)
#endif

static u64 clks_syscall_fs_stat_type(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];
    struct clks_fs_node_info info;
    struct clks_exec_proc_snapshot snap;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_user_path_read_allowed(path) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_procfs_is_root(path) == CLKS_TRUE) {
        return (u64)CLKS_FS_NODE_DIR;
    }

    if (CLKS_CFG_PROCFS != 0 &&
        (clks_syscall_procfs_is_list(path) == CLKS_TRUE || clks_syscall_procfs_is_self(path) == CLKS_TRUE)) {
        return (u64)CLKS_FS_NODE_FILE;
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_procfs_snapshot_for_path(path, &snap) == CLKS_TRUE) {
        return (u64)CLKS_FS_NODE_FILE;
    }

    if (clks_fs_stat(path, &info) == CLKS_FALSE) {
        return (u64)-1;
    }

    return (u64)info.type;
}

static u64 clks_syscall_fs_stat_size(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];
    struct clks_fs_node_info info;
    char proc_text[CLKS_SYSCALL_PROCFS_TEXT_MAX];
    usize proc_len = 0U;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_user_path_read_allowed(path) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_procfs_is_root(path) == CLKS_TRUE) {
        return 0ULL;
    }

    if (CLKS_CFG_PROCFS != 0 &&
        clks_syscall_procfs_render_file(path, proc_text, sizeof(proc_text), &proc_len) == CLKS_TRUE) {
        return (u64)proc_len;
    }

    if (clks_fs_stat(path, &info) == CLKS_FALSE) {
        return (u64)-1;
    }

    return info.size;
}

static u64 clks_syscall_fs_mkdir(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_user_path_write_allowed(path) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_fs_mkdir(path) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_fs_write_common(u64 arg0, u64 arg1, u64 arg2, clks_bool append_mode) {
    char path[CLKS_SYSCALL_PATH_MAX];
    u64 src_addr = arg1;
    u64 checked = 0ULL;
    u64 written = 0ULL;
    u64 remaining = arg2;
    clks_bool ok;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_user_path_write_allowed(path) == CLKS_FALSE) {
        return 0ULL;
    }

    if (arg2 == 0ULL) {
        if (append_mode == CLKS_TRUE) {
            ok = clks_fs_append(path, CLKS_NULL, 0ULL);
        } else {
            ok = clks_fs_write_all(path, CLKS_NULL, 0ULL);
        }

        return (ok == CLKS_TRUE) ? 1ULL : 0ULL;
    }

    if (arg1 == 0ULL) {
        return 0ULL;
    }

    if (append_mode == CLKS_FALSE && arg2 <= CLKS_SYSCALL_FS_WHOLE_WRITE_MAX) {
        u8 *whole = (u8 *)clks_kmalloc((usize)arg2);

        if (whole != CLKS_NULL) {
            u64 copied = 0ULL;

            remaining = arg2;
            while (remaining > 0ULL) {
                u64 chunk_len = remaining;

                if (chunk_len > CLKS_SYSCALL_FS_IO_CHUNK_LEN) {
                    chunk_len = CLKS_SYSCALL_FS_IO_CHUNK_LEN;
                }

                if (src_addr + copied < src_addr ||
                    clks_syscall_user_ptr_readable(src_addr + copied, chunk_len) == CLKS_FALSE) {
                    clks_kfree(whole);
                    return 0ULL;
                }

                clks_memcpy(whole + (usize)copied, (const void *)(usize)(src_addr + copied), (usize)chunk_len);
                copied += chunk_len;
                remaining -= chunk_len;
            }

            ok = clks_fs_write_all(path, whole, arg2);
            clks_kfree(whole);
            return (ok == CLKS_TRUE) ? arg2 : 0ULL;
        }
    }

    remaining = arg2;
    while (checked < arg2) {
        u64 chunk_len = remaining;

        if (chunk_len > CLKS_SYSCALL_FS_IO_CHUNK_LEN) {
            chunk_len = CLKS_SYSCALL_FS_IO_CHUNK_LEN;
        }

        if (src_addr + checked < src_addr ||
            clks_syscall_user_ptr_readable(src_addr + checked, chunk_len) == CLKS_FALSE) {
            return 0ULL;
        }

        checked += chunk_len;
        remaining -= chunk_len;
    }

    remaining = arg2;
    while (remaining > 0ULL) {
        u64 chunk_len = remaining;

        if (chunk_len > CLKS_SYSCALL_FS_IO_CHUNK_LEN) {
            chunk_len = CLKS_SYSCALL_FS_IO_CHUNK_LEN;
        }

        clks_memcpy(clks_syscall_fs_io_bounce, (const void *)(usize)(src_addr + written), (usize)chunk_len);

        if (append_mode == CLKS_TRUE || written > 0ULL) {
            ok = clks_fs_append(path, clks_syscall_fs_io_bounce, chunk_len);
        } else {
            ok = clks_fs_write_all(path, clks_syscall_fs_io_bounce, chunk_len);
        }
        if (ok == CLKS_FALSE) {
            return 0ULL;
        }

        written += chunk_len;
        remaining -= chunk_len;
    }

    return arg2;
}

static u64 clks_syscall_fs_write(u64 arg0, u64 arg1, u64 arg2) {
    return clks_syscall_fs_write_common(arg0, arg1, arg2, CLKS_FALSE);
}

static u64 clks_syscall_fs_append(u64 arg0, u64 arg1, u64 arg2) {
    return clks_syscall_fs_write_common(arg0, arg1, arg2, CLKS_TRUE);
}

static u64 clks_syscall_fs_remove(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_user_path_write_allowed(path) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_fs_remove(path) == CLKS_TRUE) ? 1ULL : 0ULL;
