static clks_bool clks_wm_owner_allows(const struct clks_wm_window *win, u64 owner_pid) {
    if (win == CLKS_NULL || win->used == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (owner_pid == 0ULL) {
        return CLKS_TRUE;
    }

    return (win->owner_pid == owner_pid) ? CLKS_TRUE : CLKS_FALSE;
}

static i32 clks_wm_find_slot_by_id(u64 id) {
    u32 i;

    if (id == 0ULL) {
        return -1;
    }

    for (i = 0U; i < CLKS_WM_MAX_WINDOWS; i++) {
        if (clks_wm_windows[i].used == CLKS_TRUE && clks_wm_windows[i].id == id) {
            return (i32)i;
        }
    }

    return -1;
}

static i32 clks_wm_find_free_slot(void) {
    u32 i;

    for (i = 0U; i < CLKS_WM_MAX_WINDOWS; i++) {
        if (clks_wm_windows[i].used == CLKS_FALSE) {
            return (i32)i;
        }
    }

    return -1;
}

static void clks_wm_z_remove_slot(u32 slot) {
    u32 i;

    for (i = 0U; i < clks_wm_z_count; i++) {
        if (clks_wm_z_order[i] == slot) {
            u32 j;

            for (j = i; j + 1U < clks_wm_z_count; j++) {
                clks_wm_z_order[j] = clks_wm_z_order[j + 1U];
            }

            if (clks_wm_z_count > 0U) {
                clks_wm_z_count--;
            }

            return;
        }
    }
}

static void clks_wm_z_insert_for_flags(u32 slot) {
    u32 pos = clks_wm_z_count;
    u32 i;

    if (clks_wm_z_count >= CLKS_WM_MAX_WINDOWS || slot >= CLKS_WM_MAX_WINDOWS ||
        clks_wm_windows[slot].used == CLKS_FALSE) {
        return;
    }

    if ((clks_wm_windows[slot].flags & CLKS_WM_FLAG_TOPMOST) == 0ULL) {
        for (i = 0U; i < clks_wm_z_count; i++) {
            u32 existing = clks_wm_z_order[i];
            if (existing < CLKS_WM_MAX_WINDOWS && (clks_wm_windows[existing].flags & CLKS_WM_FLAG_TOPMOST) != 0ULL) {
                pos = i;
                break;
            }
        }
    }

    for (i = clks_wm_z_count; i > pos; i--) {
        clks_wm_z_order[i] = clks_wm_z_order[i - 1U];
    }

    clks_wm_z_order[pos] = slot;
    clks_wm_z_count++;
}

static i32 clks_wm_focused_slot(void) {
    u32 i;

    for (i = 0U; i < CLKS_WM_MAX_WINDOWS; i++) {
        if (clks_wm_windows[i].used == CLKS_TRUE && clks_wm_windows[i].focused == CLKS_TRUE) {
            return (i32)i;
        }
    }

    return -1;
}

static void clks_wm_event_push(u32 slot, u64 type, u64 arg0, u64 arg1, u64 arg2, u64 arg3) {
    struct clks_wm_event_queue *queue;
    u32 write_index;

    if (slot >= CLKS_WM_MAX_WINDOWS || clks_wm_windows[slot].used == CLKS_FALSE) {
        return;
    }

    queue = &clks_wm_windows[slot].queue;
    if (type == CLKS_WM_EVENT_MOUSE_MOVE && queue->count > 0U) {
        u32 last_index = (queue->head + queue->count - 1U) % CLKS_WM_EVENT_QUEUE_CAP;

        if (queue->events[last_index].type == CLKS_WM_EVENT_MOUSE_MOVE) {
            queue->events[last_index].arg0 = arg0;
            queue->events[last_index].arg1 = arg1;
            queue->events[last_index].arg2 = arg2;
            queue->events[last_index].arg3 = arg3;
            return;
        }
    }

    if (queue->count >= CLKS_WM_EVENT_QUEUE_CAP) {
        queue->head = (queue->head + 1U) % CLKS_WM_EVENT_QUEUE_CAP;
        queue->count--;
    }

    write_index = (queue->head + queue->count) % CLKS_WM_EVENT_QUEUE_CAP;
    queue->events[write_index].type = type;
    queue->events[write_index].arg0 = arg0;
    queue->events[write_index].arg1 = arg1;
    queue->events[write_index].arg2 = arg2;
    queue->events[write_index].arg3 = arg3;
    queue->count++;
}

static clks_bool clks_wm_event_pop(u32 slot, struct clks_wm_event *out_event) {
    struct clks_wm_event_queue *queue;

    if (out_event == CLKS_NULL || slot >= CLKS_WM_MAX_WINDOWS || clks_wm_windows[slot].used == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    queue = &clks_wm_windows[slot].queue;
    if (queue->count == 0U) {
        return CLKS_FALSE;
    }

    *out_event = queue->events[queue->head];
    queue->head = (queue->head + 1U) % CLKS_WM_EVENT_QUEUE_CAP;
    queue->count--;
    return CLKS_TRUE;
}

