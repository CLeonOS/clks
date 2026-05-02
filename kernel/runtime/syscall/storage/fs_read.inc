static u64 clks_syscall_fs_child_count(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];
    u64 base_count;

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_procfs_is_root(path) == CLKS_TRUE) {
        return 2ULL + clks_exec_proc_count();
    }

    base_count = clks_fs_count_children(path);

    if (base_count == (u64)-1) {
        return (u64)-1;
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_fs_is_root(path) == CLKS_TRUE &&
        clks_syscall_fs_has_real_proc_dir() == CLKS_FALSE) {
        return base_count + 1ULL;
    }

    return base_count;
}

static u64 clks_syscall_fs_get_child_name(u64 arg0, u64 arg1, u64 arg2) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (arg2 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg2, CLKS_SYSCALL_NAME_MAX) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_procfs_is_root(path) == CLKS_TRUE) {
        if (arg1 == 0ULL) {
            clks_memset((void *)arg2, 0, CLKS_SYSCALL_NAME_MAX);
            clks_memcpy((void *)arg2, "self", 5U);
            return 1ULL;
        }

        if (arg1 == 1ULL) {
            clks_memset((void *)arg2, 0, CLKS_SYSCALL_NAME_MAX);
            clks_memcpy((void *)arg2, "list", 5U);
            return 1ULL;
        }

        {
            u64 pid = 0ULL;
            char pid_text[32];
            usize len;

            if (clks_exec_proc_pid_at(arg1 - 2ULL, &pid) == CLKS_FALSE || pid == 0ULL) {
                return 0ULL;
            }

            clks_memset(pid_text, 0, sizeof(pid_text));
            len = clks_syscall_procfs_append_u64_dec(pid_text, sizeof(pid_text), 0U, pid);

            if (len + 1U > CLKS_SYSCALL_NAME_MAX) {
                return 0ULL;
            }

            clks_memset((void *)arg2, 0, CLKS_SYSCALL_NAME_MAX);
            clks_memcpy((void *)arg2, pid_text, len + 1U);
            return 1ULL;
        }
    }

    if (CLKS_CFG_PROCFS != 0 && clks_syscall_fs_is_root(path) == CLKS_TRUE &&
        clks_syscall_fs_has_real_proc_dir() == CLKS_FALSE) {
        if (arg1 == 0ULL) {
            clks_memset((void *)arg2, 0, CLKS_SYSCALL_NAME_MAX);
            clks_memcpy((void *)arg2, "proc", 5U);
            return 1ULL;
        }

        if (clks_fs_get_child_name(path, arg1 - 1ULL, (char *)arg2, (usize)CLKS_SYSCALL_NAME_MAX) == CLKS_FALSE) {
            return 0ULL;
        }

        return 1ULL;
    }

    if (clks_fs_get_child_name(path, arg1, (char *)arg2, (usize)CLKS_SYSCALL_NAME_MAX) == CLKS_FALSE) {
        return 0ULL;
    }

    return 1ULL;
}

static u64 clks_syscall_fs_read(u64 arg0, u64 arg1, u64 arg2) {
    char path[CLKS_SYSCALL_PATH_MAX];
    const void *data;
    u64 file_size = 0ULL;
    u64 copy_len;

    if (arg1 == 0ULL || arg2 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, arg2) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (CLKS_CFG_PROCFS != 0 &&
        (clks_syscall_procfs_is_list(path) == CLKS_TRUE || clks_syscall_procfs_is_self(path) == CLKS_TRUE ||
         clks_syscall_procfs_parse_pid(path, &file_size) == CLKS_TRUE)) {
        char proc_text[CLKS_SYSCALL_PROCFS_TEXT_MAX];
        usize proc_len = 0U;

        if (clks_syscall_procfs_render_file(path, proc_text, sizeof(proc_text), &proc_len) == CLKS_FALSE) {
            return 0ULL;
        }

        copy_len = ((u64)proc_len < arg2) ? (u64)proc_len : arg2;

        if (copy_len == 0ULL) {
            return 0ULL;
        }

        clks_memcpy((void *)arg1, proc_text, (usize)copy_len);
        return copy_len;
    }

    data = clks_fs_read_all(path, &file_size);

    if (data == CLKS_NULL || file_size == 0ULL) {
        return 0ULL;
    }

    copy_len = (file_size < arg2) ? file_size : arg2;
    clks_memcpy((void *)arg1, data, (usize)copy_len);
    return copy_len;
}

