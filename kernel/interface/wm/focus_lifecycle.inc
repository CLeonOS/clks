static void clks_wm_set_focus_slot(i32 new_focus_slot) {
    i32 old_focus_slot = clks_wm_focused_slot();

    if (old_focus_slot == new_focus_slot) {
        return;
    }

    if (old_focus_slot >= 0) {
        u32 old_slot = (u32)old_focus_slot;
        clks_wm_mark_dirty_window(&clks_wm_windows[old_slot]);
        clks_wm_windows[old_slot].focused = CLKS_FALSE;
        clks_wm_event_push(old_slot, CLKS_WM_EVENT_FOCUS_LOST, 0ULL, 0ULL, 0ULL, 0ULL);
    }

    if (new_focus_slot >= 0) {
        u32 new_slot = (u32)new_focus_slot;
        clks_wm_mark_dirty_window(&clks_wm_windows[new_slot]);
        clks_wm_windows[new_slot].focused = CLKS_TRUE;
        clks_wm_z_remove_slot(new_slot);
        clks_wm_z_insert_for_flags(new_slot);
        clks_wm_mark_dirty_window(&clks_wm_windows[new_slot]);
        clks_wm_event_push(new_slot, CLKS_WM_EVENT_FOCUS_GAINED, 1ULL, 0ULL, 0ULL, 0ULL);
    }
}

#if CLKS_CFG_WM_INPUT_DISPATCH != 0
static i32 clks_wm_window_hit_test(i32 x, i32 y) {
    i32 zi;

    for (zi = (i32)clks_wm_z_count - 1; zi >= 0; zi--) {
        u32 slot = clks_wm_z_order[(u32)zi];
        const struct clks_wm_window *win = &clks_wm_windows[slot];
        i32 left;
        i32 top;
        i32 right;
        i32 bottom;

        if (win->used == CLKS_FALSE) {
            continue;
        }

        left = win->x;
        top = win->y;
        right = win->x + (i32)win->width;
        bottom = win->y + (i32)win->height;

        if (x >= left && x < right && y >= top && y < bottom) {
            return (i32)slot;
        }
    }

    return -1;
}
#endif

static void clks_wm_remove_slot(u32 slot) {
    clks_bool was_focused;

    if (slot >= CLKS_WM_MAX_WINDOWS || clks_wm_windows[slot].used == CLKS_FALSE) {
        return;
    }

    was_focused = clks_wm_windows[slot].focused;
    clks_wm_mark_dirty_window(&clks_wm_windows[slot]);

    if (clks_wm_windows[slot].pixels != CLKS_NULL) {
        clks_kfree(clks_wm_windows[slot].pixels);
    }

    clks_memset(&clks_wm_windows[slot], 0, sizeof(clks_wm_windows[slot]));
    clks_wm_z_remove_slot(slot);

    if (was_focused == CLKS_TRUE) {
        if (clks_wm_z_count > 0U) {
            clks_wm_set_focus_slot((i32)clks_wm_z_order[clks_wm_z_count - 1U]);
        } else {
            clks_wm_set_focus_slot(-1);
        }
    }

    if (clks_wm_z_count == 0U) {
        clks_wm_layer_free();
        clks_wm_scene_damage.count = 0U;
        clks_wm_present_damage.count = 0U;
    }
}

static void clks_wm_reap_dead_owners(void) {
#if CLKS_CFG_WM_REAP_DEAD_OWNERS == 0
    return;
#else
    u32 i;

    for (i = 0U; i < CLKS_WM_MAX_WINDOWS; i++) {
        struct clks_exec_proc_snapshot snap;

        if (clks_wm_windows[i].used == CLKS_FALSE || clks_wm_windows[i].owner_pid == 0ULL) {
            continue;
        }

        if (clks_exec_proc_snapshot(clks_wm_windows[i].owner_pid, &snap) == CLKS_FALSE) {
            clks_wm_remove_slot(i);
            continue;
        }

        if (snap.state == CLKS_EXEC_PROC_STATE_UNUSED || snap.state == CLKS_EXEC_PROC_STATE_EXITED) {
            clks_wm_remove_slot(i);
        }
    }
#endif
}
