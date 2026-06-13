enum clks_syscall_category {
    CLKS_SYSCALL_CATEGORY_CORE = 0,
    CLKS_SYSCALL_CATEGORY_FS,
    CLKS_SYSCALL_CATEGORY_EXEC,
    CLKS_SYSCALL_CATEGORY_TTY,
    CLKS_SYSCALL_CATEGORY_KEYBOARD,
    CLKS_SYSCALL_CATEGORY_PROCESS,
    CLKS_SYSCALL_CATEGORY_KDBG,
    CLKS_SYSCALL_CATEGORY_STATS,
    CLKS_SYSCALL_CATEGORY_FD,
    CLKS_SYSCALL_CATEGORY_DL,
    CLKS_SYSCALL_CATEGORY_FB,
    CLKS_SYSCALL_CATEGORY_DISK,
    CLKS_SYSCALL_CATEGORY_NET,
    CLKS_SYSCALL_CATEGORY_VM,
    CLKS_SYSCALL_CATEGORY_USER,
    CLKS_SYSCALL_CATEGORY_DISPLAY,
    CLKS_SYSCALL_CATEGORY_INPUTM,
    CLKS_SYSCALL_CATEGORY_WM,
    CLKS_SYSCALL_CATEGORY_DRIVER,
    CLKS_SYSCALL_CATEGORY_AUDIO,
    CLKS_SYSCALL_CATEGORY_RESERVED,
};

enum clks_syscall_usc_gate {
    CLKS_SYSCALL_USC_NONE = 0,
    CLKS_SYSCALL_USC_ALWAYS,
    CLKS_SYSCALL_USC_CFG_FS_MKDIR,
    CLKS_SYSCALL_USC_CFG_FS_WRITE,
    CLKS_SYSCALL_USC_CFG_FS_APPEND,
    CLKS_SYSCALL_USC_CFG_FS_REMOVE,
    CLKS_SYSCALL_USC_CFG_EXEC_PATH,
    CLKS_SYSCALL_USC_CFG_EXEC_PATHV,
    CLKS_SYSCALL_USC_CFG_EXEC_PATHV_IO,
    CLKS_SYSCALL_USC_CFG_SPAWN_PATH,
    CLKS_SYSCALL_USC_CFG_SPAWN_PATHV,
    CLKS_SYSCALL_USC_CFG_PROC_KILL,
    CLKS_SYSCALL_USC_CFG_SHUTDOWN,
    CLKS_SYSCALL_USC_CFG_RESTART,
};

#define CLKS_SYSCALL_META_MAX_ID CLKS_SYSCALL_NET_TCP_ACCEPT
#define CLKS_SYSCALL_STATS_MAX_ID CLKS_SYSCALL_META_MAX_ID

#define CLKS_SYSCALL_FLAG_NONE 0x00000000U
#define CLKS_SYSCALL_FLAG_RESERVED 0x00000001U
#define CLKS_SYSCALL_FLAG_TRACE_SUPPRESS 0x00000002U
#define CLKS_SYSCALL_FLAG_TRACE_RETURN_ONLY 0x00000004U
#define CLKS_SYSCALL_FLAG_TRACE_SKIP_ZERO_RET 0x00000008U
#define CLKS_SYSCALL_FLAG_USC_DANGEROUS 0x00000010U
#define CLKS_SYSCALL_FLAG_FS_MUTATION 0x00000020U
#define CLKS_SYSCALL_FLAG_PROCESS_CONTROL 0x00000040U
#define CLKS_SYSCALL_FLAG_RAW_STORAGE 0x00000080U
#define CLKS_SYSCALL_FLAG_CONFIG_MUTATION 0x00000100U

struct clks_syscall_meta {
    u64 id;
    const char *name;
    enum clks_syscall_category category;
    u8 argc;
    u32 flags;
    enum clks_syscall_usc_gate usc_gate;
};

static const struct clks_syscall_meta *clks_syscall_meta_by_id(u64 id) {
    static struct clks_syscall_meta cache[CLKS_SYSCALL_META_MAX_ID + 1ULL];
    static clks_bool cache_valid[CLKS_SYSCALL_META_MAX_ID + 1ULL];
    clks_rust_syscall_meta rust_meta;

    if (id > CLKS_SYSCALL_META_MAX_ID || id > clks_rust_syscall_meta_max_id()) {
        return CLKS_NULL;
    }

    if (cache_valid[id] == CLKS_TRUE) {
        return &cache[id];
    }

    if (clks_rust_syscall_meta_fill(id, &rust_meta) == CLKS_FALSE || rust_meta.id != id ||
        rust_meta.name == CLKS_NULL) {
        return CLKS_NULL;
    }

    cache[id].id = rust_meta.id;
    cache[id].name = rust_meta.name;
    cache[id].category = (enum clks_syscall_category)rust_meta.category;
    cache[id].argc = rust_meta.argc;
    cache[id].flags = rust_meta.flags;
    cache[id].usc_gate = (enum clks_syscall_usc_gate)rust_meta.usc_gate;
    cache_valid[id] = CLKS_TRUE;
    return &cache[id];
}
