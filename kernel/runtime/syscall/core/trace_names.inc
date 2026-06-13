static void clks_syscall_serial_write_hex64(u64 value) {
    i32 nibble;

    for (nibble = 15; nibble >= 0; nibble--) {
        u64 current = (value >> (u64)(nibble * 4)) & 0x0FULL;
        char ch = (current < 10ULL) ? (char)('0' + current) : (char)('A' + (current - 10ULL));
        clks_serial_write_char(ch);
    }
}

static const char *clks_syscall_name(u64 id) {
    const struct clks_syscall_meta *meta = clks_syscall_meta_by_id(id);

    if (meta == CLKS_NULL || meta->name == CLKS_NULL) {
        return "UNKNOWN";
    }

    return meta->name;
}
static clks_bool clks_syscall_trace_suppressed(u64 id) {
    const struct clks_syscall_meta *meta = clks_syscall_meta_by_id(id);

    if (meta == CLKS_NULL) {
        return CLKS_FALSE;
    }

    return ((meta->flags & CLKS_SYSCALL_FLAG_TRACE_SUPPRESS) != 0U) ? CLKS_TRUE : CLKS_FALSE;
}
static void clks_syscall_trace_user_call(clks_bool trace_enabled, u64 pid, u64 id, u64 arg0, u64 arg1, u64 arg2) {
    const struct clks_syscall_meta *meta;
    const char *name;

    if (CLKS_CFG_SYSCALL_SERIAL_LOG == 0) {
        return;
    }

    if (trace_enabled == CLKS_FALSE) {
        return;
    }

    if (clks_syscall_trace_suppressed(id) == CLKS_TRUE) {
        return;
    }

    meta = clks_syscall_meta_by_id(id);
    if (meta != CLKS_NULL && (meta->flags & CLKS_SYSCALL_FLAG_TRACE_RETURN_ONLY) != 0U) {
        return;
    }

    name = clks_syscall_name(id);
    clks_serial_write("[INFO][SYSCALL] CALL PID: 0X");
    clks_syscall_serial_write_hex64(pid);
    clks_serial_write(" ID: 0X");
    clks_syscall_serial_write_hex64(id);
    clks_serial_write(" NAME: ");
    clks_serial_write(name);
    clks_serial_write(" ARG0: 0X");
    clks_syscall_serial_write_hex64(arg0);
    clks_serial_write(" ARG1: 0X");
    clks_syscall_serial_write_hex64(arg1);
    clks_serial_write(" ARG2: 0X");
    clks_syscall_serial_write_hex64(arg2);
    clks_serial_write("\n");
}

static void clks_syscall_trace_user_return(clks_bool trace_enabled, u64 pid, u64 id, u64 ret) {
    const struct clks_syscall_meta *meta;
    const char *name;

    if (CLKS_CFG_SYSCALL_SERIAL_LOG == 0) {
        return;
    }

    if (trace_enabled == CLKS_FALSE) {
        return;
    }

    if (clks_syscall_trace_suppressed(id) == CLKS_TRUE) {
        return;
    }

    meta = clks_syscall_meta_by_id(id);
    if (meta != CLKS_NULL && (meta->flags & CLKS_SYSCALL_FLAG_TRACE_SKIP_ZERO_RET) != 0U && ret == 0ULL) {
        return;
    }

    name = clks_syscall_name(id);
    clks_serial_write("[INFO][SYSCALL] RET  PID: 0X");
    clks_syscall_serial_write_hex64(pid);
    clks_serial_write(" ID: 0X");
    clks_syscall_serial_write_hex64(id);
    clks_serial_write(" NAME: ");
    clks_serial_write(name);
    clks_serial_write(" RET: 0X");
    clks_syscall_serial_write_hex64(ret);
    clks_serial_write("\n");
}
