#include <clks/driver.h>
#include <clks/elf64.h>
#include <clks/exec.h>
#include <clks/fs.h>
#include <clks/log.h>
#include <clks/string.h>
#include <clks/types.h>

#define CLKS_DRIVER_MAX 32U
#define CLKS_DRIVER_CHILD_NAME_MAX 96U
#define CLKS_DRIVER_SCAN_PATH_MAX 224U

static struct clks_driver_info clks_driver_table[CLKS_DRIVER_MAX];
static u64 clks_driver_table_count = 0ULL;
static u64 clks_driver_table_elf_count = 0ULL;
static u64 clks_driver_next_load_id = 1ULL;

static void clks_driver_copy_name(char *dst, usize dst_size, const char *src) {
    usize i = 0U;

    if (dst == CLKS_NULL || dst_size == 0U) {
        return;
    }

    if (src == CLKS_NULL) {
        dst[0] = '\0';
        return;
    }

    while (i + 1U < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static clks_bool clks_driver_has_elf_suffix(const char *name) {
    usize len;

    if (name == CLKS_NULL) {
        return CLKS_FALSE;
    }

    len = clks_strlen(name);

    if (len < 4U) {
        return CLKS_FALSE;
    }

    return (name[len - 4U] == '.' && name[len - 3U] == 'e' && name[len - 2U] == 'l' && name[len - 1U] == 'f')
               ? CLKS_TRUE
               : CLKS_FALSE;
}

static const char *clks_driver_basename(const char *path) {
    const char *base = path;
    usize i = 0U;

    if (path == CLKS_NULL) {
        return "";
    }

    while (path[i] != '\0') {
        if (path[i] == '/' && path[i + 1U] != '\0') {
            base = &path[i + 1U];
        }

        i++;
    }

    return base;
}

static u64 clks_driver_alloc_load_id(void) {
    u64 id = clks_driver_next_load_id;

    clks_driver_next_load_id++;
    if (clks_driver_next_load_id == 0ULL) {
        clks_driver_next_load_id = 1ULL;
    }

    return id;
}

static clks_bool clks_driver_build_path(const char *child_name, char *out_path, usize out_size) {
    static const char prefix[] = "/driver/";
    usize prefix_len = sizeof(prefix) - 1U;
    usize child_len;

    if (child_name == CLKS_NULL || out_path == CLKS_NULL || out_size == 0U) {
        return CLKS_FALSE;
    }

    child_len = clks_strlen(child_name);

    if (prefix_len + child_len + 1U > out_size) {
        return CLKS_FALSE;
    }

    clks_memcpy(out_path, prefix, prefix_len);
    clks_memcpy(out_path + prefix_len, child_name, child_len);
    out_path[prefix_len + child_len] = '\0';
    return CLKS_TRUE;
}

static i32 clks_driver_find(const char *name_or_path) {
    u64 i;

    if (name_or_path == CLKS_NULL || name_or_path[0] == '\0') {
        return -1;
    }

    for (i = 0ULL; i < clks_driver_table_count; i++) {
        if (clks_driver_table[i].state == CLKS_DRIVER_STATE_UNLOADED) {
            continue;
        }

        if (clks_strcmp(clks_driver_table[i].name, name_or_path) == 0 ||
            clks_strcmp(clks_driver_table[i].path, name_or_path) == 0) {
            return (i32)i;
        }
    }

    return -1;
}

static clks_bool clks_driver_push(const char *name, const char *path, enum clks_driver_kind kind,
                                  enum clks_driver_class driver_class, enum clks_driver_state state,
                                  clks_bool from_elf, u64 image_size, u64 elf_entry, u64 owner_pid) {
    struct clks_driver_info *slot;

    if (clks_driver_table_count >= CLKS_DRIVER_MAX) {
        return CLKS_FALSE;
    }

    slot = &clks_driver_table[clks_driver_table_count];
    clks_memset(slot, 0, sizeof(*slot));

    clks_driver_copy_name(slot->name, sizeof(slot->name), name);
    clks_driver_copy_name(slot->path, sizeof(slot->path), path);
    slot->kind = kind;
    slot->state = state;
    slot->driver_class = driver_class;
    slot->from_elf = from_elf;
    slot->image_size = image_size;
    slot->elf_entry = elf_entry;
    slot->load_id = clks_driver_alloc_load_id();
    slot->owner_pid = owner_pid;

    clks_driver_table_count++;

    if (from_elf == CLKS_TRUE) {
        clks_driver_table_elf_count++;
    }

    return CLKS_TRUE;
}

static void clks_driver_metadata_for_elf(const char *path, const char **out_name,
                                         enum clks_driver_class *out_class) {
    const char *base = clks_driver_basename(path);
    const char *name = base;
    enum clks_driver_class driver_class = CLKS_DRIVER_CLASS_OTHER;

    if (clks_strcmp(base, "serialdrv.elf") == 0) {
        name = "serial";
        driver_class = CLKS_DRIVER_CLASS_CHAR;
    } else if (clks_strcmp(base, "fbdrv.elf") == 0 || clks_strcmp(base, "framebufferdrv.elf") == 0) {
        name = "framebuffer";
        driver_class = CLKS_DRIVER_CLASS_VIDEO;
    } else if (clks_strcmp(base, "ttydrv.elf") == 0) {
        name = "tty";
        driver_class = CLKS_DRIVER_CLASS_TTY;
    } else if (clks_strcmp(base, "pcspeakerdrv.elf") == 0 || clks_strcmp(base, "audiodrv.elf") == 0) {
        name = "pcspeaker";
        driver_class = CLKS_DRIVER_CLASS_AUDIO;
    } else if (clks_strcmp(base, "diskdrv.elf") == 0) {
        name = "disk";
        driver_class = CLKS_DRIVER_CLASS_DISK;
    } else if (clks_strcmp(base, "netdrv.elf") == 0) {
        name = "net0";
        driver_class = CLKS_DRIVER_CLASS_NET;
    } else if (clks_strcmp(base, "kbddrv.elf") == 0 || clks_strcmp(base, "keyboarddrv.elf") == 0) {
        name = "keyboard";
        driver_class = CLKS_DRIVER_CLASS_INPUT;
    } else if (clks_strcmp(base, "mousedrv.elf") == 0) {
        name = "mouse";
        driver_class = CLKS_DRIVER_CLASS_INPUT;
    }

    if (out_name != CLKS_NULL) {
        *out_name = name;
    }

    if (out_class != CLKS_NULL) {
        *out_class = driver_class;
    }
}

u64 clks_driver_load_path(const char *path) {
    const void *image;
    u64 image_size = 0ULL;
    struct clks_elf64_info info;
    const char *name;
    enum clks_driver_class driver_class = CLKS_DRIVER_CLASS_OTHER;
    u64 owner_pid = 0ULL;

    if (clks_fs_is_ready() == CLKS_FALSE) {
        clks_log(CLKS_LOG_ERROR, "DRV", "FS NOT READY FOR DRIVER LOAD");
        return 0ULL;
    }

    if (path == CLKS_NULL || path[0] != '/' || clks_driver_has_elf_suffix(path) == CLKS_FALSE) {
        return 0ULL;
    }

    clks_driver_metadata_for_elf(path, &name, &driver_class);

    if (clks_driver_find(path) >= 0 || clks_driver_find(name) >= 0 || clks_driver_find(clks_driver_basename(path)) >= 0) {
        return 0ULL;
    }

    image = clks_fs_read_all(path, &image_size);

    if (image == CLKS_NULL) {
        clks_log(CLKS_LOG_ERROR, "DRV", "DRIVER ELF MISSING");
        clks_log(CLKS_LOG_ERROR, "DRV", path);
        return 0ULL;
    }

    if (clks_elf64_inspect(image, image_size, &info) == CLKS_FALSE) {
        clks_log(CLKS_LOG_ERROR, "DRV", "DRIVER ELF INVALID");
        clks_log(CLKS_LOG_ERROR, "DRV", path);
        return 0ULL;
    }

    if (clks_exec_spawn_pathv(path, "", "CLKS_DRIVER=1", &owner_pid) == CLKS_FALSE) {
        clks_log(CLKS_LOG_ERROR, "DRV", "DRIVER ELF SPAWN FAILED");
        clks_log(CLKS_LOG_ERROR, "DRV", path);
        return 0ULL;
    }

    if (clks_driver_push(name, path, CLKS_DRIVER_KIND_ELF, driver_class, CLKS_DRIVER_STATE_LOADED,
                         CLKS_TRUE, image_size, info.entry, owner_pid) == CLKS_FALSE) {
        (void)clks_exec_proc_kill(owner_pid, CLKS_EXEC_SIGNAL_TERM);
        return 0ULL;
    }

    return clks_driver_table[clks_driver_table_count - 1ULL].load_id;
}

u64 clks_driver_reload_elf_dir(void) {
    u64 child_count;
    u64 i;
    u64 loaded = 0ULL;

    if (clks_fs_is_ready() == CLKS_FALSE) {
        clks_log(CLKS_LOG_ERROR, "DRV", "FS NOT READY FOR DRIVER PROBE");
        return 0ULL;
    }

    child_count = clks_fs_count_children("/driver");

    for (i = 0ULL; i < child_count; i++) {
        char child_name[CLKS_DRIVER_CHILD_NAME_MAX];
        char full_path[CLKS_DRIVER_SCAN_PATH_MAX];

        clks_memset(child_name, 0, sizeof(child_name));
        clks_memset(full_path, 0, sizeof(full_path));

        if (clks_fs_get_child_name("/driver", i, child_name, sizeof(child_name)) == CLKS_FALSE) {
            continue;
        }

        if (clks_driver_has_elf_suffix(child_name) == CLKS_FALSE) {
            continue;
        }

        if (clks_driver_build_path(child_name, full_path, sizeof(full_path)) == CLKS_FALSE) {
            clks_driver_push(child_name, "", CLKS_DRIVER_KIND_ELF, CLKS_DRIVER_CLASS_OTHER,
                             CLKS_DRIVER_STATE_FAILED, CLKS_TRUE, 0ULL, 0ULL, 0ULL);
            continue;
        }

        if (clks_driver_load_path(full_path) != 0ULL) {
            loaded++;
        }
    }

    return loaded;
}

static void clks_driver_probe_driver_dir(void) {
    (void)clks_driver_reload_elf_dir();
}

void clks_driver_init(void) {
    clks_memset(clks_driver_table, 0, sizeof(clks_driver_table));
    clks_driver_table_count = 0ULL;
    clks_driver_table_elf_count = 0ULL;
    clks_driver_next_load_id = 1ULL;

    clks_driver_probe_driver_dir();

}

u64 clks_driver_count(void) {
    return clks_driver_table_count;
}

u64 clks_driver_elf_count(void) {
    return clks_driver_table_elf_count;
}

clks_bool clks_driver_get(u64 index, struct clks_driver_info *out_info) {
    if (out_info == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (index >= clks_driver_table_count) {
        return CLKS_FALSE;
    }

    *out_info = clks_driver_table[index];
    return CLKS_TRUE;
}

clks_bool clks_driver_unload(const char *name_or_path) {
    i32 index = clks_driver_find(name_or_path);
    struct clks_driver_info *slot;

    if (index < 0) {
        return CLKS_FALSE;
    }

    slot = &clks_driver_table[(u32)index];

    if (slot->from_elf == CLKS_FALSE || slot->kind != CLKS_DRIVER_KIND_ELF) {
        return CLKS_FALSE;
    }

    if (slot->owner_pid != 0ULL) {
        (void)clks_exec_proc_kill(slot->owner_pid, CLKS_EXEC_SIGNAL_TERM);
    }

    slot->state = CLKS_DRIVER_STATE_UNLOADED;
    if (clks_driver_table_elf_count > 0ULL) {
        clks_driver_table_elf_count--;
    }

    return CLKS_TRUE;
}
