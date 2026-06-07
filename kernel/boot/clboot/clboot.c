#include <clks/boot.h>
#include <clks/clboot.h>
#include <clks/limine.h>
#include <clks/string.h>

#define CLKS_CLBOOT_MAX_MEMMAP 128U
#define CLKS_CLBOOT_MAX_MODULES 8U

static const struct clboot_info *clks_clboot_info = CLKS_NULL;
static struct limine_framebuffer clks_clboot_fb;
static struct limine_memmap_entry clks_clboot_memmap_entries[CLKS_CLBOOT_MAX_MEMMAP];
static struct limine_memmap_entry *clks_clboot_memmap_ptrs[CLKS_CLBOOT_MAX_MEMMAP];
static struct limine_memmap_response clks_clboot_memmap_response;
static struct limine_file clks_clboot_modules[CLKS_CLBOOT_MAX_MODULES];
static struct limine_file *clks_clboot_module_ptrs[CLKS_CLBOOT_MAX_MODULES];
static u64 clks_clboot_module_count = 0ULL;

static u64 clks_clboot_memmap_type_to_limine(u64 type) {
    switch (type) {
    case CLBOOT_MEMMAP_USABLE:
        return LIMINE_MEMMAP_USABLE;
    case CLBOOT_MEMMAP_ACPI_RECLAIMABLE:
        return LIMINE_MEMMAP_ACPI_RECLAIMABLE;
    case CLBOOT_MEMMAP_ACPI_NVS:
        return LIMINE_MEMMAP_ACPI_NVS;
    case CLBOOT_MEMMAP_BAD_MEMORY:
        return LIMINE_MEMMAP_BAD_MEMORY;
    case CLBOOT_MEMMAP_BOOTLOADER_RECLAIMABLE:
        return LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE;
    case CLBOOT_MEMMAP_KERNEL_AND_MODULES:
        return LIMINE_MEMMAP_EXECUTABLE_AND_MODULES;
    case CLBOOT_MEMMAP_FRAMEBUFFER:
        return LIMINE_MEMMAP_FRAMEBUFFER;
    default:
        return LIMINE_MEMMAP_RESERVED;
    }
}

void clks_clboot_set_info(u64 magic, const struct clboot_info *info) {
    u64 i;

    if (magic != CLBOOT_MAGIC || info == CLKS_NULL || info->magic != CLBOOT_MAGIC || info->version != CLBOOT_VERSION) {
        clks_clboot_info = CLKS_NULL;
        return;
    }

    clks_clboot_info = info;

    clks_memset(&clks_clboot_fb, 0, sizeof(clks_clboot_fb));
    if (info->framebuffer != 0ULL) {
        const struct clboot_framebuffer *fb = (const struct clboot_framebuffer *)(usize)info->framebuffer;
        clks_clboot_fb.address = (void *)(usize)fb->address;
        clks_clboot_fb.width = fb->width;
        clks_clboot_fb.height = fb->height;
        clks_clboot_fb.pitch = fb->pitch;
        clks_clboot_fb.bpp = fb->bpp;
        clks_clboot_fb.memory_model = fb->memory_model;
        clks_clboot_fb.red_mask_size = fb->red_mask_size;
        clks_clboot_fb.red_mask_shift = fb->red_mask_shift;
        clks_clboot_fb.green_mask_size = fb->green_mask_size;
        clks_clboot_fb.green_mask_shift = fb->green_mask_shift;
        clks_clboot_fb.blue_mask_size = fb->blue_mask_size;
        clks_clboot_fb.blue_mask_shift = fb->blue_mask_shift;
    }

    clks_memset(&clks_clboot_memmap_response, 0, sizeof(clks_clboot_memmap_response));
    if (info->memmap_entries != 0ULL) {
        const struct clboot_memmap_entry *entries = (const struct clboot_memmap_entry *)(usize)info->memmap_entries;
        u64 count = info->memmap_count;
        if (count > CLKS_CLBOOT_MAX_MEMMAP) {
            count = CLKS_CLBOOT_MAX_MEMMAP;
        }
        for (i = 0ULL; i < count; i++) {
            clks_clboot_memmap_entries[i].base = entries[i].base;
            clks_clboot_memmap_entries[i].length = entries[i].length;
            clks_clboot_memmap_entries[i].type = clks_clboot_memmap_type_to_limine(entries[i].type);
            clks_clboot_memmap_ptrs[i] = &clks_clboot_memmap_entries[i];
        }
        clks_clboot_memmap_response.entry_count = count;
        clks_clboot_memmap_response.entries = clks_clboot_memmap_ptrs;
    }

    clks_memset(clks_clboot_modules, 0, sizeof(clks_clboot_modules));
    clks_clboot_module_count = 0ULL;
    if (info->modules != 0ULL) {
        const struct clboot_module *modules = (const struct clboot_module *)(usize)info->modules;
        u64 count = info->module_count;
        if (count > CLKS_CLBOOT_MAX_MODULES) {
            count = CLKS_CLBOOT_MAX_MODULES;
        }
        for (i = 0ULL; i < count; i++) {
            clks_clboot_modules[i].address = (void *)(usize)modules[i].address;
            clks_clboot_modules[i].size = modules[i].size;
            clks_clboot_modules[i].path = (char *)(usize)modules[i].path;
            clks_clboot_modules[i].string = (char *)(usize)modules[i].cmdline;
            clks_clboot_module_ptrs[i] = &clks_clboot_modules[i];
        }
        clks_clboot_module_count = count;
    }
}

clks_bool clks_clboot_active(void) {
    return (clks_clboot_info != CLKS_NULL) ? CLKS_TRUE : CLKS_FALSE;
}

const char *clks_clboot_get_bootlog(u64 *out_size, u64 *out_entry_count) {
    if (out_size != CLKS_NULL) {
        *out_size = 0ULL;
    }
    if (out_entry_count != CLKS_NULL) {
        *out_entry_count = 0ULL;
    }

    if (clks_clboot_active() == CLKS_FALSE || clks_clboot_info->bootlog == 0ULL ||
        clks_clboot_info->bootlog_size == 0ULL) {
        return CLKS_NULL;
    }

    if (out_size != CLKS_NULL) {
        *out_size = clks_clboot_info->bootlog_size;
    }
    if (out_entry_count != CLKS_NULL) {
        *out_entry_count = clks_clboot_info->bootlog_entry_count;
    }

    return (const char *)(usize)clks_clboot_info->bootlog;
}

clks_bool clks_boot_base_revision_supported(void) {
    return CLKS_TRUE;
}

const struct limine_framebuffer *clks_boot_get_framebuffer(void) {
    if (clks_clboot_active() == CLKS_FALSE || clks_clboot_info->framebuffer == 0ULL) {
        return CLKS_NULL;
    }

    return &clks_clboot_fb;
}

const struct limine_memmap_response *clks_boot_get_memmap(void) {
    if (clks_clboot_active() == CLKS_FALSE || clks_clboot_memmap_response.entry_count == 0ULL) {
        return CLKS_NULL;
    }

    return &clks_clboot_memmap_response;
}

const struct limine_file *clks_boot_get_executable_file(void) {
    return CLKS_NULL;
}

const char *clks_boot_get_cmdline(void) {
    if (clks_clboot_active() == CLKS_FALSE || clks_clboot_info->cmdline == 0ULL) {
        return "";
    }

    return (const char *)(usize)clks_clboot_info->cmdline;
}

static clks_bool clks_boot_token_matches(const char *token, const char *name, usize name_len) {
    usize i;

    if (token == CLKS_NULL || name == CLKS_NULL || name_len == 0U) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < name_len; i++) {
        if (token[i] != name[i]) {
            return CLKS_FALSE;
        }
    }

    if (token[name_len] == '\0' || token[name_len] == ' ' || token[name_len] == '\t') {
        return CLKS_TRUE;
    }

    if (token[name_len] == '=') {
        char value = token[name_len + 1U];
        return (value == '1' || value == 'y' || value == 'Y' || value == 't' || value == 'T') ? CLKS_TRUE
                                                                                              : CLKS_FALSE;
    }

    return CLKS_FALSE;
}

clks_bool clks_boot_cmdline_flag_enabled(const char *name) {
    const char *cmdline = clks_boot_get_cmdline();
    usize name_len = 0U;
    usize i = 0U;

    if (name == CLKS_NULL || name[0] == '\0') {
        return CLKS_FALSE;
    }

    while (name[name_len] != '\0') {
        name_len++;
    }

    while (cmdline[i] != '\0') {
        while (cmdline[i] == ' ' || cmdline[i] == '\t') {
            i++;
        }
        if (cmdline[i] == '\0') {
            break;
        }

        if (clks_boot_token_matches(cmdline + i, name, name_len) == CLKS_TRUE) {
            return CLKS_TRUE;
        }

        while (cmdline[i] != '\0' && cmdline[i] != ' ' && cmdline[i] != '\t') {
            i++;
        }
    }

    return CLKS_FALSE;
}

clks_bool clks_boot_cmdline_get_value(const char *name, char *out_value, usize out_size) {
    const char *cmdline = clks_boot_get_cmdline();
    usize name_len = 0U;
    usize i = 0U;
    usize out_pos;

    if (name == CLKS_NULL || name[0] == '\0' || out_value == CLKS_NULL || out_size == 0U) {
        return CLKS_FALSE;
    }

    out_value[0] = '\0';

    while (name[name_len] != '\0') {
        name_len++;
    }

    while (cmdline[i] != '\0') {
        usize j;

        while (cmdline[i] == ' ' || cmdline[i] == '\t') {
            i++;
        }
        if (cmdline[i] == '\0') {
            break;
        }

        for (j = 0U; j < name_len; j++) {
            if (cmdline[i + j] != name[j]) {
                break;
            }
        }

        if (j == name_len && cmdline[i + name_len] == '=') {
            i += name_len + 1U;
            out_pos = 0U;
            while (cmdline[i] != '\0' && cmdline[i] != ' ' && cmdline[i] != '\t') {
                if (out_pos + 1U < out_size) {
                    out_value[out_pos++] = cmdline[i];
                }
                i++;
            }
            out_value[out_pos] = '\0';
            return (out_pos > 0U) ? CLKS_TRUE : CLKS_FALSE;
        }

        while (cmdline[i] != '\0' && cmdline[i] != ' ' && cmdline[i] != '\t') {
            i++;
        }
    }

    return CLKS_FALSE;
}

clks_bool clks_boot_rescue_mode(void) {
    return (clks_boot_cmdline_flag_enabled("clks.rescue") == CLKS_TRUE ||
            clks_boot_cmdline_flag_enabled("rescue") == CLKS_TRUE)
               ? CLKS_TRUE
               : CLKS_FALSE;
}

u64 clks_boot_get_module_count(void) {
    return (clks_clboot_active() == CLKS_TRUE) ? clks_clboot_module_count : 0ULL;
}

const struct limine_file *clks_boot_get_module(u64 index) {
    if (clks_clboot_active() == CLKS_FALSE || index >= clks_clboot_module_count) {
        return CLKS_NULL;
    }

    return clks_clboot_module_ptrs[index];
}

u64 clks_boot_get_hhdm_offset(void) {
    if (clks_clboot_active() == CLKS_FALSE) {
        return 0ULL;
    }

    return clks_clboot_info->hhdm_offset;
}

void *clks_boot_phys_to_virt(u64 phys_addr) {
    u64 offset = clks_boot_get_hhdm_offset();

    if (offset == 0ULL) {
        return CLKS_NULL;
    }

    return (void *)(usize)(phys_addr + offset);
}
