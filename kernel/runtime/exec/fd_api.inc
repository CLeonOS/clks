u64 clks_exec_fd_open(const char *path, u64 flags, u64 mode) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();
    struct clks_fs_node_info info;
    i32 fd_slot;
    u8 empty_payload = 0U;

    (void)mode;

    if (proc == CLKS_NULL || path == CLKS_NULL || path[0] != '/') {
        return (u64)-1;
    }

    if (clks_strlen(path) >= CLKS_EXEC_PATH_MAX) {
        return (u64)-1;
    }

    if (clks_exec_fd_access_mode_valid(flags) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (clks_exec_fd_can_read(flags) == CLKS_TRUE && clks_user_path_read_allowed(path) == CLKS_FALSE) {
        return (u64)-1;
    }

    if ((clks_exec_fd_can_write(flags) == CLKS_TRUE || (flags & CLKS_EXEC_O_CREAT) != 0ULL ||
         (flags & CLKS_EXEC_O_TRUNC) != 0ULL || (flags & CLKS_EXEC_O_APPEND) != 0ULL) &&
        clks_user_path_write_allowed(path) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (((flags & CLKS_EXEC_O_TRUNC) != 0ULL || (flags & CLKS_EXEC_O_APPEND) != 0ULL) &&
        clks_exec_fd_can_write(flags) == CLKS_FALSE) {
        return (u64)-1;
    }

    fd_slot = clks_exec_fd_find_free(proc);

    if (fd_slot < 0) {
        return (u64)-1;
    }

    if (clks_exec_path_is_dev_tty(path) == CLKS_TRUE || clks_exec_path_is_dev_tty0(path) == CLKS_TRUE) {
        struct clks_exec_fd_entry *entry = &proc->fds[(u32)fd_slot];

        clks_memset(entry, 0, sizeof(*entry));
        entry->used = CLKS_TRUE;
        entry->kind =
            (clks_exec_path_is_dev_tty0(path) == CLKS_TRUE) ? CLKS_EXEC_FD_KIND_DEV_TTY0 : CLKS_EXEC_FD_KIND_TTY;
        entry->flags = flags;
        entry->offset = 0ULL;
        entry->tty_index = (clks_exec_path_is_dev_tty0(path) == CLKS_TRUE) ? 0U : proc->tty_index;
        entry->path[0] = '\0';
        return (u64)fd_slot;
    }

    if (clks_exec_path_is_dev_null(path) == CLKS_TRUE || clks_exec_path_is_dev_zero(path) == CLKS_TRUE ||
        clks_exec_path_is_dev_random(path) == CLKS_TRUE) {
        struct clks_exec_fd_entry *entry = &proc->fds[(u32)fd_slot];

        clks_memset(entry, 0, sizeof(*entry));
        entry->used = CLKS_TRUE;
        entry->flags = flags;
        entry->offset = 0ULL;
        entry->tty_index = proc->tty_index;
        entry->path[0] = '\0';

        if (clks_exec_path_is_dev_null(path) == CLKS_TRUE) {
            entry->kind = CLKS_EXEC_FD_KIND_DEV_NULL;
        } else if (clks_exec_path_is_dev_zero(path) == CLKS_TRUE) {
            entry->kind = CLKS_EXEC_FD_KIND_DEV_ZERO;
        } else {
            entry->kind = CLKS_EXEC_FD_KIND_DEV_RANDOM;
        }

        return (u64)fd_slot;
    }

    if (clks_exec_path_is_new_dev_node(path) == CLKS_TRUE) {
        struct clks_exec_fd_entry *entry = &proc->fds[(u32)fd_slot];

        clks_memset(entry, 0, sizeof(*entry));
        entry->used = CLKS_TRUE;
        entry->flags = flags;
        entry->offset = 0ULL;
        entry->tty_index = proc->tty_index;
        entry->path[0] = '\0';

        if (clks_exec_path_is_dev_fb0(path) == CLKS_TRUE) {
            entry->kind = CLKS_EXEC_FD_KIND_DEV_FB0;
        } else if (clks_exec_path_is_dev_kbd(path) == CLKS_TRUE) {
            entry->kind = CLKS_EXEC_FD_KIND_DEV_KBD;
        } else if (clks_exec_path_is_dev_mouse(path) == CLKS_TRUE) {
            entry->kind = CLKS_EXEC_FD_KIND_DEV_MOUSE;
        } else if (clks_exec_path_is_dev_net0(path) == CLKS_TRUE) {
            entry->kind = CLKS_EXEC_FD_KIND_DEV_NET0;
        } else if (clks_exec_path_is_dev_disk0(path) == CLKS_TRUE) {
            entry->kind = CLKS_EXEC_FD_KIND_DEV_DISK0;
        } else {
            entry->kind = CLKS_EXEC_FD_KIND_DEV_TTY0;
            entry->tty_index = 0U;
        }

        return (u64)fd_slot;
    }

    if (clks_fs_stat(path, &info) == CLKS_FALSE) {
        if ((flags & CLKS_EXEC_O_CREAT) == 0ULL || clks_exec_fd_can_write(flags) == CLKS_FALSE) {
            return (u64)-1;
        }

        if (clks_fs_write_all(path, &empty_payload, 0ULL) == CLKS_FALSE || clks_fs_stat(path, &info) == CLKS_FALSE) {
            return (u64)-1;
        }
    }

    if (info.type != CLKS_FS_NODE_FILE) {
        return (u64)-1;
    }

    if ((flags & CLKS_EXEC_O_TRUNC) != 0ULL) {
        if (clks_fs_write_all(path, &empty_payload, 0ULL) == CLKS_FALSE || clks_fs_stat(path, &info) == CLKS_FALSE) {
            return (u64)-1;
        }
    }

    {
        struct clks_exec_fd_entry *entry = &proc->fds[(u32)fd_slot];

        clks_memset(entry, 0, sizeof(*entry));
        entry->used = CLKS_TRUE;
        entry->kind = CLKS_EXEC_FD_KIND_FILE;
        entry->flags = flags;
        entry->offset = ((flags & CLKS_EXEC_O_APPEND) != 0ULL) ? info.size : 0ULL;
        entry->tty_index = proc->tty_index;
        clks_exec_copy_path(entry->path, sizeof(entry->path), path);
    }

    return (u64)fd_slot;
}

u64 clks_exec_fd_open_pty(void) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();
    struct clks_exec_fd_entry *entry;
    i32 fd_slot;
    u64 pty_id;

    if (proc == CLKS_NULL) {
        return (u64)-1;
    }

    fd_slot = clks_exec_fd_find_free(proc);
    if (fd_slot < 0) {
        return (u64)-1;
    }

    pty_id = clks_pty_alloc();
    if (pty_id == 0ULL) {
        return (u64)-1;
    }

    entry = &proc->fds[(u32)fd_slot];
    clks_memset(entry, 0, sizeof(*entry));
    entry->used = CLKS_TRUE;
    entry->kind = CLKS_EXEC_FD_KIND_PTY;
    entry->flags = CLKS_EXEC_O_RDWR;
    entry->offset = 0ULL;
    entry->tty_index = proc->tty_index;
    entry->pty_id = pty_id;
    entry->path[0] = '\0';
    return (u64)fd_slot;
}

u64 clks_exec_fd_read(u64 fd, void *out_buffer, u64 size) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();
    struct clks_exec_fd_entry *entry;

    if (proc == CLKS_NULL) {
        return (u64)-1;
    }

    if (size == 0ULL) {
        return 0ULL;
    }

    if (out_buffer == CLKS_NULL) {
        return (u64)-1;
    }

    entry = clks_exec_fd_lookup(proc, fd);

    if (entry == CLKS_NULL || clks_exec_fd_can_read(entry->flags) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_TTY || entry->kind == CLKS_EXEC_FD_KIND_DEV_TTY0 ||
        entry->kind == CLKS_EXEC_FD_KIND_DEV_KBD) {
        u64 count = 0ULL;
        char *dst = (char *)out_buffer;

        while (count < size) {
            char ch;

            if (clks_keyboard_pop_char_for_tty(entry->tty_index, &ch) == CLKS_FALSE) {
                break;
            }

            dst[count] = ch;
            count++;
        }

        if (count == 0ULL) {
            clks_exec_proc_set_block(proc, CLKS_EXEC_BLOCK_REASON_TTY_INPUT, 0ULL, 0ULL);
        } else {
            clks_exec_proc_clear_block(proc);
        }
        return count;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_DEV_FB0) {
        return clks_exec_dev_fb0_read(entry, out_buffer, size);
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_DEV_MOUSE) {
        return clks_exec_dev_mouse_read(entry, out_buffer, size);
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_DEV_NET0) {
        clks_net_poll();
        return clks_exec_dev_net0_read(entry, out_buffer, size);
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_DEV_DISK0) {
        return clks_exec_dev_disk0_read(entry, out_buffer, size);
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_DEV_NULL) {
        return 0ULL;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_DEV_ZERO) {
        clks_memset(out_buffer, 0, (usize)size);
        entry->offset += size;
        return size;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_DEV_RANDOM) {
        u8 *dst = (u8 *)out_buffer;
        u64 i;

        for (i = 0ULL; i < size; i++) {
            dst[(usize)i] = clks_exec_random_next_byte();
        }

        entry->offset += size;
        return size;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_PTY) {
        u64 got = clks_pty_read(entry->pty_id, out_buffer, size);

        if (got != (u64)-1) {
            entry->offset += got;
        }

        if (got == 0ULL) {
            clks_exec_proc_set_block(proc, CLKS_EXEC_BLOCK_REASON_PTY_INPUT, 0ULL, 0ULL);
        } else if (got != (u64)-1) {
            clks_exec_proc_clear_block(proc);
        }
        return got;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_FILE) {
        return clks_exec_fd_file_read(entry, out_buffer, size);
    }

    return (u64)-1;
}

u64 clks_exec_fd_write(u64 fd, const void *buffer, u64 size) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();
    struct clks_exec_fd_entry *entry;

    if (proc == CLKS_NULL) {
        return (u64)-1;
    }

    if (size == 0ULL) {
        return 0ULL;
    }

    if (buffer == CLKS_NULL) {
        return (u64)-1;
    }

    entry = clks_exec_fd_lookup(proc, fd);

    if (entry == CLKS_NULL || clks_exec_fd_can_write(entry->flags) == CLKS_FALSE) {
        return (u64)-1;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_TTY || entry->kind == CLKS_EXEC_FD_KIND_DEV_TTY0) {
        clks_tty_write_n_to(entry->tty_index, (const char *)buffer, (usize)size);
        return size;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_DEV_FB0) {
        u64 wrote = clks_exec_dev_fb0_write(buffer, size);
        if (wrote != (u64)-1) {
            entry->offset += wrote;
        }
        return wrote;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_DEV_NET0) {
        clks_net_poll();
        entry->offset += size;
        return size;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_DEV_KBD || entry->kind == CLKS_EXEC_FD_KIND_DEV_MOUSE ||
        entry->kind == CLKS_EXEC_FD_KIND_DEV_DISK0) {
        return (u64)-1;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_DEV_NULL || entry->kind == CLKS_EXEC_FD_KIND_DEV_ZERO ||
        entry->kind == CLKS_EXEC_FD_KIND_DEV_RANDOM) {
        entry->offset += size;
        return size;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_PTY) {
        u64 wrote = clks_pty_write(entry->pty_id, buffer, size);

        if (wrote != (u64)-1) {
            entry->offset += wrote;
        }

        return wrote;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_FILE) {
        return clks_exec_fd_file_write(entry, buffer, size);
    }

    return (u64)-1;
}

u64 clks_exec_fd_close(u64 fd) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();
    struct clks_exec_fd_entry *entry;

    if (proc == CLKS_NULL) {
        return (u64)-1;
    }

    entry = clks_exec_fd_lookup(proc, fd);

    if (entry == CLKS_NULL) {
        return (u64)-1;
    }

    clks_exec_fd_release_entry(entry);
    return 0ULL;
}

u64 clks_exec_fd_dup(u64 fd) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();
    struct clks_exec_fd_entry *entry;
    i32 fd_slot;

    if (proc == CLKS_NULL) {
        return (u64)-1;
    }

    entry = clks_exec_fd_lookup(proc, fd);

    if (entry == CLKS_NULL) {
        return (u64)-1;
    }

    fd_slot = clks_exec_fd_find_free(proc);

    if (fd_slot < 0) {
        return (u64)-1;
    }

    proc->fds[(u32)fd_slot] = *entry;
    if (proc->fds[(u32)fd_slot].kind == CLKS_EXEC_FD_KIND_PTY &&
        clks_pty_retain(proc->fds[(u32)fd_slot].pty_id) == CLKS_FALSE) {
        clks_memset(&proc->fds[(u32)fd_slot], 0, sizeof(proc->fds[(u32)fd_slot]));
        return (u64)-1;
    }

    return (u64)fd_slot;
}

