u64 clks_exec_current_pid(void) {
    i32 depth_index = clks_exec_current_depth_index();

    if (depth_index < 0) {
        return 0ULL;
    }

    return clks_exec_pid_stack[(u32)depth_index];
}

u32 clks_exec_current_tty(void) {
    i32 depth_index = clks_exec_current_depth_index();
    u32 tty_count = clks_tty_count();
    i32 slot;
    const struct clks_exec_proc_record *proc;

    if (tty_count == 0U) {
        return 0U;
    }

    if (depth_index < 0) {
        u32 active = clks_tty_active();
        return (active < tty_count) ? active : 0U;
    }

    slot = clks_exec_proc_find_slot_by_pid(clks_exec_pid_stack[(u32)depth_index]);

    if (slot < 0) {
        return 0U;
    }

    proc = &clks_exec_proc_table[(u32)slot];

    if (proc->used == CLKS_FALSE || proc->tty_index >= tty_count) {
        return 0U;
    }

    return proc->tty_index;
}

static struct clks_exec_proc_record *clks_exec_current_proc(void) {
    i32 depth_index = clks_exec_current_depth_index();
    i32 slot;

    if (depth_index < 0) {
        return CLKS_NULL;
    }

    slot = clks_exec_proc_find_slot_by_pid(clks_exec_pid_stack[(u32)depth_index]);

    if (slot < 0) {
        return CLKS_NULL;
    }

    if (clks_exec_proc_table[(u32)slot].used == CLKS_FALSE) {
        return CLKS_NULL;
    }

    return &clks_exec_proc_table[(u32)slot];
}

clks_bool clks_exec_current_set_user(u64 uid, u64 role, const char *name, const char *home, clks_bool logged_in,
                                     clks_bool disk_login_required) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();

    if (proc == CLKS_NULL || name == CLKS_NULL || home == CLKS_NULL || name[0] == '\0' || home[0] != '/') {
        return CLKS_FALSE;
    }

    proc->uid = uid;
    proc->user_role = (role == CLKS_EXEC_USER_ROLE_ADMIN) ? CLKS_EXEC_USER_ROLE_ADMIN : CLKS_EXEC_USER_ROLE_USER;
    proc->user_logged_in = logged_in;
    proc->disk_login_required = disk_login_required;
    clks_exec_copy_line(proc->username, sizeof(proc->username), name);
    clks_exec_copy_line(proc->user_home, sizeof(proc->user_home), home);
    return CLKS_TRUE;
}

void clks_exec_current_clear_user(clks_bool disk_login_required) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();

    if (proc == CLKS_NULL) {
        return;
    }

    clks_exec_proc_set_default_user(proc);
    proc->disk_login_required = disk_login_required;
}

clks_bool clks_exec_current_user_info(u64 *out_uid, u64 *out_role, char *out_name, usize name_size, char *out_home,
                                      usize home_size, clks_bool *out_logged_in, clks_bool *out_disk_login_required) {
    const struct clks_exec_proc_record *proc = clks_exec_current_proc();

    if (proc == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (out_uid != CLKS_NULL) {
        *out_uid = proc->uid;
    }
    if (out_role != CLKS_NULL) {
        *out_role = proc->user_role;
    }
    if (out_logged_in != CLKS_NULL) {
        *out_logged_in = proc->user_logged_in;
    }
    if (out_disk_login_required != CLKS_NULL) {
        *out_disk_login_required = proc->disk_login_required;
    }
    if (out_name != CLKS_NULL && name_size > 0U) {
        clks_exec_copy_line(out_name, name_size, proc->username);
    }
    if (out_home != CLKS_NULL && home_size > 0U) {
        clks_exec_copy_line(out_home, home_size, proc->user_home);
    }

    return CLKS_TRUE;
}

u64 clks_exec_current_uid(void) {
    const struct clks_exec_proc_record *proc = clks_exec_current_proc();
    return (proc != CLKS_NULL) ? proc->uid : CLKS_EXEC_USER_UID_NOBODY;
}

u64 clks_exec_current_role(void) {
    const struct clks_exec_proc_record *proc = clks_exec_current_proc();
    return (proc != CLKS_NULL) ? proc->user_role : CLKS_EXEC_USER_ROLE_USER;
}

clks_bool clks_exec_current_logged_in(void) {
    const struct clks_exec_proc_record *proc = clks_exec_current_proc();
    return (proc != CLKS_NULL) ? proc->user_logged_in : CLKS_FALSE;
}

u64 clks_exec_current_argc(void) {
    const struct clks_exec_proc_record *proc = clks_exec_current_proc();

    if (proc == CLKS_NULL) {
        return 0ULL;
    }

    return (u64)proc->argc;
}

clks_bool clks_exec_copy_current_argv(u64 index, char *out_value, usize out_size) {
    const struct clks_exec_proc_record *proc = clks_exec_current_proc();

    if (proc == CLKS_NULL || out_value == CLKS_NULL || out_size == 0U) {
        return CLKS_FALSE;
    }

    if (index >= (u64)proc->argc || index >= CLKS_EXEC_MAX_ARGS) {
        return CLKS_FALSE;
    }

    clks_exec_copy_line(out_value, out_size, proc->argv_items[(u32)index]);
    return CLKS_TRUE;
}

u64 clks_exec_current_envc(void) {
    const struct clks_exec_proc_record *proc = clks_exec_current_proc();

    if (proc == CLKS_NULL) {
        return 0ULL;
    }

    return (u64)proc->envc;
}

clks_bool clks_exec_copy_current_env(u64 index, char *out_value, usize out_size) {
    const struct clks_exec_proc_record *proc = clks_exec_current_proc();

    if (proc == CLKS_NULL || out_value == CLKS_NULL || out_size == 0U) {
        return CLKS_FALSE;
    }

    if (index >= (u64)proc->envc || index >= CLKS_EXEC_MAX_ENVS) {
        return CLKS_FALSE;
    }

    clks_exec_copy_line(out_value, out_size, proc->env_items[(u32)index]);
    return CLKS_TRUE;
}

u64 clks_exec_current_signal(void) {
    const struct clks_exec_proc_record *proc = clks_exec_current_proc();
    return (proc != CLKS_NULL) ? proc->last_signal : 0ULL;
}

u64 clks_exec_current_fault_vector(void) {
    const struct clks_exec_proc_record *proc = clks_exec_current_proc();
    return (proc != CLKS_NULL) ? proc->last_fault_vector : 0ULL;
}

u64 clks_exec_current_fault_error(void) {
    const struct clks_exec_proc_record *proc = clks_exec_current_proc();
    return (proc != CLKS_NULL) ? proc->last_fault_error : 0ULL;
}

u64 clks_exec_current_fault_rip(void) {
    const struct clks_exec_proc_record *proc = clks_exec_current_proc();
    return (proc != CLKS_NULL) ? proc->last_fault_rip : 0ULL;
}

