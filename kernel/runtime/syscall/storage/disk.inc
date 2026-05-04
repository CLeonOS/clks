static u64 clks_syscall_kernel_version(u64 arg0, u64 arg1) {
    /* Version query: tiny syscall, huge bike-shed potential. */
    usize len = clks_strlen(CLKS_VERSION_STRING);
    return clks_syscall_copy_text_to_user(arg0, arg1, CLKS_VERSION_STRING, len);
}

static void clks_syscall_copy_const_text(char *dst, usize dst_size, const char *src) {
    usize i = 0U;

    if (dst == CLKS_NULL || dst_size == 0U) {
        return;
    }

    if (src == CLKS_NULL) {
        dst[0] = '\0';
        return;
    }

    while (i + 1U < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static u64 clks_syscall_sysinfo(u64 arg0, u64 arg1) {
    struct clks_syscall_sysinfo_user info;
    struct clks_pmm_stats pmm;
    struct clks_heap_stats heap;
    struct clks_scheduler_stats sched;
    u64 hz;

    if (arg0 == 0ULL || arg1 < (u64)sizeof(info)) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg0, (u64)sizeof(info)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memset(&info, 0, sizeof(info));
    pmm = clks_pmm_get_stats();
    heap = clks_heap_get_stats();
    sched = clks_scheduler_get_stats();
    hz = (u64)clks_interrupts_timer_hz();

    clks_syscall_copy_const_text(info.kernel_name, sizeof(info.kernel_name), "CLeonKernelSystem");
    clks_syscall_copy_const_text(info.kernel_version, sizeof(info.kernel_version), CLKS_VERSION_STRING);
    clks_syscall_copy_const_text(info.arch, sizeof(info.arch), "x86_64");
    clks_syscall_copy_const_text(info.build_date, sizeof(info.build_date), __DATE__);
    clks_syscall_copy_const_text(info.build_time, sizeof(info.build_time), __TIME__);
    clks_syscall_copy_const_text(info.boot_mode, sizeof(info.boot_mode),
                                 (clks_disk_is_mounted() == CLKS_TRUE && clks_disk_mount_path() != CLKS_NULL &&
                                  clks_strcmp(clks_disk_mount_path(), "/") == 0)
                                     ? "disk"
                                     : "iso");

    info.timer_ticks = clks_interrupts_timer_ticks();
    info.timer_hz = hz;
    info.uptime_ms = (hz != 0ULL) ? ((info.timer_ticks * 1000ULL) / hz) : 0ULL;
    info.managed_pages = pmm.managed_pages;
    info.free_pages = pmm.free_pages;
    info.used_pages = pmm.used_pages;
    info.dropped_pages = pmm.dropped_pages;
    info.heap_total_bytes = (u64)heap.total_bytes;
    info.heap_used_bytes = (u64)heap.used_bytes;
    info.heap_free_bytes = (u64)heap.free_bytes;
    info.fs_nodes = clks_fs_node_count();
    info.task_count = sched.task_count;
    info.service_count = clks_service_count();
    info.service_ready_count = clks_service_ready_count();

    clks_memcpy((void *)(usize)arg0, &info, sizeof(info));
    return 1ULL;
}

static u64 clks_syscall_disk_present(void) {
    return (clks_disk_present() == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_disk_size_bytes(void) {
    return clks_disk_size_bytes();
}

static u64 clks_syscall_disk_sector_count(void) {
    return clks_disk_sector_count();
}

static u64 clks_syscall_disk_formatted(void) {
    return (clks_disk_is_formatted_fat32() == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_disk_format_fat32(u64 arg0) {
    char label[16];

    if (clks_user_privileged_operation_allowed() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_copy_user_optional_string(arg0, label, sizeof(label)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_disk_format_fat32((label[0] != '\0') ? label : CLKS_NULL) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_disk_mount(u64 arg0) {
    char path[CLKS_SYSCALL_PATH_MAX];

    if (clks_user_privileged_operation_allowed() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_copy_user_string(arg0, path, sizeof(path)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_disk_mount(path) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_disk_mounted(void) {
    return (clks_disk_is_mounted() == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_disk_mount_path(u64 arg0, u64 arg1) {
    const char *mount_path = clks_disk_mount_path();

    if (mount_path == CLKS_NULL || mount_path[0] == '\0') {
        return 0ULL;
    }

    return clks_syscall_copy_text_to_user(arg0, arg1, mount_path, clks_strlen(mount_path));
}

static u64 clks_syscall_disk_read_sector(u64 arg0, u64 arg1) {
    u8 sector[CLKS_SYSCALL_DISK_SECTOR_BYTES];

    if (arg1 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, (u64)CLKS_SYSCALL_DISK_SECTOR_BYTES) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_disk_read_sector(arg0, (void *)sector) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy((void *)arg1, sector, (usize)CLKS_SYSCALL_DISK_SECTOR_BYTES);
    return 1ULL;
}

static u64 clks_syscall_disk_write_sector(u64 arg0, u64 arg1) {
    u8 sector[CLKS_SYSCALL_DISK_SECTOR_BYTES];

    if (clks_user_privileged_operation_allowed() == CLKS_FALSE) {
        return 0ULL;
    }

    if (arg1 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg1, (u64)CLKS_SYSCALL_DISK_SECTOR_BYTES) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(sector, (const void *)arg1, (usize)CLKS_SYSCALL_DISK_SECTOR_BYTES);
    return (clks_disk_write_sector(arg0, (const void *)sector) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_disk_fsck_fat32(u64 arg0, u64 arg1) {
    struct clks_disk_fsck_result result;

    if (arg1 == 0ULL) {
        return 0ULL;
    }

    if ((arg0 & CLKS_DISK_FSCK_FLAG_FIX) != 0ULL && clks_user_privileged_operation_allowed() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, (u64)sizeof(result)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memset(&result, 0, sizeof(result));
    if (clks_disk_fsck_fat32(arg0, &result) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy((void *)arg1, &result, sizeof(result));
    return 1ULL;
}
