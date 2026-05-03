static clks_bool clks_disk_parse_fat32_layout(void) {
    const u8 *boot;
    u64 volume_lba = 0ULL;
    u64 boot_offset = 0ULL;
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 fat_count;
    u32 sectors_per_fat;
    u32 total_sectors_16;
    u32 total_sectors_32;
    u32 total_sectors;
    u32 root_cluster;
    u64 data_sectors;
    u64 cluster_count;
    const u8 fat32_sig[CLKS_DISK_FAT32_TYPE_LEN] = {'F', 'A', 'T', '3', '2'};

    clks_memset(&clks_disk_fat32, 0, sizeof(clks_disk_fat32));
    clks_disk_alloc_hint = 2U;

    if (clks_disk_ready == CLKS_FALSE || clks_disk_bytes == CLKS_NULL || clks_disk_bytes_len < CLKS_DISK_SECTOR_SIZE) {
        return CLKS_FALSE;
    }

    if (clks_disk_ensure_loaded_range(0ULL, CLKS_DISK_SECTOR_SIZE) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    boot = clks_disk_bytes;

    if (boot[CLKS_DISK_FAT32_BOOT_SIG_OFFSET] != 0x55U ||
        boot[CLKS_DISK_FAT32_BOOT_SIG_OFFSET + 1U] != 0xAAU ||
        clks_disk_bytes_equal(boot + CLKS_DISK_FAT32_TYPE_OFFSET, fat32_sig, CLKS_DISK_FAT32_TYPE_LEN) ==
            CLKS_FALSE) {
        u64 shifted_offset = CLKS_DISK_FAT32_PARTITION_LBA * CLKS_DISK_SECTOR_SIZE;

        if (shifted_offset > clks_disk_bytes_len ||
            (CLKS_DISK_SECTOR_SIZE > (clks_disk_bytes_len - shifted_offset))) {
            return CLKS_FALSE;
        }

        if (clks_disk_ensure_loaded_range(shifted_offset, CLKS_DISK_SECTOR_SIZE) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        volume_lba = CLKS_DISK_FAT32_PARTITION_LBA;
        boot_offset = shifted_offset;
        boot = clks_disk_bytes + (usize)boot_offset;
    }

    if (boot[CLKS_DISK_FAT32_BOOT_SIG_OFFSET] != 0x55U || boot[CLKS_DISK_FAT32_BOOT_SIG_OFFSET + 1U] != 0xAAU) {
        return CLKS_FALSE;
    }

    bytes_per_sector = clks_disk_read_u16(boot + 11U);
    sectors_per_cluster = boot[13U];
    reserved_sectors = clks_disk_read_u16(boot + 14U);
    fat_count = boot[16U];
    sectors_per_fat = clks_disk_read_u32(boot + 36U);
    root_cluster = clks_disk_read_u32(boot + 44U);
    total_sectors_16 = (u32)clks_disk_read_u16(boot + 19U);
    total_sectors_32 = clks_disk_read_u32(boot + 32U);
    total_sectors = (total_sectors_16 != 0U) ? total_sectors_16 : total_sectors_32;

    if (bytes_per_sector != (u16)CLKS_DISK_SECTOR_SIZE) {
        return CLKS_FALSE;
    }

    if (sectors_per_cluster == 0U || (sectors_per_cluster & (sectors_per_cluster - 1U)) != 0U) {
        return CLKS_FALSE;
    }

    if (reserved_sectors == 0U || fat_count < 1U || sectors_per_fat == 0U || total_sectors == 0U) {
        return CLKS_FALSE;
    }

    if (root_cluster < 2U) {
        return CLKS_FALSE;
    }

    if (clks_disk_bytes_equal(boot + CLKS_DISK_FAT32_TYPE_OFFSET, fat32_sig, CLKS_DISK_FAT32_TYPE_LEN) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if ((u64)total_sectors > (clks_disk_sector_total - volume_lba)) {
        return CLKS_FALSE;
    }

    if ((u64)total_sectors <= (u64)reserved_sectors + ((u64)fat_count * (u64)sectors_per_fat)) {
        return CLKS_FALSE;
    }

    data_sectors = (u64)total_sectors - (u64)reserved_sectors - ((u64)fat_count * (u64)sectors_per_fat);
    cluster_count = data_sectors / (u64)sectors_per_cluster;

    if (cluster_count < (u64)CLKS_DISK_FAT32_MIN_CLUSTER_COUNT || cluster_count > 0x0FFFFFF5ULL) {
        return CLKS_FALSE;
    }

    if (root_cluster >= (u32)(cluster_count + 2ULL)) {
        return CLKS_FALSE;
    }

    clks_disk_fat32.valid = CLKS_TRUE;
    clks_disk_fat32.bytes_per_sector = bytes_per_sector;
    clks_disk_fat32.sectors_per_cluster = sectors_per_cluster;
    clks_disk_fat32.reserved_sectors = reserved_sectors;
    clks_disk_fat32.fat_count = fat_count;
    clks_disk_fat32.sectors_per_fat = sectors_per_fat;
    clks_disk_fat32.total_sectors = total_sectors;
    clks_disk_fat32.root_cluster = root_cluster;
    clks_disk_fat32.cluster_count = (u32)cluster_count;
    clks_disk_fat32.cluster_size_bytes = (u32)((u32)sectors_per_cluster * (u32)CLKS_DISK_SECTOR_SIZE);
    clks_disk_fat32.entries_per_cluster = clks_disk_fat32.cluster_size_bytes / CLKS_DISK_DIRENT_SIZE;
    clks_disk_fat32.volume_lba = volume_lba;
    clks_disk_fat32.fat_lba = volume_lba + (u64)reserved_sectors;
    clks_disk_fat32.data_lba = volume_lba + (u64)reserved_sectors + ((u64)fat_count * (u64)sectors_per_fat);

    return CLKS_TRUE;
}

static clks_bool clks_disk_cluster_valid(u32 cluster) {
    if (clks_disk_fat32.valid == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return (cluster >= 2U && cluster < (clks_disk_fat32.cluster_count + 2U)) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_disk_cluster_lba(u32 cluster, u64 *out_lba) {
    u64 lba;

    if (out_lba == CLKS_NULL || clks_disk_cluster_valid(cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    lba = clks_disk_fat32.data_lba + ((u64)(cluster - 2U) * (u64)clks_disk_fat32.sectors_per_cluster);

    if (lba >= clks_disk_sector_total) {
        return CLKS_FALSE;
    }

    if ((u64)clks_disk_fat32.sectors_per_cluster > (clks_disk_sector_total - lba)) {
        return CLKS_FALSE;
    }

    *out_lba = lba;
    return CLKS_TRUE;
}

static clks_bool clks_disk_fat_is_eoc(u32 value) {
    return ((value & 0x0FFFFFFFU) >= CLKS_DISK_FAT32_EOC_MIN) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_disk_fat_read_entry(u32 cluster, u32 *out_value) {
    u64 fat_byte_offset;
    u64 fat_sector_index;
    u64 fat_sector_offset;
    u64 fat_lba;
    u64 disk_offset;
    u32 value;

    if (out_value == CLKS_NULL || clks_disk_fat32.valid == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (cluster >= (clks_disk_fat32.cluster_count + 2U)) {
        return CLKS_FALSE;
    }

    fat_byte_offset = (u64)cluster * 4ULL;
    fat_sector_index = fat_byte_offset / CLKS_DISK_SECTOR_SIZE;
    fat_sector_offset = fat_byte_offset % CLKS_DISK_SECTOR_SIZE;

    if (fat_sector_index >= (u64)clks_disk_fat32.sectors_per_fat) {
        return CLKS_FALSE;
    }

    fat_lba = clks_disk_fat32.fat_lba + fat_sector_index;
    disk_offset = fat_lba * CLKS_DISK_SECTOR_SIZE + fat_sector_offset;

    if (clks_disk_bytes_range_valid(disk_offset, 4ULL) == CLKS_FALSE) {
        return CLKS_FALSE;
    }
    if (clks_disk_ensure_loaded_range(disk_offset, 4ULL) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    value = clks_disk_read_u32(clks_disk_bytes + (usize)disk_offset);
    *out_value = value & 0x0FFFFFFFU;
    return CLKS_TRUE;
}

static clks_bool clks_disk_fat_write_entry(u32 cluster, u32 value) {
    u64 fat_byte_offset;
    u64 fat_sector_index;
    u64 fat_sector_offset;
    u8 fat_index;

    if (clks_disk_fat32.valid == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (cluster >= (clks_disk_fat32.cluster_count + 2U)) {
        return CLKS_FALSE;
    }

    fat_byte_offset = (u64)cluster * 4ULL;
    fat_sector_index = fat_byte_offset / CLKS_DISK_SECTOR_SIZE;
    fat_sector_offset = fat_byte_offset % CLKS_DISK_SECTOR_SIZE;

    if (fat_sector_index >= (u64)clks_disk_fat32.sectors_per_fat) {
        return CLKS_FALSE;
    }

    for (fat_index = 0U; fat_index < clks_disk_fat32.fat_count; fat_index++) {
        u64 fat_lba =
            clks_disk_fat32.fat_lba + ((u64)fat_index * (u64)clks_disk_fat32.sectors_per_fat) + fat_sector_index;
        u64 disk_offset = fat_lba * CLKS_DISK_SECTOR_SIZE + fat_sector_offset;
        u32 old_value;
        u32 new_value;

        if (clks_disk_bytes_range_valid(disk_offset, 4ULL) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if (clks_disk_ensure_loaded_range(disk_offset, 4ULL) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        old_value = clks_disk_read_u32(clks_disk_bytes + (usize)disk_offset);
        new_value = (old_value & 0xF0000000U) | (value & 0x0FFFFFFFU);
        clks_disk_write_u32(clks_disk_bytes + (usize)disk_offset, new_value);

        if (clks_disk_sync_sector_range(fat_lba, 1ULL) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

static clks_bool clks_disk_fat_next_cluster(u32 cluster, u32 *out_next, clks_bool *out_eoc) {
    u32 value;

    if (out_next == CLKS_NULL || out_eoc == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_fat_read_entry(cluster, &value) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    *out_eoc = clks_disk_fat_is_eoc(value);
    *out_next = value & 0x0FFFFFFFU;
    return CLKS_TRUE;
}
