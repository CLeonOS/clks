static u8 clks_syscall_fd_io_bounce[CLKS_SYSCALL_FS_IO_CHUNK_LEN];

static u64 clks_syscall_fd_open(u64 arg0, u64 arg1, u64 arg2) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if ((((arg1 & CLKS_EXEC_FD_ACCESS_MASK) == CLKS_EXEC_O_WRONLY) ||
         ((arg1 & CLKS_EXEC_FD_ACCESS_MASK) == CLKS_EXEC_O_RDWR) || ((arg1 & CLKS_EXEC_O_CREAT) != 0ULL) ||
         ((arg1 & CLKS_EXEC_O_TRUNC) != 0ULL) || ((arg1 & CLKS_EXEC_O_APPEND) != 0ULL)) &&
        clks_user_path_write_allowed(path) == CLKS_FALSE) {
        return (u64)-1;
    }

    return clks_exec_fd_open(path, arg1, arg2);
}

static u64 clks_syscall_fd_read(u64 arg0, u64 arg1, u64 arg2) {
    u64 done = 0ULL;

    if (arg2 > 0ULL && arg1 == 0ULL) {
        return (u64)-1;
    }

    if (arg2 == 0ULL) {
        return 0ULL;
    }

    while (done < arg2) {
        u64 chunk_len = arg2 - done;
        u64 got;

        if (chunk_len > CLKS_SYSCALL_FS_IO_CHUNK_LEN) {
            chunk_len = CLKS_SYSCALL_FS_IO_CHUNK_LEN;
        }

        if (arg1 + done < arg1 || clks_syscall_user_ptr_writable(arg1 + done, chunk_len) == CLKS_FALSE) {
            return (done > 0ULL) ? done : (u64)-1;
        }

        got = clks_exec_fd_read(arg0, clks_syscall_fd_io_bounce, chunk_len);
        if (got == (u64)-1) {
            return (done > 0ULL) ? done : (u64)-1;
        }

        if (got == 0ULL) {
            break;
        }

        if (got > chunk_len) {
            return (done > 0ULL) ? done : (u64)-1;
        }

        clks_memcpy((void *)(usize)(arg1 + done), clks_syscall_fd_io_bounce, (usize)got);
        done += got;

        if (got < chunk_len) {
            break;
        }
    }

    return done;
}

static u64 clks_syscall_fd_write(u64 arg0, u64 arg1, u64 arg2) {
    if (arg2 > 0ULL && arg1 == 0ULL) {
        return (u64)-1;
    }

    if (arg2 > 0ULL && clks_syscall_user_ptr_readable(arg1, arg2) == CLKS_FALSE) {
        return (u64)-1;
    }

    return clks_exec_fd_write(arg0, (const void *)arg1, arg2);
}

static u64 clks_syscall_fd_close(u64 arg0) {
    return clks_exec_fd_close(arg0);
}

static u64 clks_syscall_fd_dup(u64 arg0) {
    return clks_exec_fd_dup(arg0);
}

static u64 clks_syscall_pty_open(void) {
    return clks_exec_fd_open_pty();
}

static u64 clks_syscall_driver_count(void) {
    return clks_driver_count();
}

static u64 clks_syscall_driver_info(u64 arg0, u64 arg1, u64 arg2) {
    struct clks_driver_info info;
    struct clks_syscall_driver_info_user out_info;

    if (arg1 == 0ULL || arg2 < (u64)sizeof(out_info)) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, (u64)sizeof(out_info)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_driver_get(arg0, &info) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memset(&out_info, 0, sizeof(out_info));
    clks_memcpy(out_info.name, info.name, sizeof(out_info.name));
    clks_memcpy(out_info.path, info.path, sizeof(out_info.path));
    out_info.kind = (u64)info.kind;
    out_info.state = (u64)info.state;
    out_info.driver_class = (u64)info.driver_class;
    out_info.from_elf = (info.from_elf == CLKS_TRUE) ? 1ULL : 0ULL;
    out_info.image_size = info.image_size;
    out_info.elf_entry = info.elf_entry;
    out_info.load_id = info.load_id;
    out_info.owner_pid = info.owner_pid;
    clks_memcpy((void *)(usize)arg1, &out_info, sizeof(out_info));
    return 1ULL;
}

static u64 clks_syscall_driver_load(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (clks_user_privileged_operation_allowed() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    return clks_driver_load_path(path);
}

static u64 clks_syscall_driver_unload(u64 arg0) {
    char name_or_path[CLKS_SYSCALL_PATH_MAX];

    if (clks_user_privileged_operation_allowed() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_copy_user_string(arg0, name_or_path, sizeof(name_or_path)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_driver_unload(name_or_path) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_driver_reload(void) {
    if (clks_user_privileged_operation_allowed() == CLKS_FALSE) {
        return 0ULL;
    }

    return clks_driver_reload_elf_dir();
}

static u64 clks_syscall_dl_open(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    return clks_exec_dl_open(path);
}

static u64 clks_syscall_dl_close(u64 arg0) {
    return clks_exec_dl_close(arg0);
}

static u64 clks_syscall_dl_sym(u64 arg0, u64 arg1) {
    char symbol[CLKS_SYSCALL_NAME_MAX];

    if (clks_syscall_copy_user_string(arg1, symbol, sizeof(symbol)) == CLKS_FALSE) {
        return 0ULL;
    }

    return clks_exec_dl_sym(arg0, symbol);
}
