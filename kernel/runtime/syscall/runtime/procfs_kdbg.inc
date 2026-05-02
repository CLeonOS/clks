static clks_bool clks_syscall_procfs_is_root(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/proc") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_syscall_fs_is_root(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_syscall_fs_has_real_proc_dir(void) {
    struct clks_fs_node_info info;

    if (clks_fs_stat("/proc", &info) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return (info.type == CLKS_FS_NODE_DIR) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_syscall_procfs_is_self(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/proc/self") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_syscall_procfs_is_list(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/proc/list") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_syscall_parse_u64_dec(const char *text, u64 *out_value) {
    u64 value = 0ULL;
    usize i = 0U;

    if (text == CLKS_NULL || out_value == CLKS_NULL || text[0] == '\0') {
        return CLKS_FALSE;
    }

    while (text[i] != '\0') {
        u64 digit;

        if (text[i] < '0' || text[i] > '9') {
            return CLKS_FALSE;
        }

        digit = (u64)(text[i] - '0');

        if (value > ((0xFFFFFFFFFFFFFFFFULL - digit) / 10ULL)) {
            return CLKS_FALSE;
        }

        value = (value * 10ULL) + digit;
        i++;
    }

    *out_value = value;
    return CLKS_TRUE;
}

static clks_bool clks_syscall_procfs_parse_pid(const char *path, u64 *out_pid) {
    const char *part;
    usize i = 0U;
    char pid_text[32];
    u64 pid;

    if (path == CLKS_NULL || out_pid == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (path[0] != '/' || path[1] != 'p' || path[2] != 'r' || path[3] != 'o' || path[4] != 'c' || path[5] != '/') {
        return CLKS_FALSE;
    }

    part = &path[6];

    if (part[0] == '\0' || clks_strcmp(part, "self") == 0 || clks_strcmp(part, "list") == 0) {
        return CLKS_FALSE;
    }

    while (part[i] != '\0') {
        if (i + 1U >= sizeof(pid_text)) {
            return CLKS_FALSE;
        }

        if (part[i] < '0' || part[i] > '9') {
            return CLKS_FALSE;
        }

        pid_text[i] = part[i];
        i++;
    }

    pid_text[i] = '\0';

    if (clks_syscall_parse_u64_dec(pid_text, &pid) == CLKS_FALSE || pid == 0ULL) {
        return CLKS_FALSE;
    }

    *out_pid = pid;
    return CLKS_TRUE;
}

static const char *clks_syscall_proc_state_name(u64 state) {
    if (state == CLKS_EXEC_PROC_STATE_PENDING) {
        return "PENDING";
    }

    if (state == CLKS_EXEC_PROC_STATE_RUNNING) {
        return "RUNNING";
    }

    if (state == CLKS_EXEC_PROC_STATE_STOPPED) {
        return "STOPPED";
    }

    if (state == CLKS_EXEC_PROC_STATE_EXITED) {
        return "EXITED";
    }

    return "UNUSED";
}

static usize clks_syscall_procfs_append_char(char *out, usize out_size, usize pos, char ch) {
    if (out == CLKS_NULL || out_size == 0U) {
        return pos;
    }

    if (pos + 1U < out_size) {
        out[pos] = ch;
        out[pos + 1U] = '\0';
        return pos + 1U;
    }

    out[out_size - 1U] = '\0';
    return pos;
}

static usize clks_syscall_procfs_append_text(char *out, usize out_size, usize pos, const char *text) {
    usize i = 0U;

    if (text == CLKS_NULL) {
        return pos;
    }

    while (text[i] != '\0') {
        pos = clks_syscall_procfs_append_char(out, out_size, pos, text[i]);
        i++;
    }

    return pos;
}

static usize clks_syscall_procfs_append_u64_dec(char *out, usize out_size, usize pos, u64 value) {
    char temp[32];
    usize len = 0U;
    usize i;

    if (value == 0ULL) {
        return clks_syscall_procfs_append_char(out, out_size, pos, '0');
    }

    while (value != 0ULL && len + 1U < sizeof(temp)) {
        temp[len++] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    }

    for (i = 0U; i < len; i++) {
        pos = clks_syscall_procfs_append_char(out, out_size, pos, temp[len - 1U - i]);
    }

    return pos;
}

static usize clks_syscall_procfs_append_u64_hex(char *out, usize out_size, usize pos, u64 value) {
    i32 nibble;

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "0X");

    for (nibble = 15; nibble >= 0; nibble--) {
        u64 current = (value >> (u64)(nibble * 4)) & 0x0FULL;
        char ch = (current < 10ULL) ? (char)('0' + current) : (char)('A' + (current - 10ULL));
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ch);
    }

    return pos;
}

static usize clks_syscall_procfs_append_n(char *out, usize out_size, usize pos, const char *text, usize text_len) {
    usize i = 0U;

    if (text == CLKS_NULL) {
        return pos;
    }

    while (i < text_len) {
        pos = clks_syscall_procfs_append_char(out, out_size, pos, text[i]);
        i++;
    }

    return pos;
}

static clks_bool clks_syscall_is_hex(char ch) {
    if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}

static u8 clks_syscall_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return (u8)(ch - '0');
    }

    if (ch >= 'a' && ch <= 'f') {
        return (u8)(10 + (ch - 'a'));
    }

    return (u8)(10 + (ch - 'A'));
}

static clks_bool clks_syscall_parse_symbol_line(const char *line, usize len, u64 *out_addr, const char **out_name,
                                                usize *out_name_len, const char **out_source, usize *out_source_len) {
    usize i = 0U;
    u64 addr = 0ULL;
    u32 digits = 0U;
    usize name_start;
    usize name_end;
    usize source_start;
    usize source_end;

    if (line == CLKS_NULL || out_addr == CLKS_NULL || out_name == CLKS_NULL || out_name_len == CLKS_NULL ||
        out_source == CLKS_NULL || out_source_len == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (len == 0U) {
        return CLKS_FALSE;
    }

    if (len >= 2U && line[0] == '0' && (line[1] == 'X' || line[1] == 'x')) {
        i = 2U;
    }

    while (i < len && clks_syscall_is_hex(line[i]) == CLKS_TRUE) {
        addr = (addr << 4) | (u64)clks_syscall_hex_value(line[i]);
        digits++;
        i++;
    }

    if (digits == 0U) {
        return CLKS_FALSE;
    }

    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }

    if (i >= len) {
        return CLKS_FALSE;
    }

    name_start = i;

    while (i < len && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
        i++;
    }

    name_end = i;

    if (name_end <= name_start) {
        return CLKS_FALSE;
    }

    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }

    source_start = i;
    source_end = len;

    while (source_end > source_start &&
           (line[source_end - 1U] == ' ' || line[source_end - 1U] == '\t' || line[source_end - 1U] == '\r')) {
        source_end--;
    }

    *out_addr = addr;
    *out_name = &line[name_start];
    *out_name_len = name_end - name_start;
    *out_source = (source_end > source_start) ? &line[source_start] : CLKS_NULL;
    *out_source_len = (source_end > source_start) ? (source_end - source_start) : 0U;
    return CLKS_TRUE;
}

static clks_bool clks_syscall_symbols_ready(void) {
    const void *data;
    u64 size = 0ULL;

    if (clks_syscall_symbols_checked == CLKS_TRUE) {
        return (clks_syscall_symbols_data != CLKS_NULL && clks_syscall_symbols_size > 0ULL) ? CLKS_TRUE : CLKS_FALSE;
    }

    clks_syscall_symbols_checked = CLKS_TRUE;

    if (clks_fs_is_ready() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    data = clks_fs_read_all(CLKS_SYSCALL_KERNEL_SYMBOL_FILE, &size);

    if (data == CLKS_NULL || size == 0ULL) {
        return CLKS_FALSE;
    }

    clks_syscall_symbols_data = (const char *)data;
    clks_syscall_symbols_size = size;
    return CLKS_TRUE;
}

static clks_bool clks_syscall_lookup_symbol(u64 addr, const char **out_name, usize *out_name_len, u64 *out_base,
                                            const char **out_source, usize *out_source_len) {
    const char *data;
    const char *end;
    const char *line;
    const char *best_name = CLKS_NULL;
    const char *best_source = CLKS_NULL;
    usize best_name_len = 0U;
    usize best_source_len = 0U;
    u64 best_addr = 0ULL;
    clks_bool found = CLKS_FALSE;

    if (out_name == CLKS_NULL || out_name_len == CLKS_NULL || out_base == CLKS_NULL || out_source == CLKS_NULL ||
        out_source_len == CLKS_NULL) {
        return CLKS_FALSE;
    }

    *out_name = CLKS_NULL;
    *out_name_len = 0U;
    *out_base = 0ULL;
    *out_source = CLKS_NULL;
    *out_source_len = 0U;

    if (clks_syscall_symbols_ready() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    data = clks_syscall_symbols_data;
    end = clks_syscall_symbols_data + clks_syscall_symbols_size;

    while (data < end) {
        u64 line_addr;
        const char *line_name;
        usize line_name_len;
        const char *line_source;
        usize line_source_len;
        usize line_len = 0U;

        line = data;

        while (data < end && *data != '\n') {
            data++;
            line_len++;
        }

        if (data < end && *data == '\n') {
            data++;
        }

        if (clks_syscall_parse_symbol_line(line, line_len, &line_addr, &line_name, &line_name_len, &line_source,
                                           &line_source_len) == CLKS_FALSE) {
            continue;
        }

        if (line_addr <= addr && (found == CLKS_FALSE || line_addr >= best_addr)) {
            best_addr = line_addr;
            best_name = line_name;
            best_name_len = line_name_len;
            best_source = line_source;
            best_source_len = line_source_len;
            found = CLKS_TRUE;
        }
    }

    if (found == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    *out_name = best_name;
    *out_name_len = best_name_len;
    *out_base = best_addr;
    *out_source = best_source;
    *out_source_len = best_source_len;
    return CLKS_TRUE;
}

static usize clks_syscall_kdbg_format_symbol_into(char *out, usize out_size, usize pos, u64 addr) {
    const char *sym_name = CLKS_NULL;
    const char *sym_source = CLKS_NULL;
    usize sym_name_len = 0U;
    usize sym_source_len = 0U;
    u64 sym_base = 0ULL;
    clks_bool has_symbol;

    pos = clks_syscall_procfs_append_u64_hex(out, out_size, pos, addr);
    has_symbol = clks_syscall_lookup_symbol(addr, &sym_name, &sym_name_len, &sym_base, &sym_source, &sym_source_len);

    if (has_symbol == CLKS_TRUE) {
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
        pos = clks_syscall_procfs_append_n(out, out_size, pos, sym_name, sym_name_len);
        pos = clks_syscall_procfs_append_char(out, out_size, pos, '+');
        pos = clks_syscall_procfs_append_u64_hex(out, out_size, pos, addr - sym_base);

        if (sym_source != CLKS_NULL && sym_source_len > 0U) {
            pos = clks_syscall_procfs_append_text(out, out_size, pos, " @ ");
            pos = clks_syscall_procfs_append_n(out, out_size, pos, sym_source, sym_source_len);
        }
    } else {
        pos = clks_syscall_procfs_append_text(out, out_size, pos, " <no-symbol>");
    }

    return pos;
}

static usize clks_syscall_kdbg_append_bt_frame(char *out, usize out_size, usize pos, u64 index, u64 rip) {
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '#');
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, index);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
    pos = clks_syscall_kdbg_format_symbol_into(out, out_size, pos, rip);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');
    return pos;
}

static clks_bool clks_syscall_kdbg_stack_ptr_valid(u64 ptr, u64 stack_low, u64 stack_high) {
    if ((ptr & 0x7ULL) != 0ULL) {
        return CLKS_FALSE;
    }

    if (ptr < stack_low || ptr + 16ULL > stack_high) {
        return CLKS_FALSE;
    }

    if (ptr < CLKS_SYSCALL_KERNEL_ADDR_BASE) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static u64 clks_syscall_kdbg_sym(u64 arg0, u64 arg1, u64 arg2) {
    char text[CLKS_SYSCALL_KDBG_TEXT_MAX];
    usize len;

    if (arg1 == 0ULL || arg2 == 0ULL) {
        return 0ULL;
    }

    text[0] = '\0';
    len = clks_syscall_kdbg_format_symbol_into(text, sizeof(text), 0U, arg0);
    return clks_syscall_copy_text_to_user(arg1, arg2, text, len);
}

static u64 clks_syscall_kdbg_regs(u64 arg0, u64 arg1) {
    char text[CLKS_SYSCALL_KDBG_TEXT_MAX];
    usize pos = 0U;
    const struct clks_syscall_frame *frame = &clks_syscall_last_frame;

    if (arg0 == 0ULL || arg1 == 0ULL) {
        return 0ULL;
    }

    text[0] = '\0';

    if (clks_syscall_last_frame_valid == CLKS_FALSE) {
        pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "NO REG SNAPSHOT\n");
        return clks_syscall_copy_text_to_user(arg0, arg1, text, pos);
    }

    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "RAX=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rax);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RBX=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rbx);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RCX=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rcx);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RDX=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rdx);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "RSI=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rsi);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RDI=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rdi);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RBP=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rbp);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RSP=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rsp);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "R8 =");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r8);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " R9 =");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r9);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " R10=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r10);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " R11=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r11);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "R12=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r12);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " R13=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r13);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " R14=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r14);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " R15=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->r15);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "RIP=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rip);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " CS=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->cs);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RFLAGS=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->rflags);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "VECTOR=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->vector);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " ERROR=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->error_code);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " SS=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, frame->ss);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    return clks_syscall_copy_text_to_user(arg0, arg1, text, pos);
}

static u64 clks_syscall_kdbg_bt(u64 arg0) {
    struct clks_syscall_kdbg_bt_req req;
    char text[CLKS_SYSCALL_KDBG_TEXT_MAX];
    usize pos = 0U;
    u64 frame_index = 0ULL;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)arg0, sizeof(req));

    if (req.out_ptr == 0ULL || req.out_size == 0ULL) {
        return 0ULL;
    }

    text[0] = '\0';
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "BT RBP=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, req.rbp);
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, " RIP=");
    pos = clks_syscall_procfs_append_u64_hex(text, sizeof(text), pos, req.rip);
    pos = clks_syscall_procfs_append_char(text, sizeof(text), pos, '\n');

    if (req.rip != 0ULL) {
        pos = clks_syscall_kdbg_append_bt_frame(text, sizeof(text), pos, frame_index, req.rip);
        frame_index++;
    }

#if defined(CLKS_ARCH_X86_64)
    {
        u64 current_rbp = req.rbp;
        u64 current_rsp = 0ULL;
        u64 stack_low;
        u64 stack_high;

        __asm__ volatile("mov %%rsp, %0" : "=r"(current_rsp));

        stack_low = (current_rsp > CLKS_SYSCALL_KDBG_STACK_WINDOW_BYTES)
                        ? (current_rsp - CLKS_SYSCALL_KDBG_STACK_WINDOW_BYTES)
                        : CLKS_SYSCALL_KERNEL_ADDR_BASE;
        stack_high = current_rsp + CLKS_SYSCALL_KDBG_STACK_WINDOW_BYTES;

        if (stack_high < current_rsp) {
            stack_high = 0xFFFFFFFFFFFFFFFFULL;
        }

        if (stack_low < CLKS_SYSCALL_KERNEL_ADDR_BASE) {
            stack_low = CLKS_SYSCALL_KERNEL_ADDR_BASE;
        }

        if (clks_syscall_kdbg_stack_ptr_valid(current_rbp, stack_low, stack_high) == CLKS_TRUE) {
            while (frame_index < CLKS_SYSCALL_KDBG_BT_MAX_FRAMES) {
                const u64 *frame_ptr;
                u64 next_rbp;
                u64 ret_rip;

                frame_ptr = (const u64 *)(usize)current_rbp;
                next_rbp = frame_ptr[0];
                ret_rip = frame_ptr[1];

                if (ret_rip == 0ULL) {
                    break;
                }

                pos = clks_syscall_kdbg_append_bt_frame(text, sizeof(text), pos, frame_index, ret_rip);
                frame_index++;

                if (next_rbp <= current_rbp) {
                    break;
                }

                if (clks_syscall_kdbg_stack_ptr_valid(next_rbp, stack_low, stack_high) == CLKS_FALSE) {
                    break;
                }

                current_rbp = next_rbp;
            }
        } else {
            pos = clks_syscall_procfs_append_text(
                text, sizeof(text), pos, "NOTE: stack walk skipped (rbp not in current kernel stack window)\n");
        }
    }
#else
    pos = clks_syscall_procfs_append_text(text, sizeof(text), pos, "NOTE: stack walk unsupported on this arch\n");
#endif

    return clks_syscall_copy_text_to_user(req.out_ptr, req.out_size, text, pos);
}

static clks_bool clks_syscall_procfs_snapshot_for_path(const char *path, struct clks_exec_proc_snapshot *out_snap) {
    u64 pid;

    if (path == CLKS_NULL || out_snap == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_syscall_procfs_is_self(path) == CLKS_TRUE) {
        pid = clks_exec_current_pid();

        if (pid == 0ULL) {
            return CLKS_FALSE;
        }

        return clks_exec_proc_snapshot(pid, out_snap);
    }

    if (clks_syscall_procfs_parse_pid(path, &pid) == CLKS_TRUE) {
        return clks_exec_proc_snapshot(pid, out_snap);
    }

    return CLKS_FALSE;
}

static usize clks_syscall_procfs_render_snapshot(char *out, usize out_size,
                                                 const struct clks_exec_proc_snapshot *snap) {
    usize pos = 0U;

    if (out == CLKS_NULL || out_size == 0U || snap == CLKS_NULL) {
        return 0U;
    }

    out[0] = '\0';

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "pid=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->pid);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "ppid=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->ppid);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "state=");
    pos = clks_syscall_procfs_append_text(out, out_size, pos, clks_syscall_proc_state_name(snap->state));
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "state_id=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->state);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "tty=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->tty_index);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "runtime_ticks=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->runtime_ticks);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "mem_bytes=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->mem_bytes);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "exit_status=");
    pos = clks_syscall_procfs_append_u64_hex(out, out_size, pos, snap->exit_status);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "last_signal=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->last_signal);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "last_fault_vector=");
    pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap->last_fault_vector);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "last_fault_error=");
    pos = clks_syscall_procfs_append_u64_hex(out, out_size, pos, snap->last_fault_error);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "last_fault_rip=");
    pos = clks_syscall_procfs_append_u64_hex(out, out_size, pos, snap->last_fault_rip);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    pos = clks_syscall_procfs_append_text(out, out_size, pos, "path=");
    pos = clks_syscall_procfs_append_text(out, out_size, pos, snap->path);
    pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');

    return pos;
}

static usize clks_syscall_procfs_render_list(char *out, usize out_size) {
    usize pos = 0U;
    u64 proc_count = clks_exec_proc_count();
    u64 i;

    if (out == CLKS_NULL || out_size == 0U) {
        return 0U;
    }

    out[0] = '\0';
    pos = clks_syscall_procfs_append_text(out, out_size, pos, "pid state tty runtime mem path\n");

    for (i = 0ULL; i < proc_count; i++) {
        u64 pid = 0ULL;
        struct clks_exec_proc_snapshot snap;

        if (clks_exec_proc_pid_at(i, &pid) == CLKS_FALSE || pid == 0ULL) {
            continue;
        }

        if (clks_exec_proc_snapshot(pid, &snap) == CLKS_FALSE) {
            continue;
        }

        pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap.pid);
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
        pos = clks_syscall_procfs_append_text(out, out_size, pos, clks_syscall_proc_state_name(snap.state));
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
        pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap.tty_index);
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
        pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap.runtime_ticks);
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
        pos = clks_syscall_procfs_append_u64_dec(out, out_size, pos, snap.mem_bytes);
        pos = clks_syscall_procfs_append_char(out, out_size, pos, ' ');
        pos = clks_syscall_procfs_append_text(out, out_size, pos, snap.path);
        pos = clks_syscall_procfs_append_char(out, out_size, pos, '\n');
    }

    return pos;
}

static clks_bool clks_syscall_procfs_render_file(const char *path, char *out, usize out_size, usize *out_len) {
    struct clks_exec_proc_snapshot snap;

    if (out_len != CLKS_NULL) {
        *out_len = 0U;
    }

    if (path == CLKS_NULL || out == CLKS_NULL || out_size == 0U || out_len == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_syscall_procfs_is_list(path) == CLKS_TRUE) {
        *out_len = clks_syscall_procfs_render_list(out, out_size);
        return CLKS_TRUE;
    }

    if (clks_syscall_procfs_snapshot_for_path(path, &snap) == CLKS_TRUE) {
        *out_len = clks_syscall_procfs_render_snapshot(out, out_size, &snap);
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}

