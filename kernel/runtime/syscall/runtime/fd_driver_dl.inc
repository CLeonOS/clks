static u64 clks_syscall_fd_open(u64 arg0, u64 arg1, u64 arg2) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    return clks_exec_fd_open(path, arg1, arg2);
}

static u64 clks_syscall_fd_read(u64 arg0, u64 arg1, u64 arg2) {
    if (arg2 > 0ULL && arg1 == 0ULL) {
        return (u64)-1;
    }

    if (arg2 > 0ULL && clks_syscall_user_ptr_writable(arg1, arg2) == CLKS_FALSE) {
        return (u64)-1;
    }

    return clks_exec_fd_read(arg0, (void *)arg1, arg2);
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

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    return clks_driver_load_path(path);
}

static u64 clks_syscall_driver_unload(u64 arg0) {
    char name_or_path[CLKS_SYSCALL_PATH_MAX];

    if (clks_syscall_copy_user_string(arg0, name_or_path, sizeof(name_or_path)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_driver_unload(name_or_path) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_driver_reload(void) {
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

