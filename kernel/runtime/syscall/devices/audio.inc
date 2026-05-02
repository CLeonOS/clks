static u64 clks_syscall_audio_available(void) {
    return (clks_audio_available() == CLKS_TRUE) ? 1ULL : 0ULL;
}

static u64 clks_syscall_audio_play_tone(u64 arg0, u64 arg1) {
    if (clks_audio_play_tone(arg0, arg1) == CLKS_FALSE) {
        return 0ULL;
    }

    return 1ULL;
}

static u64 clks_syscall_audio_stop(void) {
    clks_audio_stop();
    return 1ULL;
}
