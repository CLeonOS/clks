static void clks_syscall_user_info_export(const struct clks_user_public_info *src,
                                          struct clks_syscall_user_info_user *dst) {
    if (src == CLKS_NULL || dst == CLKS_NULL) {
        return;
    }

    clks_memset(dst, 0, sizeof(*dst));
    dst->uid = src->uid;
    dst->role = src->role;
    dst->logged_in = src->logged_in;
    dst->disk_login_required = src->disk_login_required;
    clks_memcpy(dst->name, src->name, sizeof(dst->name));
    clks_memcpy(dst->home, src->home, sizeof(dst->home));
}

static u64 clks_syscall_user_copy_info_to_user(u64 dst_addr, const struct clks_user_public_info *info) {
    struct clks_syscall_user_info_user out_info;

    if (dst_addr == 0ULL || info == CLKS_NULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(dst_addr, (u64)sizeof(out_info)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_syscall_user_info_export(info, &out_info);
    clks_memcpy((void *)(usize)dst_addr, &out_info, sizeof(out_info));
    return 1ULL;
}

static u64 clks_syscall_user_current(u64 arg0) {
    struct clks_user_public_info info;

    if (clks_user_current_info(&info) == CLKS_FALSE) {
        return 0ULL;
    }

    return clks_syscall_user_copy_info_to_user(arg0, &info);
}

static u64 clks_syscall_user_login(u64 arg0) {
    struct clks_syscall_user_login_req req;
    char name[CLKS_USER_NAME_MAX];
    char password[96];
    struct clks_user_public_info info;

    if (arg0 == 0ULL || clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));

    if (clks_syscall_copy_user_string(req.name_ptr, name, sizeof(name)) == CLKS_FALSE ||
        clks_syscall_copy_user_string(req.password_ptr, password, sizeof(password)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_user_login(name, password, &info) == CLKS_FALSE) {
        return 0ULL;
    }

    if (req.out_info_ptr != 0ULL) {
        (void)clks_syscall_user_copy_info_to_user(req.out_info_ptr, &info);
    }

    return 1ULL;
}

static u64 clks_syscall_user_logout(void) {
    clks_user_logout();
    return 1ULL;
}

static u64 clks_syscall_user_count(void) {
#if CLKS_CFG_USER_SYSTEM == 0
    return 0ULL;
#else
    if (clks_user_current_is_admin() == CLKS_FALSE) {
        return (u64)-1;
    }

    return clks_user_count();
#endif
}

static u64 clks_syscall_user_at(u64 arg0, u64 arg1) {
#if CLKS_CFG_USER_SYSTEM == 0
    (void)arg0;
    (void)arg1;
    return 0ULL;
#else
    struct clks_user_public_info info;

    if (clks_user_current_is_admin() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_user_at(arg0, &info) == CLKS_FALSE) {
        return 0ULL;
    }

    return clks_syscall_user_copy_info_to_user(arg1, &info);
#endif
}

static u64 clks_syscall_user_add(u64 arg0) {
#if CLKS_CFG_USER_SYSTEM == 0
    (void)arg0;
    return 0ULL;
#else
    struct clks_syscall_user_add_req req;
    char name[CLKS_USER_NAME_MAX];
    char password[96];

    if (arg0 == 0ULL || clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));

    if (clks_syscall_copy_user_string(req.name_ptr, name, sizeof(name)) == CLKS_FALSE ||
        clks_syscall_copy_user_string(req.password_ptr, password, sizeof(password)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_user_create(name, password, req.role) == CLKS_TRUE) ? 1ULL : 0ULL;
#endif
}

static u64 clks_syscall_user_passwd(u64 arg0) {
#if CLKS_CFG_USER_SYSTEM == 0
    (void)arg0;
    return 0ULL;
#else
    struct clks_syscall_user_passwd_req req;
    char name[CLKS_USER_NAME_MAX];
    char old_password[96];
    char new_password[96];

    if (arg0 == 0ULL || clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));

    if (clks_syscall_copy_user_string(req.name_ptr, name, sizeof(name)) == CLKS_FALSE ||
        clks_syscall_copy_user_optional_string(req.old_password_ptr, old_password, sizeof(old_password)) == CLKS_FALSE ||
        clks_syscall_copy_user_string(req.new_password_ptr, new_password, sizeof(new_password)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_user_change_password(name, old_password, new_password) == CLKS_TRUE) ? 1ULL : 0ULL;
#endif
}

static u64 clks_syscall_user_set_role(u64 arg0, u64 arg1) {
#if CLKS_CFG_USER_SYSTEM == 0
    (void)arg0;
    (void)arg1;
    return 0ULL;
#else
    char name[CLKS_USER_NAME_MAX];

    if (clks_syscall_copy_user_string(arg0, name, sizeof(name)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_user_set_role(name, arg1) == CLKS_TRUE) ? 1ULL : 0ULL;
#endif
}

static u64 clks_syscall_user_remove(u64 arg0) {
#if CLKS_CFG_USER_SYSTEM == 0
    (void)arg0;
    return 0ULL;
#else
    char name[CLKS_USER_NAME_MAX];

    if (clks_syscall_copy_user_string(arg0, name, sizeof(name)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_user_remove(name) == CLKS_TRUE) ? 1ULL : 0ULL;
#endif
}

static u64 clks_syscall_user_is_admin(void) {
    return (clks_user_current_is_admin() == CLKS_TRUE) ? 1ULL : 0ULL;
}
