static u64 clks_wm_read_tsc(void) {
#if defined(CLKS_ARCH_X86_64)
    u32 lo;
    u32 hi;

    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32U) | (u64)lo;
#else
    return 0ULL;
#endif
}

static u64 clks_wm_cycles_per_second(void) {
    if (clks_wm_timing.tsc_ready == CLKS_FALSE || clks_wm_timing.cycles_per_tick == 0ULL) {
        return 0ULL;
    }

    return clks_wm_timing.cycles_per_tick * (u64)clks_interrupts_timer_hz();
}

static void clks_wm_timing_reset(void) {
    clks_memset(&clks_wm_timing, 0, sizeof(clks_wm_timing));
}

static void clks_wm_timing_update_timebase(u64 now_tick, u64 now_tsc) {
    u64 tick_delta;
    u64 tsc_delta;

    if (now_tsc == 0ULL) {
        return;
    }

    if (clks_wm_timing.timebase_valid == CLKS_FALSE) {
        clks_wm_timing.timebase_tick = now_tick;
        clks_wm_timing.timebase_tsc = now_tsc;
        clks_wm_timing.timebase_valid = CLKS_TRUE;
        return;
    }

    if (now_tick == clks_wm_timing.timebase_tick || now_tick < clks_wm_timing.timebase_tick ||
        now_tsc <= clks_wm_timing.timebase_tsc) {
        return;
    }

    tick_delta = now_tick - clks_wm_timing.timebase_tick;
    tsc_delta = now_tsc - clks_wm_timing.timebase_tsc;
    if (tick_delta == 0ULL) {
        return;
    }

    if (clks_wm_timing.cycles_per_tick == 0ULL) {
        clks_wm_timing.cycles_per_tick = tsc_delta / tick_delta;
    } else {
        u64 measured = tsc_delta / tick_delta;
        clks_wm_timing.cycles_per_tick = ((clks_wm_timing.cycles_per_tick * 7ULL) + measured) / 8ULL;
    }

    if (clks_wm_timing.cycles_per_tick != 0ULL) {
        clks_wm_timing.tsc_ready = CLKS_TRUE;
    }

    clks_wm_timing.timebase_tick = now_tick;
    clks_wm_timing.timebase_tsc = now_tsc;
}

static clks_bool clks_wm_frame_paced(u64 now_tick, u64 now_tsc) {
#if CLKS_CFG_WM_FRAME_PACING == 0
    (void)now_tick;
    (void)now_tsc;
    return CLKS_TRUE;
#else
    u64 cycles_per_second;
    u64 min_cycles;
    u64 tick_interval;
    u64 timer_hz;

    if (clks_wm_timing.last_present_tick == 0ULL && clks_wm_timing.last_present_tsc == 0ULL) {
        (void)now_tick;
        (void)now_tsc;
        return CLKS_TRUE;
    }

    cycles_per_second = clks_wm_cycles_per_second();
    if (cycles_per_second != 0ULL && now_tsc > clks_wm_timing.last_present_tsc) {
        min_cycles = cycles_per_second / CLKS_WM_FRAME_CAP_FPS;
        if (min_cycles == 0ULL) {
            return CLKS_TRUE;
        }

        return ((now_tsc - clks_wm_timing.last_present_tsc) >= min_cycles) ? CLKS_TRUE : CLKS_FALSE;
    }

    timer_hz = (u64)clks_interrupts_timer_hz();

    if (timer_hz == 0ULL || CLKS_WM_FRAME_CAP_FPS >= timer_hz) {
        return CLKS_TRUE;
    }

    tick_interval = timer_hz / CLKS_WM_FRAME_CAP_FPS;
    if (tick_interval == 0ULL) {
        tick_interval = 1ULL;
    }

    return (now_tick >= clks_wm_timing.last_present_tick + tick_interval) ? CLKS_TRUE : CLKS_FALSE;
#endif
}

static void clks_wm_stats_on_present(u64 now_tick, u64 now_tsc) {
#if CLKS_CFG_WM_STATS_OVERLAY == 0
    clks_wm_timing.last_present_tick = now_tick;
    clks_wm_timing.last_present_tsc = now_tsc;
#else
    u64 cycles_per_second = clks_wm_cycles_per_second();

    if (cycles_per_second != 0ULL && clks_wm_timing.last_present_tsc != 0ULL &&
        now_tsc > clks_wm_timing.last_present_tsc) {
        u64 delta_cycles = now_tsc - clks_wm_timing.last_present_tsc;
        u64 ms = (delta_cycles * 1000ULL + (cycles_per_second / 2ULL)) / cycles_per_second;
        clks_wm_timing.frame_ms = (ms > 9999ULL) ? 9999U : (u32)ms;
    } else if (clks_wm_timing.last_present_tick != 0ULL && now_tick > clks_wm_timing.last_present_tick) {
        u64 delta_ticks = now_tick - clks_wm_timing.last_present_tick;
        u64 timer_hz = (u64)clks_interrupts_timer_hz();
        u64 ms;
        if (timer_hz == 0ULL) {
            timer_hz = 1ULL;
        }
        ms = (delta_ticks * 1000ULL + (timer_hz / 2ULL)) / timer_hz;
        clks_wm_timing.frame_ms = (ms > 9999ULL) ? 9999U : (u32)ms;
    }

    clks_wm_timing.stats_frames++;
    if (clks_wm_timing.stats_anchor_valid == CLKS_FALSE) {
        clks_wm_timing.stats_anchor_tick = now_tick;
        clks_wm_timing.stats_anchor_tsc = now_tsc;
        clks_wm_timing.stats_anchor_valid = CLKS_TRUE;
    } else if (cycles_per_second != 0ULL && now_tsc > clks_wm_timing.stats_anchor_tsc) {
        u64 elapsed_cycles = now_tsc - clks_wm_timing.stats_anchor_tsc;
        u64 sample_cycles = clks_wm_timing.cycles_per_tick * (u64)clks_interrupts_timer_hz();

        if (elapsed_cycles >= sample_cycles && sample_cycles != 0ULL) {
            u64 fps = ((u64)clks_wm_timing.stats_frames * cycles_per_second + (elapsed_cycles / 2ULL)) / elapsed_cycles;
            clks_wm_timing.fps = (fps > 9999ULL) ? 9999U : (u32)fps;
            clks_wm_timing.stats_frames = 0U;
            clks_wm_timing.stats_anchor_tick = now_tick;
            clks_wm_timing.stats_anchor_tsc = now_tsc;
        }
    } else if (now_tick >= clks_wm_timing.stats_anchor_tick + (u64)clks_interrupts_timer_hz()) {
        u64 elapsed_ticks = now_tick - clks_wm_timing.stats_anchor_tick;
        u64 timer_hz = (u64)clks_interrupts_timer_hz();

        if (elapsed_ticks != 0ULL && timer_hz != 0ULL) {
            u64 fps = ((u64)clks_wm_timing.stats_frames * timer_hz + (elapsed_ticks / 2ULL)) / elapsed_ticks;
            clks_wm_timing.fps = (fps > 9999ULL) ? 9999U : (u32)fps;
        }

        clks_wm_timing.stats_frames = 0U;
        clks_wm_timing.stats_anchor_tick = now_tick;
        clks_wm_timing.stats_anchor_tsc = now_tsc;
    }

    clks_wm_timing.last_present_tick = now_tick;
    clks_wm_timing.last_present_tsc = now_tsc;
#endif
}

