#ifndef CLKS_DRIVER_H
#define CLKS_DRIVER_H

#include <clks/types.h>

#define CLKS_DRIVER_NAME_MAX 32U
#define CLKS_DRIVER_PATH_MAX 192U

enum clks_driver_kind {
    CLKS_DRIVER_KIND_RESERVED = 1,
    CLKS_DRIVER_KIND_ELF = 2,
};

enum clks_driver_state {
    CLKS_DRIVER_STATE_OFFLINE = 0,
    CLKS_DRIVER_STATE_READY = 1,
    CLKS_DRIVER_STATE_FAILED = 2,
    CLKS_DRIVER_STATE_LOADED = 3,
    CLKS_DRIVER_STATE_UNLOADED = 4,
};

enum clks_driver_class {
    CLKS_DRIVER_CLASS_OTHER = 0,
    CLKS_DRIVER_CLASS_CHAR = 1,
    CLKS_DRIVER_CLASS_VIDEO = 2,
    CLKS_DRIVER_CLASS_TTY = 3,
    CLKS_DRIVER_CLASS_AUDIO = 4,
    CLKS_DRIVER_CLASS_DISK = 5,
    CLKS_DRIVER_CLASS_NET = 6,
    CLKS_DRIVER_CLASS_INPUT = 7,
};

struct clks_driver_info {
    char name[CLKS_DRIVER_NAME_MAX];
    char path[CLKS_DRIVER_PATH_MAX];
    enum clks_driver_kind kind;
    enum clks_driver_state state;
    enum clks_driver_class driver_class;
    clks_bool from_elf;
    u64 image_size;
    u64 elf_entry;
    u64 load_id;
    u64 owner_pid;
};

void clks_driver_init(void);
u64 clks_driver_count(void);
u64 clks_driver_elf_count(void);
clks_bool clks_driver_get(u64 index, struct clks_driver_info *out_info);
u64 clks_driver_load_path(const char *path);
u64 clks_driver_reload_elf_dir(void);
clks_bool clks_driver_unload(const char *name_or_path);

#endif
