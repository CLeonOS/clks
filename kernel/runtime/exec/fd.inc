static clks_bool clks_exec_fd_access_mode_valid(u64 flags) {
    u64 mode = flags & CLKS_EXEC_FD_ACCESS_MASK;
    return (mode == CLKS_EXEC_O_RDONLY || mode == CLKS_EXEC_O_WRONLY || mode == CLKS_EXEC_O_RDWR) ? CLKS_TRUE
                                                                                                  : CLKS_FALSE;
}

static clks_bool clks_exec_fd_can_read(u64 flags) {
    u64 mode = flags & CLKS_EXEC_FD_ACCESS_MASK;
    return (mode == CLKS_EXEC_O_RDONLY || mode == CLKS_EXEC_O_RDWR) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_exec_fd_can_write(u64 flags) {
    u64 mode = flags & CLKS_EXEC_FD_ACCESS_MASK;
    return (mode == CLKS_EXEC_O_WRONLY || mode == CLKS_EXEC_O_RDWR) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_exec_path_is_dev_tty(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/dev/tty") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_exec_path_is_dev_tty0(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/dev/tty0") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_exec_path_is_dev_null(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/dev/null") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_exec_path_is_dev_zero(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/dev/zero") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_exec_path_is_dev_random(const char *path) {
    if (path == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_strcmp(path, "/dev/random") == 0) {
        return CLKS_TRUE;
    }

    return (clks_strcmp(path, "/dev/urandom") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_exec_path_is_dev_fb0(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/dev/fb0") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_exec_path_is_dev_kbd(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/dev/input/kbd") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_exec_path_is_dev_mouse(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/dev/input/mouse") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_exec_path_is_dev_net0(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/dev/net0") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_exec_path_is_dev_disk0(const char *path) {
    return (path != CLKS_NULL && clks_strcmp(path, "/dev/disk0") == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_exec_path_is_new_dev_node(const char *path) {
    if (clks_exec_path_is_dev_fb0(path) == CLKS_TRUE || clks_exec_path_is_dev_kbd(path) == CLKS_TRUE ||
        clks_exec_path_is_dev_mouse(path) == CLKS_TRUE || clks_exec_path_is_dev_net0(path) == CLKS_TRUE ||
        clks_exec_path_is_dev_tty0(path) == CLKS_TRUE || clks_exec_path_is_dev_disk0(path) == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}

static void clks_exec_text_append_char(char *out, usize out_size, usize *io_pos, char ch) {
    if (out == CLKS_NULL || io_pos == CLKS_NULL || out_size == 0U) {
        return;
    }

    if (*io_pos + 1U >= out_size) {
        out[out_size - 1U] = '\0';
        return;
    }

    out[*io_pos] = ch;
    *io_pos = *io_pos + 1U;
    out[*io_pos] = '\0';
}

static void clks_exec_text_append(const char *text, char *out, usize out_size, usize *io_pos) {
    usize i = 0U;

    if (text == CLKS_NULL) {
        return;
    }

    while (text[i] != '\0') {
        clks_exec_text_append_char(out, out_size, io_pos, text[i]);
        i++;
    }
}

static void clks_exec_text_append_u64_dec(u64 value, char *out, usize out_size, usize *io_pos) {
    char tmp[24];
    usize len = 0U;

    if (value == 0ULL) {
        clks_exec_text_append_char(out, out_size, io_pos, '0');
        return;
    }

    while (value != 0ULL && len < sizeof(tmp)) {
        tmp[len] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
        len++;
    }

    while (len != 0U) {
        len--;
        clks_exec_text_append_char(out, out_size, io_pos, tmp[len]);
    }
}

static void clks_exec_text_append_i32_dec(i32 value, char *out, usize out_size, usize *io_pos) {
    u64 mag;

    if (value < 0) {
        clks_exec_text_append_char(out, out_size, io_pos, '-');
        mag = (u64)(-(i64)value);
    } else {
        mag = (u64)value;
    }

    clks_exec_text_append_u64_dec(mag, out, out_size, io_pos);
}

static void clks_exec_text_append_ipv4(u32 ipv4_be, char *out, usize out_size, usize *io_pos) {
    clks_exec_text_append_u64_dec((u64)((ipv4_be >> 24U) & 0xFFU), out, out_size, io_pos);
    clks_exec_text_append_char(out, out_size, io_pos, '.');
    clks_exec_text_append_u64_dec((u64)((ipv4_be >> 16U) & 0xFFU), out, out_size, io_pos);
    clks_exec_text_append_char(out, out_size, io_pos, '.');
    clks_exec_text_append_u64_dec((u64)((ipv4_be >> 8U) & 0xFFU), out, out_size, io_pos);
    clks_exec_text_append_char(out, out_size, io_pos, '.');
    clks_exec_text_append_u64_dec((u64)(ipv4_be & 0xFFU), out, out_size, io_pos);
}

static u64 clks_exec_dev_copy_text(struct clks_exec_fd_entry *entry, const char *text, void *out_buffer, u64 size) {
    u64 text_len;
    u64 read_len;

    if (entry == CLKS_NULL || text == CLKS_NULL || out_buffer == CLKS_NULL || size == 0ULL) {
        return 0ULL;
    }

    text_len = (u64)clks_strlen(text);

    if (entry->offset >= text_len) {
        return 0ULL;
    }

    read_len = text_len - entry->offset;
    if (read_len > size) {
        read_len = size;
    }

    clks_memcpy(out_buffer, (const u8 *)text + (usize)entry->offset, (usize)read_len);
    entry->offset += read_len;
    return read_len;
}

static u64 clks_exec_dev_fb0_read(struct clks_exec_fd_entry *entry, void *out_buffer, u64 size) {
    char text[160];
    usize pos = 0U;
    struct clks_framebuffer_info info;

    clks_memset(text, 0, sizeof(text));

    if (clks_fb_ready() == CLKS_FALSE) {
        clks_exec_text_append("available=0\n", text, sizeof(text), &pos);
        return clks_exec_dev_copy_text(entry, text, out_buffer, size);
    }

    info = clks_fb_info();
    clks_exec_text_append("available=1 width=", text, sizeof(text), &pos);
    clks_exec_text_append_u64_dec((u64)info.width, text, sizeof(text), &pos);
    clks_exec_text_append(" height=", text, sizeof(text), &pos);
    clks_exec_text_append_u64_dec((u64)info.height, text, sizeof(text), &pos);
    clks_exec_text_append(" pitch=", text, sizeof(text), &pos);
    clks_exec_text_append_u64_dec((u64)info.pitch, text, sizeof(text), &pos);
    clks_exec_text_append(" bpp=", text, sizeof(text), &pos);
    clks_exec_text_append_u64_dec((u64)info.bpp, text, sizeof(text), &pos);
    clks_exec_text_append("\n", text, sizeof(text), &pos);
    return clks_exec_dev_copy_text(entry, text, out_buffer, size);
}

static u64 clks_exec_dev_mouse_read(struct clks_exec_fd_entry *entry, void *out_buffer, u64 size) {
    char text[160];
    usize pos = 0U;
    struct clks_mouse_state state;

    clks_memset(text, 0, sizeof(text));
    clks_mouse_snapshot(&state);
    clks_exec_text_append("ready=", text, sizeof(text), &pos);
    clks_exec_text_append_u64_dec((state.ready == CLKS_TRUE) ? 1ULL : 0ULL, text, sizeof(text), &pos);
    clks_exec_text_append(" x=", text, sizeof(text), &pos);
    clks_exec_text_append_i32_dec(state.x, text, sizeof(text), &pos);
    clks_exec_text_append(" y=", text, sizeof(text), &pos);
    clks_exec_text_append_i32_dec(state.y, text, sizeof(text), &pos);
    clks_exec_text_append(" buttons=", text, sizeof(text), &pos);
    clks_exec_text_append_u64_dec((u64)state.buttons, text, sizeof(text), &pos);
    clks_exec_text_append(" packets=", text, sizeof(text), &pos);
    clks_exec_text_append_u64_dec(state.packet_count, text, sizeof(text), &pos);
    clks_exec_text_append("\n", text, sizeof(text), &pos);
    return clks_exec_dev_copy_text(entry, text, out_buffer, size);
}

static u64 clks_exec_dev_net0_read(struct clks_exec_fd_entry *entry, void *out_buffer, u64 size) {
    char text[224];
    usize pos = 0U;

    clks_memset(text, 0, sizeof(text));
    clks_exec_text_append("available=", text, sizeof(text), &pos);
    clks_exec_text_append_u64_dec((clks_net_available() == CLKS_TRUE) ? 1ULL : 0ULL, text, sizeof(text), &pos);
    clks_exec_text_append(" ipv4=", text, sizeof(text), &pos);
    clks_exec_text_append_ipv4(clks_net_ipv4_addr_be(), text, sizeof(text), &pos);
    clks_exec_text_append(" netmask=", text, sizeof(text), &pos);
    clks_exec_text_append_ipv4(clks_net_ipv4_netmask_be(), text, sizeof(text), &pos);
    clks_exec_text_append(" gateway=", text, sizeof(text), &pos);
    clks_exec_text_append_ipv4(clks_net_ipv4_gateway_be(), text, sizeof(text), &pos);
    clks_exec_text_append(" dns=", text, sizeof(text), &pos);
    clks_exec_text_append_ipv4(clks_net_ipv4_dns_be(), text, sizeof(text), &pos);
    clks_exec_text_append("\n", text, sizeof(text), &pos);
    return clks_exec_dev_copy_text(entry, text, out_buffer, size);
}

static u64 clks_exec_dev_disk0_read(struct clks_exec_fd_entry *entry, void *out_buffer, u64 size) {
    char text[192];
    usize pos = 0U;

    clks_memset(text, 0, sizeof(text));
    clks_exec_text_append("present=", text, sizeof(text), &pos);
    clks_exec_text_append_u64_dec((clks_disk_present() == CLKS_TRUE) ? 1ULL : 0ULL, text, sizeof(text), &pos);
    clks_exec_text_append(" bytes=", text, sizeof(text), &pos);
    clks_exec_text_append_u64_dec(clks_disk_size_bytes(), text, sizeof(text), &pos);
    clks_exec_text_append(" sectors=", text, sizeof(text), &pos);
    clks_exec_text_append_u64_dec(clks_disk_sector_count(), text, sizeof(text), &pos);
    clks_exec_text_append(" fat32=", text, sizeof(text), &pos);
    clks_exec_text_append_u64_dec((clks_disk_is_formatted_fat32() == CLKS_TRUE) ? 1ULL : 0ULL, text, sizeof(text),
                                  &pos);
    clks_exec_text_append(" mounted=", text, sizeof(text), &pos);
    clks_exec_text_append_u64_dec((clks_disk_is_mounted() == CLKS_TRUE) ? 1ULL : 0ULL, text, sizeof(text), &pos);
    clks_exec_text_append("\n", text, sizeof(text), &pos);
    return clks_exec_dev_copy_text(entry, text, out_buffer, size);
}

static u64 clks_exec_parse_hex_rgb(const char *text, u64 len, u32 *out_rgb) {
    u64 i = 0ULL;
    u64 value = 0ULL;
    u64 digits = 0ULL;

    if (text == CLKS_NULL || out_rgb == CLKS_NULL) {
        return 0ULL;
    }

    while (i < len && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) {
        i++;
    }

    if (i + 2ULL <= len && text[i] == '0' && (text[i + 1ULL] == 'x' || text[i + 1ULL] == 'X')) {
        i += 2ULL;
    } else if (i < len && text[i] == '#') {
        i++;
    }

    while (i < len) {
        char ch = text[i];
        u64 digit;

        if (ch >= '0' && ch <= '9') {
            digit = (u64)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            digit = (u64)(ch - 'a') + 10ULL;
        } else if (ch >= 'A' && ch <= 'F') {
            digit = (u64)(ch - 'A') + 10ULL;
        } else {
            break;
        }

        if (digits >= 8ULL) {
            return 0ULL;
        }

        value = (value << 4ULL) | digit;
        digits++;
        i++;
    }

    if (digits == 0ULL) {
        return 0ULL;
    }

    *out_rgb = (u32)(value & 0x00FFFFFFULL);
    return 1ULL;
}

static clks_bool clks_exec_buffer_starts_with(const char *buffer, u64 size, const char *prefix) {
    u64 i = 0ULL;

    if (buffer == CLKS_NULL || prefix == CLKS_NULL) {
        return CLKS_FALSE;
    }

    while (prefix[i] != '\0') {
        if (i >= size || buffer[i] != prefix[i]) {
            return CLKS_FALSE;
        }

        i++;
    }

    return CLKS_TRUE;
}

static u64 clks_exec_dev_fb0_write(const void *buffer, u64 size) {
    const char *text = (const char *)buffer;
    struct clks_framebuffer_info info;
    u64 frame_bytes;
    u32 rgb = 0U;

    if (clks_fb_ready() == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_exec_buffer_starts_with(text, size, "clear ") == CLKS_TRUE &&
        clks_exec_parse_hex_rgb(text + 6, size - 6ULL, &rgb) != 0ULL) {
        clks_fb_clear(rgb);
        return size;
    }

    info = clks_fb_info();
    frame_bytes = (u64)info.width * (u64)info.height * 4ULL;

    if (size == frame_bytes && info.bpp == 32U) {
        clks_fb_blit_rgba(0, 0, buffer, info.width, info.height, info.width * 4U);
        return size;
    }

    return (u64)-1;
}

static u8 clks_exec_random_next_byte(void) {
    clks_exec_random_state ^= (clks_interrupts_timer_ticks() + 0x9E3779B97F4A7C15ULL);
    clks_exec_random_state ^= (clks_exec_random_state << 13);
    clks_exec_random_state ^= (clks_exec_random_state >> 7);
    clks_exec_random_state ^= (clks_exec_random_state << 17);
    return (u8)(clks_exec_random_state & 0xFFULL);
}

static void clks_exec_fd_init_defaults(struct clks_exec_proc_record *proc) {
    if (proc == CLKS_NULL) {
        return;
    }

    clks_memset(proc->fds, 0, sizeof(proc->fds));

    proc->fds[0].used = CLKS_TRUE;
    proc->fds[0].kind = CLKS_EXEC_FD_KIND_TTY;
    proc->fds[0].flags = CLKS_EXEC_O_RDONLY;
    proc->fds[0].offset = 0ULL;
    proc->fds[0].tty_index = proc->tty_index;
    proc->fds[0].path[0] = '\0';

    proc->fds[1].used = CLKS_TRUE;
    proc->fds[1].kind = CLKS_EXEC_FD_KIND_TTY;
    proc->fds[1].flags = CLKS_EXEC_O_WRONLY;
    proc->fds[1].offset = 0ULL;
    proc->fds[1].tty_index = proc->tty_index;
    proc->fds[1].path[0] = '\0';

    proc->fds[2].used = CLKS_TRUE;
    proc->fds[2].kind = CLKS_EXEC_FD_KIND_TTY;
    proc->fds[2].flags = CLKS_EXEC_O_WRONLY;
    proc->fds[2].offset = 0ULL;
    proc->fds[2].tty_index = proc->tty_index;
    proc->fds[2].path[0] = '\0';
}

static clks_bool clks_exec_fd_copy_from_parent(struct clks_exec_proc_record *child,
                                               const struct clks_exec_proc_record *parent, u64 parent_fd, u64 child_fd,
                                               clks_bool require_read, clks_bool require_write) {
    const struct clks_exec_fd_entry *src;

    if (child == CLKS_NULL || parent == CLKS_NULL || parent_fd >= CLKS_EXEC_FD_MAX || child_fd >= CLKS_EXEC_FD_MAX) {
        return CLKS_FALSE;
    }

    src = &parent->fds[(u32)parent_fd];

    if (src->used == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (require_read == CLKS_TRUE && clks_exec_fd_can_read(src->flags) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (require_write == CLKS_TRUE && clks_exec_fd_can_write(src->flags) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    child->fds[(u32)child_fd] = *src;

    if (child->fds[(u32)child_fd].kind == CLKS_EXEC_FD_KIND_TTY) {
        child->fds[(u32)child_fd].tty_index = child->tty_index;
    }

    if (child->fds[(u32)child_fd].kind == CLKS_EXEC_FD_KIND_PTY &&
        clks_pty_retain(child->fds[(u32)child_fd].pty_id) == CLKS_FALSE) {
        clks_memset(&child->fds[(u32)child_fd], 0, sizeof(child->fds[(u32)child_fd]));
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static clks_bool clks_exec_fd_apply_stdio_overrides(struct clks_exec_proc_record *child,
                                                    const struct clks_exec_proc_record *parent, u64 stdin_fd,
                                                    u64 stdout_fd, u64 stderr_fd) {
    if (child == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (stdin_fd != CLKS_EXEC_FD_INHERIT) {
        if (clks_exec_fd_copy_from_parent(child, parent, stdin_fd, 0ULL, CLKS_TRUE, CLKS_FALSE) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    if (stdout_fd != CLKS_EXEC_FD_INHERIT) {
        if (clks_exec_fd_copy_from_parent(child, parent, stdout_fd, 1ULL, CLKS_FALSE, CLKS_TRUE) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    if (stderr_fd != CLKS_EXEC_FD_INHERIT) {
        if (clks_exec_fd_copy_from_parent(child, parent, stderr_fd, 2ULL, CLKS_FALSE, CLKS_TRUE) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

static i32 clks_exec_fd_find_free(struct clks_exec_proc_record *proc) {
    u32 i;

    if (proc == CLKS_NULL) {
        return -1;
    }

    for (i = 0U; i < CLKS_EXEC_FD_MAX; i++) {
        if (proc->fds[i].used == CLKS_FALSE) {
            return (i32)i;
        }
    }

    return -1;
}

static struct clks_exec_fd_entry *clks_exec_fd_lookup(struct clks_exec_proc_record *proc, u64 fd) {
    if (proc == CLKS_NULL || fd >= CLKS_EXEC_FD_MAX) {
        return CLKS_NULL;
    }

    if (proc->fds[(u32)fd].used == CLKS_FALSE) {
        return CLKS_NULL;
    }

    return &proc->fds[(u32)fd];
}

static u64 clks_exec_fd_file_read(struct clks_exec_fd_entry *entry, void *out_buffer, u64 size) {
    const void *file_data;
    u64 file_size = 0ULL;
    u64 read_len;

    if (entry == CLKS_NULL || out_buffer == CLKS_NULL || size == 0ULL) {
        return 0ULL;
    }

    if (clks_user_path_read_allowed(entry->path) == CLKS_FALSE) {
        return (u64)-1;
    }

    file_data = clks_fs_read_all(entry->path, &file_size);

    if (file_data == CLKS_NULL) {
        return (u64)-1;
    }

    if (entry->offset >= file_size) {
        return 0ULL;
    }

    read_len = file_size - entry->offset;

    if (read_len > size) {
        read_len = size;
    }

    clks_memcpy(out_buffer, (const u8 *)file_data + (usize)entry->offset, (usize)read_len);
    entry->offset += read_len;
    return read_len;
}

static u64 clks_exec_fd_file_write(struct clks_exec_fd_entry *entry, const void *buffer, u64 size) {
    const void *old_data = CLKS_NULL;
    u64 old_size = 0ULL;
    u64 write_pos;
    u64 new_size;
    void *merged = CLKS_NULL;
    clks_bool ok;

    if (entry == CLKS_NULL) {
        return (u64)-1;
    }

    if (clks_user_path_write_allowed(entry->path) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (size == 0ULL) {
        return 0ULL;
    }

    if (buffer == CLKS_NULL) {
        return (u64)-1;
    }

    if ((entry->flags & CLKS_EXEC_O_APPEND) != 0ULL) {
        if (clks_fs_append(entry->path, buffer, size) == CLKS_FALSE) {
            return (u64)-1;
        }

        entry->offset += size;
        return size;
    }

    old_data = clks_fs_read_all(entry->path, &old_size);

    if (old_data == CLKS_NULL) {
        return (u64)-1;
    }

    write_pos = entry->offset;

    if (write_pos > (0xFFFFFFFFFFFFFFFFULL - size)) {
        return (u64)-1;
    }

    new_size = write_pos + size;

    if (new_size < old_size) {
        new_size = old_size;
    }

    if (new_size == 0ULL) {
        return 0ULL;
    }

    merged = clks_kmalloc((usize)new_size);

    if (merged == CLKS_NULL) {
        return (u64)-1;
    }

    clks_memset(merged, 0, (usize)new_size);

    if (old_size > 0ULL) {
        clks_memcpy(merged, old_data, (usize)old_size);
    }

    clks_memcpy((u8 *)merged + (usize)write_pos, buffer, (usize)size);
    ok = clks_fs_write_all(entry->path, merged, new_size);
    clks_kfree(merged);

    if (ok == CLKS_FALSE) {
        return (u64)-1;
    }

    entry->offset = write_pos + size;
    return size;
}

static void clks_exec_fd_release_entry(struct clks_exec_fd_entry *entry) {
    if (entry == CLKS_NULL || entry->used == CLKS_FALSE) {
        return;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_PTY && entry->pty_id != 0ULL) {
        clks_pty_release(entry->pty_id);
    }

    clks_memset(entry, 0, sizeof(*entry));
}

static void clks_exec_fd_release_all(struct clks_exec_proc_record *proc) {
    u32 i;

    if (proc == CLKS_NULL) {
        return;
    }

    for (i = 0U; i < CLKS_EXEC_FD_MAX; i++) {
        clks_exec_fd_release_entry(&proc->fds[i]);
    }
}

