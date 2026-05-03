#include <clks/disk.h>
#include <clks/heap.h>
#include <clks/log.h>
#include <clks/string.h>
#include <clks/types.h>

/* FAT32 needs at least 65525 clusters; keep an MBR gap so Limine BIOS stage2 does not overlap the volume. */
#define CLKS_DISK_FAT32_PARTITION_LBA 2048ULL
#define CLKS_DISK_MIN_BYTES ((66581ULL + CLKS_DISK_FAT32_PARTITION_LBA) * 512ULL)
#define CLKS_DISK_CACHE_MAX_BYTES (256ULL * 1024ULL * 1024ULL)
#define CLKS_DISK_ALLOC_RETRY_STEP_BYTES (8ULL * 1024ULL * 1024ULL)
#define CLKS_DISK_HEAP_RESERVE_BYTES (16ULL * 1024ULL * 1024ULL)

#define CLKS_DISK_FAT32_MIN_CLUSTER_COUNT 65525U
#define CLKS_DISK_FAT32_EOC_MIN 0x0FFFFFF8U
#define CLKS_DISK_FAT32_EOC 0x0FFFFFFFU
#define CLKS_DISK_FAT32_FREE_CLUSTER 0x00000000U

#define CLKS_DISK_FAT32_BOOT_SIG_OFFSET 510U
#define CLKS_DISK_FAT32_TYPE_OFFSET 82U
#define CLKS_DISK_FAT32_TYPE_LEN 5U

#define CLKS_DISK_DIRENT_SIZE 32U
#define CLKS_DISK_DIRENT_NAME_LEN 11U
#define CLKS_DISK_LFN_MAX_ENTRIES 15U

#define CLKS_DISK_ATTR_READ_ONLY 0x01U
#define CLKS_DISK_ATTR_HIDDEN 0x02U
#define CLKS_DISK_ATTR_SYSTEM 0x04U
#define CLKS_DISK_ATTR_VOLUME_ID 0x08U
#define CLKS_DISK_ATTR_DIRECTORY 0x10U
#define CLKS_DISK_ATTR_ARCHIVE 0x20U
#define CLKS_DISK_ATTR_LFN 0x0FU

#define CLKS_DISK_INVALID_CLUSTER 0x00000000U

#if defined(CLKS_ARCH_X86_64)
#define CLKS_DISK_ATA_PRIMARY_IO_BASE 0x1F0U
#define CLKS_DISK_ATA_PRIMARY_CTRL_BASE 0x3F6U
#define CLKS_DISK_ATA_SECONDARY_IO_BASE 0x170U
#define CLKS_DISK_ATA_SECONDARY_CTRL_BASE 0x376U
#define CLKS_DISK_ATA_DRIVE_MASTER 0xA0U
#define CLKS_DISK_ATA_DRIVE_SLAVE 0xB0U
#define CLKS_DISK_ATA_DRIVE_LBA_MASTER 0xE0U
#define CLKS_DISK_ATA_DRIVE_LBA_SLAVE 0xF0U

#define CLKS_DISK_ATA_STATUS_ERR 0x01U
#define CLKS_DISK_ATA_STATUS_DRQ 0x08U
#define CLKS_DISK_ATA_STATUS_DF 0x20U
#define CLKS_DISK_ATA_STATUS_DRDY 0x40U
#define CLKS_DISK_ATA_STATUS_BSY 0x80U

#define CLKS_DISK_ATA_CMD_READ_SECTORS 0x20U
#define CLKS_DISK_ATA_CMD_WRITE_SECTORS 0x30U
#define CLKS_DISK_ATA_CMD_CACHE_FLUSH 0xE7U
#define CLKS_DISK_ATA_CMD_IDENTIFY 0xECU
#define CLKS_DISK_ATA_BATCH_SECTORS 128U
#endif

struct clks_disk_fat32_layout {
    clks_bool valid;
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 fat_count;
    u32 sectors_per_fat;
    u32 total_sectors;
    u32 root_cluster;
    u32 cluster_count;
    u32 cluster_size_bytes;
    u32 entries_per_cluster;
    u64 volume_lba;
    u64 fat_lba;
    u64 data_lba;
};

struct clks_disk_dir_slot {
    u32 cluster;
    u32 entry_index;
};

struct clks_disk_dir_info {
    u8 attr;
    u32 first_cluster;
    u32 size;
};

struct clks_disk_path_lookup {
    clks_bool found;
    u32 parent_cluster;
    u8 sfn[CLKS_DISK_DIRENT_NAME_LEN];
    struct clks_disk_dir_slot slot;
    struct clks_disk_dir_info info;
};

static u8 *clks_disk_bytes = CLKS_NULL;
static u64 clks_disk_bytes_len = 0ULL;
static u64 clks_disk_sector_total = 0ULL;
static u8 *clks_disk_loaded_bitmap = CLKS_NULL;
static u64 clks_disk_loaded_bitmap_bytes = 0ULL;
static clks_bool clks_disk_ready = CLKS_FALSE;
static clks_bool clks_disk_formatted = CLKS_FALSE;
static clks_bool clks_disk_mounted = CLKS_FALSE;
static clks_bool clks_disk_hw_backed = CLKS_FALSE;
static char clks_disk_mount_path_buf[CLKS_DISK_PATH_MAX];
static struct clks_disk_fat32_layout clks_disk_fat32;
static u32 clks_disk_alloc_hint = 2U;

static void *clks_disk_read_cache = CLKS_NULL;
static u64 clks_disk_read_cache_size = 0ULL;
static const u8 clks_disk_empty_file_data[1] = {0U};

#if defined(CLKS_ARCH_X86_64)
static u16 clks_disk_ata_io_base = CLKS_DISK_ATA_PRIMARY_IO_BASE;
static u16 clks_disk_ata_ctrl_base = CLKS_DISK_ATA_PRIMARY_CTRL_BASE;
static u8 clks_disk_ata_drive_select = CLKS_DISK_ATA_DRIVE_MASTER;
static u8 clks_disk_ata_drive_lba = CLKS_DISK_ATA_DRIVE_LBA_MASTER;
#endif

#include "disk/byteorder.inc"
#include "disk/ata.inc"
#include "disk/cache_io.inc"
#include "disk/path.inc"
#include "disk/fat_layout.inc"
#include "disk/fat_alloc.inc"
#include "disk/directory.inc"
#include "disk/file_chain.inc"
#include "disk/format.inc"
#include "disk/public_api.inc"
