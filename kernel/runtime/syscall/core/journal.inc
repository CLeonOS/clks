}

static u64 clks_syscall_log_journal_count(void) {
    return clks_log_journal_count();
}

static u64 clks_syscall_log_journal_read(u64 arg0, u64 arg1, u64 arg2) {
    char line[CLKS_SYSCALL_JOURNAL_MAX_LEN];
    usize line_len;
    usize copy_len;

    if (arg1 == 0ULL || arg2 == 0ULL) {
        return 0ULL;
    }

    if (clks_syscall_user_ptr_writable(arg1, arg2) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_log_journal_read(arg0, line, sizeof(line)) == CLKS_FALSE) {
        return 0ULL;
    }

    line_len = clks_strlen(line) + 1U;
    copy_len = line_len;

    if (copy_len > (usize)arg2) {
        copy_len = (usize)arg2;
    }

    if (copy_len > sizeof(line)) {
        copy_len = sizeof(line);
    }

    clks_memcpy((void *)arg1, line, copy_len);
    ((char *)arg1)[copy_len - 1U] = '\0';
    return 1ULL;
}

