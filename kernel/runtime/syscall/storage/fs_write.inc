static u64 clks_syscall_fs_stat_type(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];
    struct clks_fs_node_info info;
    struct clks_exec_proc_snapshot snap;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
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

    return (clks_fs_mkdir(path) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_fs_write_common(u64 arg0, u64 arg1, u64 arg2, clks_bool append_mode) {
    char path[CLKS_SYSCALL_PATH_MAX];
    const u8 *src = (const u8 *)arg1;
    u64 remaining = arg2;
    clks_bool first_chunk = CLKS_TRUE;
    clks_bool ok;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
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

    if (clks_syscall_user_ptr_readable(arg1, arg2) == CLKS_FALSE) {
        return 0ULL;
    }

    while (remaining > 0ULL) {
        u64 chunk_len = remaining;
        void *heap_copy;

        if (chunk_len > CLKS_SYSCALL_FS_IO_CHUNK_LEN) {
            chunk_len = CLKS_SYSCALL_FS_IO_CHUNK_LEN;
        }

        heap_copy = clks_kmalloc((usize)chunk_len);
        if (heap_copy == CLKS_NULL) {
            return 0ULL;
        }

        clks_memcpy(heap_copy, (const void *)src, (usize)chunk_len);

        if (append_mode == CLKS_TRUE || first_chunk == CLKS_FALSE) {
            ok = clks_fs_append(path, heap_copy, chunk_len);
        } else {
            ok = clks_fs_write_all(path, heap_copy, chunk_len);
        }

        clks_kfree(heap_copy);

        if (ok == CLKS_FALSE) {
            return 0ULL;
        }

        src += chunk_len;
        remaining -= chunk_len;
        first_chunk = CLKS_FALSE;
    }

    return 1ULL;
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

    return (clks_fs_remove(path) == CLKS_TRUE) ? 1ULL : 0ULL;
