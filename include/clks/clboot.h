#ifndef CLKS_CLBOOT_H
#define CLKS_CLBOOT_H

#include <clks/types.h>

#define CLBOOT_MAGIC 0x434C424F4F543031ULL
#define CLBOOT_VERSION 2U
#define CLBOOT_HHDM_OFFSET 0xFFFF800000000000ULL

#define CLBOOT_MEMMAP_USABLE 0ULL
#define CLBOOT_MEMMAP_RESERVED 1ULL
#define CLBOOT_MEMMAP_ACPI_RECLAIMABLE 2ULL
#define CLBOOT_MEMMAP_ACPI_NVS 3ULL
#define CLBOOT_MEMMAP_BAD_MEMORY 4ULL
#define CLBOOT_MEMMAP_BOOTLOADER_RECLAIMABLE 5ULL
#define CLBOOT_MEMMAP_KERNEL_AND_MODULES 6ULL
#define CLBOOT_MEMMAP_FRAMEBUFFER 7ULL

struct clboot_memmap_entry {
    u64 base;
    u64 length;
    u64 type;
};

struct clboot_module {
    u64 address;
    u64 size;
    u64 path;
    u64 cmdline;
};

struct clboot_framebuffer {
    u64 address;
    u64 width;
    u64 height;
    u64 pitch;
    u16 bpp;
    u8 memory_model;
    u8 red_mask_size;
    u8 red_mask_shift;
    u8 green_mask_size;
    u8 green_mask_shift;
    u8 blue_mask_size;
    u8 blue_mask_shift;
    u8 reserved[7];
};

struct clboot_info {
    u64 magic;
    u32 version;
    u32 flags;
    u64 hhdm_offset;
    u64 kernel_entry;
    u64 kernel_virtual_base;
    u64 kernel_physical_base;
    u64 cmdline;
    u64 framebuffer;
    u64 memmap_entries;
    u64 memmap_count;
    u64 modules;
    u64 module_count;
    u64 rsdp;
    u64 bootlog;
    u64 bootlog_size;
    u64 bootlog_entry_count;
};

void clks_clboot_set_info(u64 magic, const struct clboot_info *info);
clks_bool clks_clboot_active(void);
const char *clks_clboot_get_bootlog(u64 *out_size, u64 *out_entry_count);

#endif
