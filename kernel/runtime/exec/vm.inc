static u64 clks_exec_user_vm_next_base(u64 size) {
    u64 cursor;
    u32 guard = 0U;

    if (size == 0ULL || size > (CLKS_EXEC_USER_VM_LIMIT - CLKS_EXEC_USER_VM_BASE)) {
        return 0ULL;
    }

    if (clks_exec_next_vm_base < CLKS_EXEC_USER_VM_BASE || clks_exec_next_vm_base >= CLKS_EXEC_USER_VM_LIMIT) {
        clks_exec_next_vm_base = CLKS_EXEC_USER_VM_BASE;
    }

    cursor = clks_exec_align_up_u64(clks_exec_next_vm_base, CLKS_EXEC_USER_VM_ALIGN);

    for (;;) {
        clks_bool moved = CLKS_FALSE;
        u32 i;

        if (cursor < CLKS_EXEC_USER_VM_BASE || cursor + size <= cursor || cursor + size > CLKS_EXEC_USER_VM_LIMIT) {
            cursor = CLKS_EXEC_USER_VM_BASE;
        }

        for (i = 0U; i < CLKS_EXEC_MAX_PROCS; i++) {
            const struct clks_exec_proc_record *proc = &clks_exec_proc_table[i];
            u32 j;

            if (proc->used == CLKS_FALSE) {
                continue;
            }

            for (j = 0U; j < CLKS_EXEC_USER_VM_AREA_MAX; j++) {
                u64 begin = (u64)(usize)proc->user_vm_areas[j].ptr;
                u64 end;

                if (begin == 0ULL || proc->user_vm_areas[j].size == 0ULL) {
                    continue;
                }

                end = begin + proc->user_vm_areas[j].size;
                if (end <= begin) {
                    continue;
                }

                if (cursor < end && (cursor + size) > begin) {
                    cursor = clks_exec_align_up_u64(end + CLKS_EXEC_USER_VM_ALIGN, CLKS_EXEC_USER_VM_ALIGN);
                    moved = CLKS_TRUE;
                }
            }
        }

        if (moved == CLKS_FALSE) {
            clks_exec_next_vm_base = cursor + size + CLKS_EXEC_USER_VM_ALIGN;
            return cursor;
        }

        guard++;
        if (guard > (CLKS_EXEC_MAX_PROCS * CLKS_EXEC_USER_VM_AREA_MAX) || cursor >= CLKS_EXEC_USER_VM_LIMIT) {
            return 0ULL;
        }
    }
}

static u64 clks_exec_proc_address_space(const struct clks_exec_proc_record *proc) {
    if (proc != CLKS_NULL && proc->address_space_cr3 != 0ULL) {
        return proc->address_space_cr3;
    }

    return clks_vm_kernel_cr3();
}

static void clks_exec_switch_address_space(u64 target_cr3, u64 *out_prev_cr3, clks_bool *out_switched) {
    u64 prev_cr3 = clks_vm_current_cr3();

    if (out_prev_cr3 != CLKS_NULL) {
        *out_prev_cr3 = prev_cr3;
    }
    if (out_switched != CLKS_NULL) {
        *out_switched = CLKS_FALSE;
    }

    if (target_cr3 == 0ULL || prev_cr3 == 0ULL || target_cr3 == prev_cr3) {
        return;
    }

    clks_vm_switch_cr3(target_cr3);

    if (out_switched != CLKS_NULL) {
        *out_switched = CLKS_TRUE;
    }
}

static void clks_exec_restore_address_space(u64 prev_cr3, clks_bool switched) {
    if (switched == CLKS_TRUE && prev_cr3 != 0ULL) {
        clks_vm_switch_cr3(prev_cr3);
    }
}

static void clks_exec_proc_release_user_vm(struct clks_exec_proc_record *proc) {
    u32 i;
    u64 prev_cr3 = 0ULL;
    clks_bool switched_cr3 = CLKS_FALSE;

    if (proc == CLKS_NULL) {
        return;
    }

    clks_exec_switch_address_space(clks_exec_proc_address_space(proc), &prev_cr3, &switched_cr3);

    for (i = 0U; i < CLKS_EXEC_USER_VM_AREA_MAX; i++) {
        if (proc->user_vm_areas[i].ptr != CLKS_NULL) {
            u64 base = (u64)(usize)proc->user_vm_areas[i].ptr;
            u64 pages = (u64)proc->user_vm_areas[i].page_count;
            u64 page;

            for (page = 0ULL; page < pages; page++) {
                u64 phys = 0ULL;
                u64 virt = base + (page * CLKS_EXEC_USER_VM_ALIGN);

                if (clks_vm_unmap_page_current(virt, &phys) == CLKS_TRUE && phys != 0ULL) {
                    clks_pmm_free_page(phys);
                }
            }

            proc->user_vm_areas[i].ptr = CLKS_NULL;
            proc->user_vm_areas[i].size = 0ULL;
            proc->user_vm_areas[i].requested_size = 0ULL;
            proc->user_vm_areas[i].flags = 0ULL;
            proc->user_vm_areas[i].page_count = 0U;
        }
    }

    proc->user_vm_bytes = 0ULL;
    clks_exec_restore_address_space(prev_cr3, switched_cr3);
}

static clks_bool clks_exec_current_user_vm_ptr_range(u64 addr, u64 size, u64 required_flags) {
    const struct clks_exec_proc_record *proc = clks_exec_current_proc();
    u32 i;

    if (proc == CLKS_NULL) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < CLKS_EXEC_USER_VM_AREA_MAX; i++) {
        u64 begin = (u64)(usize)proc->user_vm_areas[i].ptr;
        u64 end;

        if (begin == 0ULL || proc->user_vm_areas[i].size == 0ULL) {
            continue;
        }

        end = begin + proc->user_vm_areas[i].size;
        if (end <= begin) {
            continue;
        }

        if (clks_exec_addr_range_in_window(addr, size, begin, end) == CLKS_TRUE &&
            (proc->user_vm_areas[i].flags & required_flags) == required_flags) {
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static i32 clks_exec_user_vm_find_free_area_slot(struct clks_exec_proc_record *proc) {
    u32 i;

    if (proc == CLKS_NULL) {
        return -1;
    }

    for (i = 0U; i < CLKS_EXEC_USER_VM_AREA_MAX; i++) {
        if (proc->user_vm_areas[i].ptr == CLKS_NULL) {
            return (i32)i;
        }
    }

    return -1;
}

static void clks_exec_user_vm_clear_area(struct clks_exec_user_vm_area *area) {
    if (area == CLKS_NULL) {
        return;
    }

    area->ptr = CLKS_NULL;
    area->size = 0ULL;
    area->requested_size = 0ULL;
    area->flags = 0ULL;
    area->page_count = 0U;
}

static void clks_exec_user_vm_set_area(struct clks_exec_user_vm_area *area, u64 base, u64 size, u64 requested_size,
                                       u64 flags) {
    if (area == CLKS_NULL) {
        return;
    }

    area->ptr = (void *)(usize)base;
    area->size = size;
    area->requested_size = requested_size;
    area->flags = flags;
    area->page_count = (u32)(size / CLKS_EXEC_USER_VM_ALIGN);
}

