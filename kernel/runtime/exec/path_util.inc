static clks_bool clks_exec_has_prefix(const char *text, const char *prefix) {
    usize i = 0U;

    if (text == CLKS_NULL || prefix == CLKS_NULL) {
        return CLKS_FALSE;
    }

    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return CLKS_FALSE;
        }

        i++;
    }

    return CLKS_TRUE;
}

static clks_bool clks_exec_has_suffix(const char *text, const char *suffix) {
    usize text_len;
    usize suffix_len;
    usize i;

    if (text == CLKS_NULL || suffix == CLKS_NULL) {
        return CLKS_FALSE;
    }

    text_len = clks_strlen(text);
    suffix_len = clks_strlen(suffix);

    if (suffix_len > text_len) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < suffix_len; i++) {
        if (text[text_len - suffix_len + i] != suffix[i]) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

static clks_bool clks_exec_path_is_user_program(const char *path) {
    if (path == CLKS_NULL || path[0] != '/') {
        return CLKS_FALSE;
    }

    if (clks_exec_has_prefix(path, "/system/") == CLKS_TRUE) {
        return CLKS_FALSE;
    }

    if (clks_exec_has_prefix(path, "/system/drivers/") == CLKS_TRUE) {
        return CLKS_FALSE;
    }

    if (clks_exec_has_prefix(path, "/shell/") == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    return clks_exec_has_suffix(path, ".elf");
}

static clks_bool clks_exec_rip_is_current_user_context(u64 rip) {
    i32 depth_index;
    u64 image_begin;
    u64 image_end;

    if (rip < CLKS_EXEC_KERNEL_ADDR_BASE) {
        return CLKS_TRUE;
    }

    if (clks_exec_pid_stack_depth == 0U) {
        return CLKS_FALSE;
    }

    depth_index = (i32)(clks_exec_pid_stack_depth - 1U);
    image_begin = clks_exec_image_begin_stack[(u32)depth_index];
    image_end = clks_exec_image_end_stack[(u32)depth_index];

    if (image_begin == 0ULL || image_end <= image_begin) {
        return CLKS_FALSE;
    }

    return (rip >= image_begin && rip < image_end) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_exec_translate_legacy_user_rip(u64 rip, u64 *out_rip) {
    i32 depth_index;
    u64 vaddr_begin;
    u64 vaddr_end;
    u64 image_begin;
    u64 image_end;
    u64 off;
    u64 translated;

    if (out_rip == CLKS_NULL || clks_exec_pid_stack_depth == 0U) {
        return CLKS_FALSE;
    }

    depth_index = (i32)(clks_exec_pid_stack_depth - 1U);
    vaddr_begin = clks_exec_image_vaddr_begin_stack[(u32)depth_index];
    vaddr_end = clks_exec_image_vaddr_end_stack[(u32)depth_index];
    image_begin = clks_exec_image_begin_stack[(u32)depth_index];
    image_end = clks_exec_image_end_stack[(u32)depth_index];

    if (vaddr_begin == 0ULL || vaddr_end <= vaddr_begin || image_begin == 0ULL || image_end <= image_begin) {
        return CLKS_FALSE;
    }

    if (rip < vaddr_begin || rip >= vaddr_end) {
        return CLKS_FALSE;
    }

    off = rip - vaddr_begin;

    if (off >= (image_end - image_begin)) {
        return CLKS_FALSE;
    }

    translated = image_begin + off;
    if (translated < image_begin || translated >= image_end) {
        return CLKS_FALSE;
    }

    *out_rip = translated;
    return CLKS_TRUE;
}

static clks_bool clks_exec_translate_loaded_user_rip_to_vaddr(u64 rip, u64 *out_rip) {
    i32 depth_index;
    u64 vaddr_begin;
    u64 vaddr_end;
    u64 image_begin;
    u64 image_end;
    u64 off;
    u64 translated;

    if (out_rip == CLKS_NULL || clks_exec_pid_stack_depth == 0U) {
        return CLKS_FALSE;
    }

    depth_index = (i32)(clks_exec_pid_stack_depth - 1U);
    vaddr_begin = clks_exec_image_vaddr_begin_stack[(u32)depth_index];
    vaddr_end = clks_exec_image_vaddr_end_stack[(u32)depth_index];
    image_begin = clks_exec_image_begin_stack[(u32)depth_index];
    image_end = clks_exec_image_end_stack[(u32)depth_index];

    if (vaddr_begin == 0ULL || vaddr_end <= vaddr_begin || image_begin == 0ULL || image_end <= image_begin) {
        return CLKS_FALSE;
    }

    if (rip < image_begin || rip >= image_end) {
        return CLKS_FALSE;
    }

    off = rip - image_begin;
    if (off >= (vaddr_end - vaddr_begin)) {
        return CLKS_FALSE;
    }

    translated = vaddr_begin + off;
    if (translated < vaddr_begin || translated >= vaddr_end) {
        return CLKS_FALSE;
    }

    *out_rip = translated;
    return CLKS_TRUE;
}

static void clks_exec_copy_path(char *dst, usize dst_size, const char *src) {
    usize i = 0U;

    if (dst == CLKS_NULL || src == CLKS_NULL || dst_size == 0U) {
        return;
    }

    while (src[i] != '\0' && i + 1U < dst_size) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void clks_exec_copy_line(char *dst, usize dst_size, const char *src) {
    usize i = 0U;

    if (dst == CLKS_NULL || dst_size == 0U) {
        return;
    }

    if (src == CLKS_NULL) {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i + 1U < dst_size) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void clks_exec_proc_set_default_user(struct clks_exec_proc_record *proc) {
    if (proc == CLKS_NULL) {
        return;
    }

    proc->uid = CLKS_EXEC_USER_UID_NOBODY;
    proc->user_role = CLKS_EXEC_USER_ROLE_USER;
    proc->user_logged_in = CLKS_FALSE;
    proc->disk_login_required = CLKS_FALSE;
    clks_exec_copy_line(proc->username, sizeof(proc->username), "nobody");
    clks_exec_copy_line(proc->user_home, sizeof(proc->user_home), "/");
}

static void clks_exec_proc_inherit_user(struct clks_exec_proc_record *child,
                                        const struct clks_exec_proc_record *parent) {
    if (child == CLKS_NULL) {
        return;
    }

    if (parent == CLKS_NULL) {
        clks_exec_proc_set_default_user(child);
        return;
    }

    child->uid = parent->uid;
    child->user_role = parent->user_role;
    child->user_logged_in = parent->user_logged_in;
    child->disk_login_required = parent->disk_login_required;
    clks_exec_copy_line(child->username, sizeof(child->username), parent->username);
    clks_exec_copy_line(child->user_home, sizeof(child->user_home), parent->user_home);
}

static clks_bool clks_exec_range_ok(u64 off, u64 len, u64 total) {
    if (off > total) {
        return CLKS_FALSE;
    }

    if (len > (total - off)) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static clks_bool clks_exec_addr_range_in_window(u64 addr, u64 size, u64 begin, u64 end) {
    if (begin == 0ULL || end <= begin) {
        return CLKS_FALSE;
    }

    if (size == 0ULL) {
        return CLKS_FALSE;
    }

    if (addr < begin || addr >= end) {
        return CLKS_FALSE;
    }

    if (size > (end - addr)) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static u64 clks_exec_align_up_u64(u64 value, u64 align) {
    u64 mask;

    if (align == 0ULL) {
        return value;
    }

    mask = align - 1ULL;
    return (value + mask) & ~mask;
}

