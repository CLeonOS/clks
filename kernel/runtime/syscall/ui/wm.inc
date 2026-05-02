static u64 clks_syscall_mouse_state(u64 arg0) {
    struct clks_mouse_state state;
    struct clks_syscall_mouse_state_user *out_state = (struct clks_syscall_mouse_state_user *)(usize)arg0;

    if (arg0 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg0, (u64)sizeof(*out_state)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_mouse_snapshot(&state);
    out_state->x = (u64)state.x;
    out_state->y = (u64)state.y;
    out_state->buttons = (u64)state.buttons;
    out_state->packet_count = state.packet_count;
    out_state->ready = (state.ready == CLKS_TRUE) ? 1ULL : 0ULL;
    return 1ULL;
}

static clks_bool clks_syscall_u64_to_i32(u64 raw, i32 *out_value) {
    i64 value = (i64)raw;

    if (out_value == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (value < (-2147483647LL - 1LL) || value > 2147483647LL) {
        return CLKS_FALSE;
    }

    *out_value = (i32)value;
    return CLKS_TRUE;
}

static u64 clks_syscall_wm_owner_pid(void) {
    if (clks_syscall_in_user_exec_context() == CLKS_TRUE) {
        return clks_exec_current_pid();
    }

    return 0ULL;
}

static u64 clks_syscall_wm_create(u64 arg0) {
    struct clks_syscall_wm_create_req req;
    i32 x = 0;
    i32 y = 0;

    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));

    if (req.width > 0xFFFFFFFFULL || req.height > 0xFFFFFFFFULL) {
        return 0ULL;
    }

    if (clks_syscall_u64_to_i32(req.x, &x) == CLKS_FALSE || clks_syscall_u64_to_i32(req.y, &y) == CLKS_FALSE) {
        return 0ULL;
    }

    return clks_wm_create(clks_syscall_wm_owner_pid(), x, y, (u32)req.width, (u32)req.height, req.flags);
}

static u64 clks_syscall_wm_destroy(u64 arg0) {
    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_wm_destroy(clks_syscall_wm_owner_pid(), arg0) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_wm_present(u64 arg0) {
    struct clks_syscall_wm_present_req req;
    u64 src_bytes;

    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));

    if (req.window_id == 0ULL || req.pixels_ptr == 0ULL) {
        return 0ULL;
    }

    if (req.src_width > 0xFFFFFFFFULL || req.src_height > 0xFFFFFFFFULL || req.src_pitch_bytes > 0xFFFFFFFFULL) {
        return 0ULL;
    }

    if (req.src_height == 0ULL || req.src_pitch_bytes == 0ULL) {
        return 0ULL;
    }

    if (req.src_height > (((u64)-1) / req.src_pitch_bytes)) {
        return 0ULL;
    }

    src_bytes = req.src_pitch_bytes * req.src_height;
    if (clks_syscall_user_ptr_readable(req.pixels_ptr, src_bytes) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_wm_present(clks_syscall_wm_owner_pid(), req.window_id, (const void *)(usize)req.pixels_ptr,
                            (u32)req.src_width, (u32)req.src_height, (u32)req.src_pitch_bytes) == CLKS_TRUE)
               ? 1ULL
               : 0ULL;
}

static u64 clks_syscall_wm_poll_event(u64 arg0, u64 arg1) {
    struct clks_wm_event *out_event = (struct clks_wm_event *)(usize)arg1;

    if (arg0 == 0ULL || arg1 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, (u64)sizeof(*out_event)) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_wm_poll_event(clks_syscall_wm_owner_pid(), arg0, out_event) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_wm_move(u64 arg0) {
    struct clks_syscall_wm_move_req req;
    i32 x = 0;
    i32 y = 0;

    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));

    if (req.window_id == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_u64_to_i32(req.x, &x) == CLKS_FALSE || clks_syscall_u64_to_i32(req.y, &y) == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_wm_move(clks_syscall_wm_owner_pid(), req.window_id, x, y) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_wm_set_focus(u64 arg0) {
    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_wm_set_focus(clks_syscall_wm_owner_pid(), arg0) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_wm_set_flags(u64 arg0, u64 arg1) {
    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    return (clks_wm_set_flags(clks_syscall_wm_owner_pid(), arg0, arg1) == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_wm_resize(u64 arg0) {
    struct clks_syscall_wm_resize_req req;

    if (arg0 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_readable(arg0, (u64)sizeof(req)) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy(&req, (const void *)(usize)arg0, sizeof(req));

    if (req.window_id == 0ULL || req.width > 0xFFFFFFFFULL || req.height > 0xFFFFFFFFULL) {
        return 0ULL;
    }

    return (clks_wm_resize(clks_syscall_wm_owner_pid(), req.window_id, (u32)req.width, (u32)req.height) == CLKS_TRUE)
               ? 1ULL
               : 0ULL;
}

static u64 clks_syscall_wm_count(void) {
    if (clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    return clks_wm_window_count();
}

static u64 clks_syscall_wm_id_at(u64 arg0, u64 arg1) {
    u64 window_id = 0ULL;

    if (arg1 == 0ULL || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, (u64)sizeof(window_id)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_wm_window_id_at(arg0, &window_id) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy((void *)(usize)arg1, &window_id, sizeof(window_id));
    return 1ULL;
}

static u64 clks_syscall_wm_snapshot(u64 arg0, u64 arg1, u64 arg2) {
    struct clks_wm_snapshot snap;

    if (arg0 == 0ULL || arg1 == 0ULL || arg2 < (u64)sizeof(snap) || clks_wm_ready() == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, (u64)sizeof(snap)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_wm_snapshot(arg0, &snap) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_memcpy((void *)(usize)arg1, &snap, sizeof(snap));
    return 1ULL;
}

