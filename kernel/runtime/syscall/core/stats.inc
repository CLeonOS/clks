static void clks_syscall_stats_reset(void) {
    clks_syscall_stats_total = 0ULL;
    clks_memset(clks_syscall_stats_id_count, 0, sizeof(clks_syscall_stats_id_count));
    clks_memset(clks_syscall_stats_recent_id_count, 0, sizeof(clks_syscall_stats_recent_id_count));
    clks_memset(clks_syscall_stats_recent_ring, 0, sizeof(clks_syscall_stats_recent_ring));
    clks_syscall_stats_recent_head = 0U;
    clks_syscall_stats_recent_size = 0U;
}

static void clks_syscall_stats_record(u64 id) {
    u16 ring_id = 0xFFFFU;

    clks_syscall_stats_total++;

    if (id <= CLKS_SYSCALL_STATS_MAX_ID) {
        clks_syscall_stats_id_count[id]++;
    }

    if (id <= 0xFFFFULL) {
        ring_id = (u16)id;
    }

    if (clks_syscall_stats_recent_size >= CLKS_SYSCALL_STATS_RING_SIZE) {
        u64 old_id = (u64)clks_syscall_stats_recent_ring[clks_syscall_stats_recent_head];

        if (old_id <= CLKS_SYSCALL_STATS_MAX_ID && clks_syscall_stats_recent_id_count[old_id] > 0ULL) {
            clks_syscall_stats_recent_id_count[old_id]--;
        }
    } else {
        clks_syscall_stats_recent_size++;
    }

    clks_syscall_stats_recent_ring[clks_syscall_stats_recent_head] = ring_id;

    if (id <= CLKS_SYSCALL_STATS_MAX_ID) {
        clks_syscall_stats_recent_id_count[id]++;
    }

    clks_syscall_stats_recent_head++;

    if (clks_syscall_stats_recent_head >= CLKS_SYSCALL_STATS_RING_SIZE) {
        clks_syscall_stats_recent_head = 0U;
    }
}

static u64 clks_syscall_stats_total_count(void) {
    return clks_syscall_stats_total;
}

static u64 clks_syscall_stats_id(u64 id) {
    if (id > CLKS_SYSCALL_STATS_MAX_ID) {
        return 0ULL;
    }

    return clks_syscall_stats_id_count[id];
}

static u64 clks_syscall_stats_recent_window(void) {
    return (u64)clks_syscall_stats_recent_size;
}

static u64 clks_syscall_stats_recent_id(u64 id) {
    if (id > CLKS_SYSCALL_STATS_MAX_ID) {
        return 0ULL;
    }

    return clks_syscall_stats_recent_id_count[id];
}

