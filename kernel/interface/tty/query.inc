u32 clks_tty_active(void) {
    return clks_tty_active_index;
}

u32 clks_tty_count(void) {
    return CLKS_TTY_COUNT;
}

clks_bool clks_tty_ready(void) {
    return clks_tty_is_ready;
}
