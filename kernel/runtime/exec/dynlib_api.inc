u64 clks_exec_dl_open(const char *path) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();
    const void *image;
    u64 image_size = 0ULL;
    i32 existing_slot;
    i32 slot;
    u64 owner_pid;
    struct clks_exec_dynlib_slot *dyn;

    if (proc == CLKS_NULL || path == CLKS_NULL || path[0] != '/') {
        return (u64)-1;
    }

    if (clks_strlen(path) >= CLKS_EXEC_PATH_MAX) {
        return (u64)-1;
    }

    owner_pid = proc->pid;
    existing_slot = clks_exec_dynlib_find_by_owner_path(owner_pid, path);

    if (existing_slot >= 0) {
        dyn = &clks_exec_dynlib_table[(u32)existing_slot];
        dyn->ref_count++;
        return dyn->handle;
    }

    slot = clks_exec_dynlib_alloc_slot();
    if (slot < 0) {
        return (u64)-1;
    }

    image = clks_fs_read_all(path, &image_size);
    if (image == CLKS_NULL || image_size == 0ULL) {
        return (u64)-1;
    }

    dyn = &clks_exec_dynlib_table[(u32)slot];
    clks_memset(dyn, 0, sizeof(*dyn));

    if (clks_elf64_load(image, image_size, &dyn->loaded) == CLKS_FALSE) {
        clks_memset(dyn, 0, sizeof(*dyn));
        return (u64)-1;
    }

    dyn->used = CLKS_TRUE;
    dyn->handle = clks_exec_dynlib_alloc_handle();
    dyn->owner_pid = owner_pid;
    dyn->ref_count = 1ULL;
    dyn->file_image = image;
    dyn->file_size = image_size;
    clks_exec_copy_path(dyn->path, sizeof(dyn->path), path);

    clks_exec_log_info_serial("DLOPEN OK");
    clks_exec_log_info_serial(path);
    clks_exec_log_hex_serial("DL_HANDLE", dyn->handle);
    return dyn->handle;
}

u64 clks_exec_dl_close(u64 handle) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();
    i32 slot;
    struct clks_exec_dynlib_slot *dyn;

    if (proc == CLKS_NULL || handle == 0ULL) {
        return (u64)-1;
    }

    slot = clks_exec_dynlib_find_by_handle(handle);
    if (slot < 0) {
        return (u64)-1;
    }

    dyn = &clks_exec_dynlib_table[(u32)slot];
    if (dyn->owner_pid != proc->pid) {
        return (u64)-1;
    }

    if (dyn->ref_count > 1ULL) {
        dyn->ref_count--;
        return 0ULL;
    }

    clks_exec_dynlib_slot_reset(dyn);
    return 0ULL;
}

u64 clks_exec_dl_sym(u64 handle, const char *symbol) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();
    i32 slot;
    struct clks_exec_dynlib_slot *dyn;
    u64 addr = 0ULL;

    if (proc == CLKS_NULL || handle == 0ULL || symbol == CLKS_NULL || symbol[0] == '\0') {
        return 0ULL;
    }

    slot = clks_exec_dynlib_find_by_handle(handle);
    if (slot < 0) {
        return 0ULL;
    }

    dyn = &clks_exec_dynlib_table[(u32)slot];
    if (dyn->owner_pid != proc->pid) {
        return 0ULL;
    }

    if (clks_exec_dynlib_resolve_symbol(dyn, symbol, &addr) == CLKS_FALSE) {
        return 0ULL;
    }

    return addr;
}

