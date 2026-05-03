static clks_bool clks_disk_format_choose_spc(u32 total_sectors, u8 *out_spc) {
    if (out_spc == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (total_sectors <= 532480U) {
        *out_spc = 1U;
    } else if (total_sectors <= 16777216U) {
        *out_spc = 8U;
    } else if (total_sectors <= 33554432U) {
        *out_spc = 16U;
    } else if (total_sectors <= 67108864U) {
        *out_spc = 32U;
    } else {
        *out_spc = 64U;
    }

    return CLKS_TRUE;
}

static void clks_disk_label_to_boot_field(char *out_label, const char *label) {
    usize i = 0U;

    for (i = 0U; i < 11U; i++) {
        out_label[i] = ' ';
    }

    if (label == CLKS_NULL || label[0] == '\0') {
        const char default_label[11] = {'C', 'L', 'E', 'O', 'N', 'O', 'S', 'D', 'I', 'S', 'K'};
        for (i = 0U; i < 11U; i++) {
            out_label[i] = default_label[i];
        }
        return;
    }

    i = 0U;
    while (label[i] != '\0' && i < 11U) {
        out_label[i] = clks_disk_ascii_upper(label[i]);
        i++;
    }
}

static clks_bool clks_disk_write_fat32_boot_sector(const char *label) {
    u64 volume_lba = CLKS_DISK_FAT32_PARTITION_LBA;
    u64 volume_offset;
    u64 reserved_bytes;
    u8 *boot;
    u8 *fsinfo;
    u8 *backup_boot;
    u8 *backup_fsinfo;
    u32 total_sectors;
    u8 sectors_per_cluster;
    u16 reserved = 32U;
    u8 fats = 2U;
    u32 fat_sectors = 1U;
    u32 iter;
    u64 cluster_count = 0ULL;
    char label_field[11];
    u64 fat_region_lba;
    u32 fat_index;

    if (clks_disk_bytes == CLKS_NULL || clks_disk_sector_total == 0ULL || clks_disk_sector_total > 0xFFFFFFFFULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_sector_total <= volume_lba) {
        return CLKS_FALSE;
    }

    volume_offset = volume_lba * CLKS_DISK_SECTOR_SIZE;
    if (clks_disk_bytes_range_valid(volume_offset, CLKS_DISK_SECTOR_SIZE) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    boot = clks_disk_bytes + (usize)volume_offset;
    total_sectors = (u32)(clks_disk_sector_total - volume_lba);
    if (clks_disk_format_choose_spc(total_sectors, &sectors_per_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    for (iter = 0U; iter < 16U; iter++) {
        u64 data_sectors;
        u64 next_fat;

        if ((u64)total_sectors <= (u64)reserved + ((u64)fats * (u64)fat_sectors)) {
            return CLKS_FALSE;
        }

        data_sectors = (u64)total_sectors - (u64)reserved - ((u64)fats * (u64)fat_sectors);
        cluster_count = data_sectors / (u64)sectors_per_cluster;
        next_fat = ((cluster_count + 2ULL) * 4ULL + (CLKS_DISK_SECTOR_SIZE - 1ULL)) / CLKS_DISK_SECTOR_SIZE;

        if (next_fat > 0xFFFFFFFFULL) {
            return CLKS_FALSE;
        }

        if ((u32)next_fat == fat_sectors) {
            break;
        }

        fat_sectors = (u32)next_fat;
    }

    if (cluster_count < (u64)CLKS_DISK_FAT32_MIN_CLUSTER_COUNT || cluster_count > 0x0FFFFFF5ULL) {
        return CLKS_FALSE;
    }

    if ((u64)reserved + ((u64)fats * (u64)fat_sectors) >= (u64)total_sectors) {
        return CLKS_FALSE;
    }

    if (clks_disk_bytes_range_valid(0ULL, CLKS_DISK_SECTOR_SIZE) == CLKS_FALSE) {
        return CLKS_FALSE;
    }
    clks_memset(clks_disk_bytes, 0, CLKS_DISK_SECTOR_SIZE);
    clks_disk_loaded_mark_range(0ULL, 1ULL);

    reserved_bytes = (u64)reserved * CLKS_DISK_SECTOR_SIZE;
    if (clks_disk_bytes_range_valid(volume_offset, reserved_bytes) == CLKS_FALSE) {
        return CLKS_FALSE;
    }
    clks_memset(clks_disk_bytes + (usize)volume_offset, 0, (usize)reserved_bytes);
    clks_disk_loaded_mark_range(volume_lba, (u64)reserved);

    boot[0U] = 0xEBU;
    boot[1U] = 0x58U;
    boot[2U] = 0x90U;
    clks_memcpy(boot + 3U, "MSDOS5.0", 8U);
    clks_disk_write_u16(boot + 11U, (u16)CLKS_DISK_SECTOR_SIZE);
    boot[13U] = sectors_per_cluster;
    clks_disk_write_u16(boot + 14U, reserved);
    boot[16U] = fats;
    clks_disk_write_u16(boot + 17U, 0U);
    clks_disk_write_u16(boot + 19U, 0U);
    boot[21U] = 0xF8U;
    clks_disk_write_u16(boot + 22U, 0U);
    clks_disk_write_u16(boot + 24U, 63U);
    clks_disk_write_u16(boot + 26U, 255U);
    clks_disk_write_u32(boot + 28U, (u32)volume_lba);
    clks_disk_write_u32(boot + 32U, total_sectors);
    clks_disk_write_u32(boot + 36U, fat_sectors);
    clks_disk_write_u16(boot + 40U, 0U);
    clks_disk_write_u16(boot + 42U, 0U);
    clks_disk_write_u32(boot + 44U, 2U);
    clks_disk_write_u16(boot + 48U, 1U);
    clks_disk_write_u16(boot + 50U, 6U);
    boot[64U] = 0x80U;
    boot[66U] = 0x29U;
    clks_disk_write_u32(boot + 67U, 0x434C4B53U);
    clks_disk_label_to_boot_field(label_field, label);
    clks_memcpy(boot + 71U, label_field, 11U);
    clks_memcpy(boot + 82U, "FAT32   ", 8U);
    boot[CLKS_DISK_FAT32_BOOT_SIG_OFFSET] = 0x55U;
    boot[CLKS_DISK_FAT32_BOOT_SIG_OFFSET + 1U] = 0xAAU;

    fsinfo = clks_disk_bytes + (usize)((volume_lba + 1ULL) * CLKS_DISK_SECTOR_SIZE);
    clks_memset(fsinfo, 0, CLKS_DISK_SECTOR_SIZE);
    clks_disk_write_u32(fsinfo + 0U, 0x41615252U);
    clks_disk_write_u32(fsinfo + 484U, 0x61417272U);
    clks_disk_write_u32(fsinfo + 488U, 0xFFFFFFFFU);
    clks_disk_write_u32(fsinfo + 492U, 0xFFFFFFFFU);
    fsinfo[CLKS_DISK_FAT32_BOOT_SIG_OFFSET] = 0x55U;
    fsinfo[CLKS_DISK_FAT32_BOOT_SIG_OFFSET + 1U] = 0xAAU;

    backup_boot = clks_disk_bytes + (usize)((volume_lba + 6ULL) * CLKS_DISK_SECTOR_SIZE);
    backup_fsinfo = clks_disk_bytes + (usize)((volume_lba + 7ULL) * CLKS_DISK_SECTOR_SIZE);
    clks_memcpy(backup_boot, boot, CLKS_DISK_SECTOR_SIZE);
    clks_memcpy(backup_fsinfo, fsinfo, CLKS_DISK_SECTOR_SIZE);

    fat_region_lba = volume_lba + (u64)reserved;
    for (fat_index = 0U; fat_index < fats; fat_index++) {
        u64 fat_lba = fat_region_lba + ((u64)fat_index * (u64)fat_sectors);
        u64 fat_offset = fat_lba * CLKS_DISK_SECTOR_SIZE;
        u64 fat_bytes = (u64)fat_sectors * CLKS_DISK_SECTOR_SIZE;
        u8 *fat_ptr;

        if (clks_disk_bytes_range_valid(fat_offset, fat_bytes) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        fat_ptr = clks_disk_bytes + (usize)fat_offset;
        clks_memset(fat_ptr, 0, (usize)fat_bytes);
        clks_disk_loaded_mark_range(fat_lba, (u64)fat_sectors);
        clks_disk_write_u32(fat_ptr + 0U, 0x0FFFFFF8U);
        clks_disk_write_u32(fat_ptr + 4U, 0x0FFFFFFFU);
        clks_disk_write_u32(fat_ptr + 8U, CLKS_DISK_FAT32_EOC);
    }

    if (clks_disk_parse_fat32_layout() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_init_directory_cluster(clks_disk_fat32.root_cluster, clks_disk_fat32.root_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_sync_sector_range(0ULL, 1ULL) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_sync_sector_range(volume_lba, (u64)reserved) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    for (fat_index = 0U; fat_index < fats; fat_index++) {
        u64 fat_lba = fat_region_lba + ((u64)fat_index * (u64)fat_sectors);
        if (clks_disk_sync_sector_range(fat_lba, (u64)fat_sectors) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}
