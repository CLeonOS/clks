static u64 clks_syscall_inputm_count(void) {
    return clks_inputm_count();
}

static u64 clks_syscall_inputm_info(u64 arg0, u64 arg1, u64 arg2) {
    struct clks_inputm_info info;

    if (arg1 == 0ULL || arg2 < (u64)sizeof(struct clks_inputm_info)) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, (u64)sizeof(struct clks_inputm_info)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_inputm_info_at(arg0, &info) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy((void *)(usize)arg1, &info, sizeof(info));
    return 1ULL;
}

static u64 clks_syscall_inputm_current(void) {
    return clks_inputm_current();
}

static u64 clks_syscall_inputm_select(u64 arg0) {
    return (clks_inputm_select(arg0) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_inputm_register(u64 arg0) {
    struct clks_inputm_register_req req;
    char name[CLKS_INPUTM_NAME_MAX];
    char path[CLKS_INPUTM_PATH_MAX];

    if (arg0 == 0ULL ||
        clks_syscall_user_ptr_readable(arg0, (u64)sizeof(struct clks_inputm_register_req)) == CLKS_FALSE) {
        return (u64)-1;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));
    if (clks_syscall_copy_user_string(req.name_ptr, name, sizeof(name)) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_syscall_copy_user_optional_string(req.path_ptr, path, sizeof(path)) == CLKS_FALSE) {
        return (u64)-1;
    }

    return clks_inputm_register(name, path, req.flags);
}

static u64 clks_syscall_tty_status_set(u64 arg0) {
    char text[CLKS_INPUTM_STATUS_MAX];

    if (arg0 == 0ULL) {
        clks_inputm_status_set("");
        return 1ULL;
    }

    if (clks_syscall_copy_user_string(arg0, text, sizeof(text)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_inputm_status_set(text);
    return 1ULL;
}
