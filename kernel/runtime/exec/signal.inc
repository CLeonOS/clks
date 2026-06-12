static u64 clks_exec_signal_from_vector(u64 vector) {
    switch (vector) {
    case 0ULL:
    case 16ULL:
    case 19ULL:
        return 8ULL;
    case 6ULL:
        return 4ULL;
    case 3ULL:
        return 5ULL;
    case 14ULL:
    case 13ULL:
    case 12ULL:
    case 11ULL:
    case 10ULL:
    case 17ULL:
        return 11ULL;
    default:
        return 6ULL;
    }
}

static u64 clks_exec_encode_signal_status(u64 signal, u64 vector, u64 error_code) {
    return CLKS_EXEC_STATUS_SIGNAL_FLAG | (signal & 0xFFULL) | ((vector & 0xFFULL) << 8) |
           ((error_code & 0xFFFFULL) << 16);
}

static clks_bool clks_exec_status_is_signal(u64 status) {
    return ((status & CLKS_EXEC_STATUS_SIGNAL_FLAG) != 0ULL) ? CLKS_TRUE : CLKS_FALSE;
}

static u64 clks_exec_status_signal(u64 status) {
    return status & 0xFFULL;
}

static u64 clks_exec_status_vector(u64 status) {
    return (status >> 8) & 0xFFULL;
}

static u64 clks_exec_status_error(u64 status) {
    return (status >> 16) & 0xFFFFULL;
}

static clks_bool clks_exec_status_is_user_terminated(u64 status) {
    u64 signal;

    if (clks_exec_status_is_signal(status) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_exec_status_vector(status) != 0ULL || clks_exec_status_error(status) != 0ULL) {
        return CLKS_FALSE;
    }

    signal = clks_exec_status_signal(status);
    if (signal == CLKS_EXEC_SIGNAL_TERM || signal == CLKS_EXEC_SIGNAL_KILL || signal == CLKS_EXEC_SIGNAL_STOP) {
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}

