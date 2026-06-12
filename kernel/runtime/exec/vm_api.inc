u64 clks_exec_vm_alloc(u64 size, u64 flags) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();
    u64 aligned_size;
    u64 base;
    u64 pages;
    u64 page;
    i32 slot;
    u64 vm_flags = CLKS_VM_FLAG_READ | CLKS_VM_FLAG_USER;

    if (proc == CLKS_NULL || size == 0ULL) {
        return 0ULL;
    }

    if (size > CLKS_EXEC_USER_VM_ALLOC_MAX) {
        return 0ULL;
    }

    aligned_size = clks_exec_align_up_u64(size, CLKS_EXEC_USER_VM_ALIGN);
    if (aligned_size == 0ULL || aligned_size < size || aligned_size > CLKS_EXEC_USER_VM_ALLOC_MAX) {
        return 0ULL;
    }

    pages = aligned_size / CLKS_EXEC_USER_VM_ALIGN;
    if (pages == 0ULL || pages > 0xFFFFFFFFULL) {
        return 0ULL;
    }

    slot = clks_exec_user_vm_find_free_area_slot(proc);
    if (slot < 0) {
        return 0ULL;
    }

    base = clks_exec_user_vm_next_base(aligned_size);
    if (base == 0ULL || base < CLKS_EXEC_USER_VM_BASE || base + aligned_size <= base ||
        base + aligned_size > CLKS_EXEC_USER_VM_LIMIT) {
        return 0ULL;
    }

    if ((flags & CLKS_VM_FLAG_WRITE) != 0ULL) {
        vm_flags |= CLKS_VM_FLAG_WRITE;
    }
    if ((flags & CLKS_VM_FLAG_EXEC) != 0ULL) {
        vm_flags |= CLKS_VM_FLAG_EXEC;
    }

    for (page = 0ULL; page < pages; page++) {
        u64 phys = clks_pmm_alloc_page();
        void *page_virt;
        u64 virt = base + (page * CLKS_EXEC_USER_VM_ALIGN);

        if (phys == 0ULL) {
            goto fail_unmap;
        }

        page_virt = clks_boot_phys_to_virt(phys);
        if (page_virt == CLKS_NULL) {
            clks_pmm_free_page(phys);
            goto fail_unmap;
        }

        clks_memset(page_virt, 0, (usize)CLKS_EXEC_USER_VM_ALIGN);

        if (clks_vm_map_page_current(virt, phys, vm_flags) == CLKS_FALSE) {
            clks_pmm_free_page(phys);
            goto fail_unmap;
        }
    }

    clks_exec_user_vm_set_area(&proc->user_vm_areas[(u32)slot], base, aligned_size, size, vm_flags);
    proc->user_vm_bytes += aligned_size;
    return base;

fail_unmap:
    while (page > 0ULL) {
        u64 phys = 0ULL;
        u64 virt;

        page--;
        virt = base + (page * CLKS_EXEC_USER_VM_ALIGN);
        if (clks_vm_unmap_page_current(virt, &phys) == CLKS_TRUE && phys != 0ULL) {
            clks_pmm_free_page(phys);
        }
    }

    return 0ULL;
}

static clks_bool clks_exec_user_vm_copy_from_kernel(u64 dst_addr, const void *src, u64 size) {
    u64 copied = 0ULL;

    if (dst_addr == 0ULL || src == CLKS_NULL || size == 0ULL) {
        return (size == 0ULL) ? CLKS_TRUE : CLKS_FALSE;
    }

    while (copied < size) {
        u64 phys = 0ULL;
        void *page_virt;
        u64 page_off = (dst_addr + copied) & (CLKS_EXEC_USER_VM_ALIGN - 1ULL);
        u64 chunk = CLKS_EXEC_USER_VM_ALIGN - page_off;

        if (chunk > (size - copied)) {
            chunk = size - copied;
        }

        if (clks_vm_translate_current(dst_addr + copied, &phys, CLKS_NULL) == CLKS_FALSE || phys == 0ULL) {
            return CLKS_FALSE;
        }

        page_virt = clks_boot_phys_to_virt((phys & ~(CLKS_EXEC_USER_VM_ALIGN - 1ULL)) + page_off);
        if (page_virt == CLKS_NULL) {
            return CLKS_FALSE;
        }

        clks_memcpy(page_virt, (const u8 *)src + (usize)copied, (usize)chunk);
        copied += chunk;
    }

    return CLKS_TRUE;
}

static clks_bool clks_exec_mmap_file_private(u64 base, u64 size, struct clks_exec_fd_entry *entry, u64 offset) {
    const void *file_data;
    u64 file_size = 0ULL;
    u64 copy_len;

    if (base == 0ULL || size == 0ULL || entry == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (entry->kind == CLKS_EXEC_FD_KIND_DEV_ZERO) {
        return CLKS_TRUE;
    }

    if (entry->kind != CLKS_EXEC_FD_KIND_FILE || entry->path[0] == '\0') {
        return CLKS_FALSE;
    }

    if (clks_user_path_read_allowed(entry->path) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    file_data = clks_fs_read_all(entry->path, &file_size);
    if (file_data == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (offset >= file_size) {
        return CLKS_TRUE;
    }

    copy_len = file_size - offset;
    if (copy_len > size) {
        copy_len = size;
    }

    return clks_exec_user_vm_copy_from_kernel(base, (const u8 *)file_data + (usize)offset, copy_len);
}

static clks_bool clks_exec_user_vm_protect_range(u64 base, u64 size, u64 flags) {
    u64 aligned_size;
    u64 pages;
    u64 page;

    if (base == 0ULL || size == 0ULL || (base & (CLKS_EXEC_USER_VM_ALIGN - 1ULL)) != 0ULL) {
        return CLKS_FALSE;
    }

    aligned_size = clks_exec_align_up_u64(size, CLKS_EXEC_USER_VM_ALIGN);
    if (aligned_size == 0ULL || aligned_size < size) {
        return CLKS_FALSE;
    }

    pages = aligned_size / CLKS_EXEC_USER_VM_ALIGN;
    for (page = 0ULL; page < pages; page++) {
        if (clks_vm_protect_page_current(base + (page * CLKS_EXEC_USER_VM_ALIGN), flags) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

u64 clks_exec_mmap(u64 addr_hint, u64 size, u64 prot, u64 flags, u64 fd, u64 offset) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();
    struct clks_exec_fd_entry *entry = CLKS_NULL;
    u64 vm_flags = CLKS_VM_FLAG_USER;
    u64 alloc_flags;
    u64 base;
    u64 aligned_size;
    clks_bool is_anonymous;

    if (proc == CLKS_NULL || size == 0ULL) {
        return 0ULL;
    }

    if (addr_hint != 0ULL || (flags & CLKS_EXEC_MMAP_FLAG_FIXED) != 0ULL) {
        return 0ULL;
    }

    if ((flags & CLKS_EXEC_MMAP_FLAG_PRIVATE) == 0ULL || (flags & CLKS_EXEC_MMAP_FLAG_SHARED) != 0ULL) {
        return 0ULL;
    }

    if (size > CLKS_EXEC_USER_VM_ALLOC_MAX) {
        return 0ULL;
    }

    aligned_size = clks_exec_align_up_u64(size, CLKS_EXEC_USER_VM_ALIGN);
    if (aligned_size == 0ULL || aligned_size < size || aligned_size > CLKS_EXEC_USER_VM_ALLOC_MAX) {
        return 0ULL;
    }

    if ((prot & CLKS_VM_FLAG_READ) != 0ULL) {
        vm_flags |= CLKS_VM_FLAG_READ;
    }
    if ((prot & CLKS_VM_FLAG_WRITE) != 0ULL) {
        vm_flags |= CLKS_VM_FLAG_WRITE;
    }
    if ((prot & CLKS_VM_FLAG_EXEC) != 0ULL) {
        vm_flags |= CLKS_VM_FLAG_EXEC;
    }

    if ((vm_flags & (CLKS_VM_FLAG_READ | CLKS_VM_FLAG_WRITE | CLKS_VM_FLAG_EXEC)) == 0ULL) {
        vm_flags |= CLKS_VM_FLAG_READ;
    }

    is_anonymous = ((flags & CLKS_EXEC_MMAP_FLAG_ANONYMOUS) != 0ULL) ? CLKS_TRUE : CLKS_FALSE;
    if (is_anonymous == CLKS_TRUE) {
        if (fd != (u64)-1 || offset != 0ULL) {
            return 0ULL;
        }
        return clks_exec_vm_alloc(size, vm_flags);
    }

    entry = clks_exec_fd_lookup(proc, fd);
    if (entry == CLKS_NULL || clks_exec_fd_can_read(entry->flags) == CLKS_FALSE) {
        return 0ULL;
    }

    alloc_flags = vm_flags | CLKS_VM_FLAG_READ | CLKS_VM_FLAG_WRITE;
    base = clks_exec_vm_alloc(size, alloc_flags);
    if (base == 0ULL) {
        return 0ULL;
    }

    if (clks_exec_mmap_file_private(base, size, entry, offset) == CLKS_FALSE) {
        (void)clks_exec_vm_free(base, aligned_size);
        return 0ULL;
    }

    if (clks_exec_user_vm_protect_range(base, size, vm_flags) == CLKS_FALSE) {
        (void)clks_exec_vm_free(base, aligned_size);
        return 0ULL;
    }

    {
        u32 i;

        for (i = 0U; i < CLKS_EXEC_USER_VM_AREA_MAX; i++) {
            if ((u64)(usize)proc->user_vm_areas[i].ptr == base) {
                proc->user_vm_areas[i].flags = vm_flags;
                break;
            }
        }
    }

    return base;
}

u64 clks_exec_vm_free(u64 addr, u64 size) {
    struct clks_exec_proc_record *proc = clks_exec_current_proc();
    u32 i;

    if (proc == CLKS_NULL || addr == 0ULL || (addr & (CLKS_EXEC_USER_VM_ALIGN - 1ULL)) != 0ULL) {
        return 0ULL;
    }

    for (i = 0U; i < CLKS_EXEC_USER_VM_AREA_MAX; i++) {
        u64 begin = (u64)(usize)proc->user_vm_areas[i].ptr;
        u64 area_size;
        u64 area_end;
        u64 unmap_size;
        u64 unmap_end;
        u64 page;
        u64 pages;

        if (begin == 0ULL) {
            continue;
        }

        area_size = proc->user_vm_areas[i].size;
        if (area_size == 0ULL || (area_size & (CLKS_EXEC_USER_VM_ALIGN - 1ULL)) != 0ULL) {
            continue;
        }

        area_end = begin + area_size;
        if (area_end <= begin || addr < begin || addr >= area_end) {
            continue;
        }

        unmap_size = (size == 0ULL) ? (area_end - addr) : clks_exec_align_up_u64(size, CLKS_EXEC_USER_VM_ALIGN);
        if (unmap_size == 0ULL || unmap_size < size) {
            return 0ULL;
        }

        unmap_end = addr + unmap_size;
        if (unmap_end <= addr || unmap_end > area_end) {
            return 0ULL;
        }

        if (addr > begin && unmap_end < area_end && clks_exec_user_vm_find_free_area_slot(proc) < 0) {
            return 0ULL;
        }

        pages = unmap_size / CLKS_EXEC_USER_VM_ALIGN;
        for (page = 0ULL; page < pages; page++) {
            u64 phys = 0ULL;
            u64 virt = addr + (page * CLKS_EXEC_USER_VM_ALIGN);

            if (clks_vm_unmap_page_current(virt, &phys) == CLKS_TRUE && phys != 0ULL) {
                clks_pmm_free_page(phys);
            }
        }

        if (proc->user_vm_bytes >= unmap_size) {
            proc->user_vm_bytes -= unmap_size;
        } else {
            proc->user_vm_bytes = 0ULL;
        }

        if (addr == begin && unmap_end == area_end) {
            clks_exec_user_vm_clear_area(&proc->user_vm_areas[i]);
        } else if (addr == begin) {
            u64 new_size = area_end - unmap_end;

            clks_exec_user_vm_set_area(&proc->user_vm_areas[i], unmap_end, new_size, new_size,
                                       proc->user_vm_areas[i].flags);
        } else if (unmap_end == area_end) {
            u64 new_size = addr - begin;

            clks_exec_user_vm_set_area(&proc->user_vm_areas[i], begin, new_size, new_size,
                                       proc->user_vm_areas[i].flags);
        } else {
            i32 tail_slot = clks_exec_user_vm_find_free_area_slot(proc);
            u64 tail_size = area_end - unmap_end;
            u64 head_size = addr - begin;

            if (tail_slot < 0) {
                return 0ULL;
            }

            clks_exec_user_vm_set_area(&proc->user_vm_areas[(u32)tail_slot], unmap_end, tail_size, tail_size,
                                       proc->user_vm_areas[i].flags);
            clks_exec_user_vm_set_area(&proc->user_vm_areas[i], begin, head_size, head_size,
                                       proc->user_vm_areas[i].flags);
        }

        return 1ULL;
    }

    return 0ULL;
}

u64 clks_exec_user_heap_alloc(u64 size) {
    return clks_exec_vm_alloc(size, CLKS_VM_FLAG_READ | CLKS_VM_FLAG_WRITE);
}

