void clks_disk_init(void) {
    u64 detected_sectors = 0ULL;
    u64 cache_bytes;
    u64 alloc_bytes;
    u64 heap_usable_bytes = 0ULL;
    u64 retry_step;
    struct clks_heap_stats heap_stats;

    if (clks_disk_bytes != CLKS_NULL) {
        clks_kfree(clks_disk_bytes);
        clks_disk_bytes = CLKS_NULL;
    }

    if (clks_disk_loaded_bitmap != CLKS_NULL) {
        clks_kfree(clks_disk_loaded_bitmap);
        clks_disk_loaded_bitmap = CLKS_NULL;
        clks_disk_loaded_bitmap_bytes = 0ULL;
    }

    clks_disk_ready = CLKS_FALSE;
    clks_disk_formatted = CLKS_FALSE;
    clks_disk_mounted = CLKS_FALSE;
    clks_disk_hw_backed = CLKS_FALSE;
    clks_disk_bytes_len = 0ULL;
    clks_disk_sector_total = 0ULL;
    clks_disk_mount_path_buf[0] = '\0';
    clks_memset(&clks_disk_fat32, 0, sizeof(clks_disk_fat32));
    clks_disk_alloc_hint = 2U;

    if (clks_disk_read_cache != CLKS_NULL) {
        clks_kfree(clks_disk_read_cache);
        clks_disk_read_cache = CLKS_NULL;
        clks_disk_read_cache_size = 0ULL;
    }

    if (clks_disk_ata_probe(&detected_sectors) == CLKS_FALSE || detected_sectors == 0ULL) {
        clks_log(CLKS_LOG_WARN, "DISK", "NO ATA DISK DETECTED (CHECK QEMU -DRIVE)");
        return;
    }

    cache_bytes = detected_sectors * CLKS_DISK_SECTOR_SIZE;
    if (cache_bytes / CLKS_DISK_SECTOR_SIZE != detected_sectors) {
        clks_log(CLKS_LOG_WARN, "DISK", "DISK SIZE OVERFLOW");
        return;
    }

    if (cache_bytes < CLKS_DISK_MIN_BYTES) {
        clks_log(CLKS_LOG_WARN, "DISK", "ATA DISK TOO SMALL FOR FAT32");
        clks_log_hex(CLKS_LOG_WARN, "DISK", "BYTES", cache_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "MIN_BYTES", CLKS_DISK_MIN_BYTES);
        return;
    }

    if (cache_bytes > CLKS_DISK_CACHE_MAX_BYTES) {
        cache_bytes = CLKS_DISK_CACHE_MAX_BYTES;
        clks_log(CLKS_LOG_WARN, "DISK", "DISK CACHE TRUNCATED");
    }

    heap_stats = clks_heap_get_stats();
    if (heap_stats.free_bytes > CLKS_DISK_HEAP_RESERVE_BYTES) {
        heap_usable_bytes = heap_stats.free_bytes - CLKS_DISK_HEAP_RESERVE_BYTES;
    }

    if (heap_usable_bytes < CLKS_DISK_MIN_BYTES) {
        clks_log(CLKS_LOG_WARN, "DISK", "INSUFFICIENT HEAP FOR DISK CACHE");
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_FREE_BYTES", heap_stats.free_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_RESERVE_BYTES", CLKS_DISK_HEAP_RESERVE_BYTES);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "MIN_BYTES", CLKS_DISK_MIN_BYTES);
        return;
    }

    alloc_bytes = cache_bytes;
    if (alloc_bytes > heap_usable_bytes) {
        alloc_bytes = heap_usable_bytes;
    }
    alloc_bytes -= (alloc_bytes % CLKS_DISK_SECTOR_SIZE);
    if (alloc_bytes < CLKS_DISK_MIN_BYTES) {
        clks_log(CLKS_LOG_WARN, "DISK", "DISK CACHE HEAP BUDGET TOO SMALL");
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_FREE_BYTES", heap_stats.free_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_USABLE_BYTES", heap_usable_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "MIN_BYTES", CLKS_DISK_MIN_BYTES);
        return;
    }
    retry_step = CLKS_DISK_ALLOC_RETRY_STEP_BYTES;
    if (retry_step < CLKS_DISK_SECTOR_SIZE) {
        retry_step = CLKS_DISK_SECTOR_SIZE;
    }
    retry_step -= (retry_step % CLKS_DISK_SECTOR_SIZE);

    while (alloc_bytes >= CLKS_DISK_MIN_BYTES) {
        clks_disk_bytes = (u8 *)clks_kmalloc((usize)alloc_bytes);
        if (clks_disk_bytes != CLKS_NULL) {
            cache_bytes = alloc_bytes;
            break;
        }

        if (alloc_bytes == CLKS_DISK_MIN_BYTES) {
            break;
        }

        if (alloc_bytes > (CLKS_DISK_MIN_BYTES + retry_step)) {
            alloc_bytes -= retry_step;
        } else {
            alloc_bytes = CLKS_DISK_MIN_BYTES;
        }

        alloc_bytes -= (alloc_bytes % CLKS_DISK_SECTOR_SIZE);
    }

    if (clks_disk_bytes == CLKS_NULL) {
        clks_log(CLKS_LOG_WARN, "DISK", "DISK BACKEND ALLOCATION FAILED");
        clks_log_hex(CLKS_LOG_WARN, "DISK", "CACHE_BYTES", cache_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_FREE_BYTES", heap_stats.free_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_RESERVE_BYTES", CLKS_DISK_HEAP_RESERVE_BYTES);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "HEAP_USABLE_BYTES", heap_usable_bytes);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "MIN_BYTES", CLKS_DISK_MIN_BYTES);
        return;
    }

    detected_sectors = cache_bytes / CLKS_DISK_SECTOR_SIZE;
    clks_memset(clks_disk_bytes, 0, (usize)cache_bytes);

    clks_disk_loaded_bitmap_bytes = clks_disk_loaded_bitmap_required_bytes(detected_sectors);
    if (clks_disk_loaded_bitmap_bytes == 0ULL) {
        clks_log(CLKS_LOG_WARN, "DISK", "DISK LOADED BITMAP SIZE INVALID");
        clks_kfree(clks_disk_bytes);
        clks_disk_bytes = CLKS_NULL;
        clks_disk_bytes_len = 0ULL;
        clks_disk_sector_total = 0ULL;
        return;
    }
    clks_disk_loaded_bitmap = (u8 *)clks_kmalloc((usize)clks_disk_loaded_bitmap_bytes);
    if (clks_disk_loaded_bitmap == CLKS_NULL) {
        clks_log(CLKS_LOG_WARN, "DISK", "DISK LOADED BITMAP ALLOCATION FAILED");
        clks_log_hex(CLKS_LOG_WARN, "DISK", "BITMAP_BYTES", clks_disk_loaded_bitmap_bytes);
        clks_kfree(clks_disk_bytes);
        clks_disk_bytes = CLKS_NULL;
        clks_disk_bytes_len = 0ULL;
        clks_disk_sector_total = 0ULL;
        return;
    }
    clks_memset(clks_disk_loaded_bitmap, 0, (usize)clks_disk_loaded_bitmap_bytes);

    clks_disk_bytes_len = cache_bytes;
    clks_disk_sector_total = detected_sectors;
    clks_disk_hw_backed = CLKS_TRUE;
    clks_disk_ready = CLKS_TRUE;
    if (clks_disk_ensure_loaded_range(0ULL, CLKS_DISK_SECTOR_SIZE) == CLKS_FALSE) {
        clks_log(CLKS_LOG_WARN, "DISK", "ATA BOOT SECTOR READ FAILED");
        clks_kfree(clks_disk_loaded_bitmap);
        clks_disk_loaded_bitmap = CLKS_NULL;
        clks_disk_loaded_bitmap_bytes = 0ULL;
        clks_kfree(clks_disk_bytes);
        clks_disk_bytes = CLKS_NULL;
        clks_disk_bytes_len = 0ULL;
        clks_disk_sector_total = 0ULL;
        clks_disk_ready = CLKS_FALSE;
        clks_disk_hw_backed = CLKS_FALSE;
        return;
    }
    clks_disk_formatted = clks_disk_parse_fat32_layout();

    if (clks_disk_formatted == CLKS_TRUE) {
        (void)clks_disk_mount("/temp/disk");
    }

    clks_log(CLKS_LOG_INFO, "DISK", "DISK BACKEND ONLINE");
    clks_log(CLKS_LOG_INFO, "DISK", "CACHE MODE: LAZY LOAD");
    clks_log_hex(CLKS_LOG_INFO, "DISK", "BYTES", clks_disk_bytes_len);
    clks_log_hex(CLKS_LOG_INFO, "DISK", "SECTORS", clks_disk_sector_total);
    clks_log_hex(CLKS_LOG_INFO, "DISK", "FAT32", (clks_disk_formatted == CLKS_TRUE) ? 1ULL : 0ULL);
    clks_log_hex(CLKS_LOG_INFO, "DISK", "HW_BACKED", (clks_disk_hw_backed == CLKS_TRUE) ? 1ULL : 0ULL);
}

clks_bool clks_disk_present(void) {
    return clks_disk_ready;
}

u64 clks_disk_size_bytes(void) {
    return (clks_disk_ready == CLKS_TRUE) ? clks_disk_bytes_len : 0ULL;
}

u64 clks_disk_sector_count(void) {
    return (clks_disk_ready == CLKS_TRUE) ? clks_disk_sector_total : 0ULL;
}

clks_bool clks_disk_read_sector(u64 lba, void *out_sector) {
    if (clks_disk_ready == CLKS_FALSE || out_sector == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (lba >= clks_disk_sector_total) {
        return CLKS_FALSE;
    }

    if (clks_disk_hw_backed == CLKS_TRUE) {
        if (clks_disk_ata_read_sector_hw(lba, out_sector) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        clks_memcpy(clks_disk_bytes + (usize)(lba * CLKS_DISK_SECTOR_SIZE), out_sector, (usize)CLKS_DISK_SECTOR_SIZE);
        clks_disk_loaded_mark_range(lba, 1ULL);
        return CLKS_TRUE;
    }

    clks_memcpy(out_sector, clks_disk_bytes + (usize)(lba * CLKS_DISK_SECTOR_SIZE), (usize)CLKS_DISK_SECTOR_SIZE);
    return CLKS_TRUE;
}

clks_bool clks_disk_write_sector(u64 lba, const void *sector_data) {
    if (clks_disk_ready == CLKS_FALSE || sector_data == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (lba >= clks_disk_sector_total) {
        return CLKS_FALSE;
    }

    clks_memcpy(clks_disk_bytes + (usize)(lba * CLKS_DISK_SECTOR_SIZE), sector_data, (usize)CLKS_DISK_SECTOR_SIZE);
    clks_disk_loaded_mark_range(lba, 1ULL);

    if (clks_disk_hw_backed == CLKS_TRUE) {
        if (clks_disk_ata_write_sector_hw(lba, sector_data) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if (clks_disk_ata_cache_flush_hw() == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

clks_bool clks_disk_is_formatted_fat32(void) {
    return (clks_disk_ready == CLKS_TRUE && clks_disk_formatted == CLKS_TRUE) ? CLKS_TRUE : CLKS_FALSE;
}

clks_bool clks_disk_format_fat32(const char *label) {
    if (clks_disk_ready == CLKS_FALSE || clks_disk_bytes == CLKS_NULL) {
        clks_log(CLKS_LOG_WARN, "DISK", "FAT32 FORMAT FAILED: DISK NOT READY");
        return CLKS_FALSE;
    }

    clks_memset(clks_disk_bytes, 0, (usize)clks_disk_bytes_len);

    if (clks_disk_write_fat32_boot_sector(label) == CLKS_FALSE) {
        clks_log(CLKS_LOG_WARN, "DISK", "FAT32 FORMAT FAILED: BOOT/FAT INIT");
        clks_log_hex(CLKS_LOG_WARN, "DISK", "BYTES", clks_disk_bytes_len);
        clks_log_hex(CLKS_LOG_WARN, "DISK", "SECTORS", clks_disk_sector_total);
        clks_disk_formatted = CLKS_FALSE;
        clks_memset(&clks_disk_fat32, 0, sizeof(clks_disk_fat32));
        return CLKS_FALSE;
    }

    clks_disk_formatted = clks_disk_parse_fat32_layout();
    if (clks_disk_formatted == CLKS_FALSE) {
        clks_log(CLKS_LOG_WARN, "DISK", "FAT32 FORMAT FAILED: LAYOUT PARSE");
        return CLKS_FALSE;
    }

    clks_log(CLKS_LOG_INFO, "DISK", "FAT32 FORMAT COMPLETE");
    return CLKS_TRUE;
}

clks_bool clks_disk_mount(const char *mount_path) {
    char normalized[CLKS_DISK_PATH_MAX];

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_normalize_absolute_path(mount_path, normalized, sizeof(normalized)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_strcmp(normalized, "/") == 0) {
        return CLKS_FALSE;
    }

    clks_disk_copy_text(clks_disk_mount_path_buf, sizeof(clks_disk_mount_path_buf), normalized);
    clks_disk_mounted = CLKS_TRUE;
    return CLKS_TRUE;
}

clks_bool clks_disk_is_mounted(void) {
    return (clks_disk_ready == CLKS_TRUE && clks_disk_mounted == CLKS_TRUE) ? CLKS_TRUE : CLKS_FALSE;
}

const char *clks_disk_mount_path(void) {
    if (clks_disk_mounted == CLKS_FALSE) {
        return "";
    }

    return clks_disk_mount_path_buf;
}

clks_bool clks_disk_path_in_mount(const char *path) {
    char relative[CLKS_DISK_PATH_MAX];
    return (clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_TRUE) ? CLKS_TRUE : CLKS_FALSE;
}

clks_bool clks_disk_stat(const char *path, u64 *out_type, u64 *out_size) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;

    if (out_type == CLKS_NULL || out_size == CLKS_NULL ||
        clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (relative[0] == '\0') {
        *out_type = CLKS_DISK_NODE_DIR;
        *out_size = 0ULL;
        return CLKS_TRUE;
    }

    if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE || lookup.found == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    *out_type = ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) ? CLKS_DISK_NODE_DIR : CLKS_DISK_NODE_FILE;
    *out_size = (u64)lookup.info.size;
    return CLKS_TRUE;
}

const void *clks_disk_read_all(const char *path, u64 *out_size) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;

    if (clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_NULL;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE || relative[0] == '\0') {
        return CLKS_NULL;
    }

    if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE || lookup.found == CLKS_FALSE) {
        return CLKS_NULL;
    }

    if ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) {
        return CLKS_NULL;
    }

    if (out_size != CLKS_NULL) {
        *out_size = (u64)lookup.info.size;
    }

    if (lookup.info.size == 0U) {
        return (const void *)clks_disk_empty_file_data;
    }

    if (clks_disk_prepare_read_cache((u64)lookup.info.size) == CLKS_FALSE) {
        return CLKS_NULL;
    }

    if (clks_disk_read_file_chain(lookup.info.first_cluster, clks_disk_read_cache, (u64)lookup.info.size) ==
        CLKS_FALSE) {
        return CLKS_NULL;
    }

    return (const void *)clks_disk_read_cache;
}

u64 clks_disk_count_children(const char *dir_path) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;
    u32 dir_cluster;
    u32 cluster;
    u32 guard = 0U;
    u64 count = 0ULL;

    if (clks_disk_path_to_relative(dir_path, relative, sizeof(relative)) == CLKS_FALSE) {
        return 0ULL;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE) {
        return 0ULL;
    }

    if (relative[0] == '\0') {
        dir_cluster = clks_disk_fat32.root_cluster;
    } else {
        if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE || lookup.found == CLKS_FALSE) {
            return 0ULL;
        }
        if ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) == 0U) {
            return 0ULL;
        }
        dir_cluster = lookup.info.first_cluster;
    }

    cluster = dir_cluster;
    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);

            if (entry == CLKS_NULL) {
                return 0ULL;
            }

            if (entry[0] == 0x00U) {
                return count;
            }

            if (clks_disk_dirent_is_visible(entry) == CLKS_FALSE) {
                continue;
            }

            if (clks_disk_dirent_is_dot_like(entry) == CLKS_TRUE) {
                continue;
            }

            count++;
        }

        {
            u32 next_cluster;
            clks_bool is_eoc;
            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                return 0ULL;
            }
            if (is_eoc == CLKS_TRUE) {
                return count;
            }
            if (clks_disk_cluster_valid(next_cluster) == CLKS_FALSE) {
                return 0ULL;
            }
            cluster = next_cluster;
            guard++;
        }
    }

    return count;
}

clks_bool clks_disk_get_child_name(const char *dir_path, u64 index, char *out_name, usize out_name_size) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;
    u32 dir_cluster;
    u32 cluster;
    u32 guard = 0U;
    u64 current = 0ULL;

    if (out_name == CLKS_NULL || out_name_size == 0U) {
        return CLKS_FALSE;
    }

    out_name[0] = '\0';

    if (clks_disk_path_to_relative(dir_path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (relative[0] == '\0') {
        dir_cluster = clks_disk_fat32.root_cluster;
    } else {
        if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE || lookup.found == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) == 0U) {
            return CLKS_FALSE;
        }
        dir_cluster = lookup.info.first_cluster;
    }

    cluster = dir_cluster;
    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);

            if (entry == CLKS_NULL) {
                return CLKS_FALSE;
            }

            if (entry[0] == 0x00U) {
                return CLKS_FALSE;
            }

            if (clks_disk_dirent_is_visible(entry) == CLKS_FALSE) {
                continue;
            }

            if (clks_disk_dirent_is_dot_like(entry) == CLKS_TRUE) {
                continue;
            }

            if (current != index) {
                current++;
                continue;
            }

            clks_disk_sfn_to_text(entry, out_name, out_name_size);
            return (out_name[0] != '\0') ? CLKS_TRUE : CLKS_FALSE;
        }

        {
            u32 next_cluster;
            clks_bool is_eoc;
            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                return CLKS_FALSE;
            }
            if (is_eoc == CLKS_TRUE) {
                return CLKS_FALSE;
            }
            if (clks_disk_cluster_valid(next_cluster) == CLKS_FALSE) {
                return CLKS_FALSE;
            }
            cluster = next_cluster;
            guard++;
        }
    }

    return CLKS_FALSE;
}

clks_bool clks_disk_mkdir(const char *path) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;
    struct clks_disk_dir_slot free_slot;
    u8 entry[CLKS_DISK_DIRENT_SIZE];
    u32 new_cluster;

    if (clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (relative[0] == '\0') {
        return CLKS_TRUE;
    }

    if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (lookup.found == CLKS_TRUE) {
        return ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) ? CLKS_TRUE : CLKS_FALSE;
    }

    if (clks_disk_cluster_valid(lookup.parent_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_dir_find_free_slot(lookup.parent_cluster, &free_slot) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_alloc_cluster(&new_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_init_directory_cluster(new_cluster, lookup.parent_cluster) == CLKS_FALSE) {
        clks_disk_free_chain(new_cluster);
        return CLKS_FALSE;
    }

    clks_disk_build_dir_entry(entry, lookup.sfn, CLKS_DISK_ATTR_DIRECTORY, new_cluster, 0U);

    if (clks_disk_write_dir_entry(&free_slot, entry) == CLKS_FALSE) {
        clks_disk_free_chain(new_cluster);
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

clks_bool clks_disk_write_all(const char *path, const void *data, u64 size) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;
    struct clks_disk_dir_slot target_slot;
    u32 old_cluster = CLKS_DISK_INVALID_CLUSTER;
    u32 new_cluster = CLKS_DISK_INVALID_CLUSTER;
    u32 needed_clusters = 0U;
    u8 entry[CLKS_DISK_DIRENT_SIZE];

    if (clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE || relative[0] == '\0') {
        return CLKS_FALSE;
    }

    if (size > 0ULL && data == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (lookup.found == CLKS_TRUE && (lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) {
        return CLKS_FALSE;
    }

    if (lookup.found == CLKS_TRUE) {
        target_slot = lookup.slot;
        old_cluster = lookup.info.first_cluster;
    } else {
        if (clks_disk_cluster_valid(lookup.parent_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if (clks_disk_dir_find_free_slot(lookup.parent_cluster, &target_slot) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    if (size > 0ULL) {
        u64 cluster_size = (u64)clks_disk_fat32.cluster_size_bytes;
        u64 need64 = (size + cluster_size - 1ULL) / cluster_size;
        if (need64 == 0ULL || need64 > (u64)clks_disk_fat32.cluster_count) {
            return CLKS_FALSE;
        }
        needed_clusters = (u32)need64;
    }

    if (old_cluster != CLKS_DISK_INVALID_CLUSTER) {
        clks_disk_free_chain(old_cluster);
    }

    if (needed_clusters > 0U) {
        if (clks_disk_alloc_chain(needed_clusters, &new_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (clks_disk_write_file_chain(new_cluster, data, size) == CLKS_FALSE) {
            clks_disk_free_chain(new_cluster);
            return CLKS_FALSE;
        }
    }

    clks_disk_build_dir_entry(entry, lookup.sfn, CLKS_DISK_ATTR_ARCHIVE, new_cluster, (u32)size);

    if (clks_disk_write_dir_entry(&target_slot, entry) == CLKS_FALSE) {
        if (new_cluster != CLKS_DISK_INVALID_CLUSTER) {
            clks_disk_free_chain(new_cluster);
        }
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

clks_bool clks_disk_append(const char *path, const void *data, u64 size) {
    char relative[CLKS_DISK_PATH_MAX];
    const void *old_data;
    u64 old_size = 0ULL;
    u64 new_size;
    void *merged;
    struct clks_disk_path_lookup lookup;

    if (clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE || relative[0] == '\0') {
        return CLKS_FALSE;
    }

    if (size > 0ULL && data == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (lookup.found == CLKS_FALSE) {
        return clks_disk_write_all(path, data, size);
    }

    if ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) {
        return CLKS_FALSE;
    }

    old_data = clks_disk_read_all(path, &old_size);
    if (old_data == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (old_size > (0xFFFFFFFFFFFFFFFFULL - size)) {
        return CLKS_FALSE;
    }

    new_size = old_size + size;
    if (new_size == 0ULL) {
        return clks_disk_write_all(path, clks_disk_empty_file_data, 0ULL);
    }

    merged = clks_kmalloc((usize)new_size);
    if (merged == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (old_size > 0ULL) {
        clks_memcpy(merged, old_data, (usize)old_size);
    }
    if (size > 0ULL) {
        clks_memcpy((u8 *)merged + (usize)old_size, data, (usize)size);
    }

    if (clks_disk_write_all(path, merged, new_size) == CLKS_FALSE) {
        clks_kfree(merged);
        return CLKS_FALSE;
    }

    clks_kfree(merged);
    return CLKS_TRUE;
}

clks_bool clks_disk_remove(const char *path) {
    char relative[CLKS_DISK_PATH_MAX];
    struct clks_disk_path_lookup lookup;
    u64 lba = 0ULL;
    u8 *entry;

    if (clks_disk_path_to_relative(path, relative, sizeof(relative)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE || relative[0] == '\0') {
        return CLKS_FALSE;
    }

    if (clks_disk_lookup_relative_path(relative, &lookup) == CLKS_FALSE || lookup.found == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if ((lookup.info.attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) {
        if (clks_disk_cluster_valid(lookup.info.first_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if (clks_disk_dir_is_empty(lookup.info.first_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    entry = clks_disk_dir_entry_ptr(lookup.slot.cluster, lookup.slot.entry_index, &lba);
    if (entry == CLKS_NULL) {
        return CLKS_FALSE;
    }

    entry[0] = 0xE5U;
    if (clks_disk_sync_sector_range(lba, 1ULL) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_cluster_valid(lookup.info.first_cluster) == CLKS_TRUE) {
        clks_disk_free_chain(lookup.info.first_cluster);
    }

    return CLKS_TRUE;
}

static u64 clks_disk_count_nodes_recursive(u32 dir_cluster, u32 depth) {
    u32 cluster = dir_cluster;
    u32 guard = 0U;
    u64 count = 0ULL;

    if (depth > 64U) {
        return 0ULL;
    }

    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);
            u8 attr;
            u32 sub_cluster;

            if (entry == CLKS_NULL) {
                return count;
            }

            if (entry[0] == 0x00U) {
                return count;
            }

            if (clks_disk_dirent_is_visible(entry) == CLKS_FALSE) {
                continue;
            }

            if (clks_disk_dirent_is_dot_like(entry) == CLKS_TRUE) {
                continue;
            }

            count++;
            attr = entry[11U];
            if ((attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) {
                sub_cluster = clks_disk_dirent_first_cluster(entry);
                if (clks_disk_cluster_valid(sub_cluster) == CLKS_TRUE) {
                    count += clks_disk_count_nodes_recursive(sub_cluster, depth + 1U);
                }
            }
        }

        {
            u32 next_cluster;
            clks_bool is_eoc;
            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                return count;
            }
            if (is_eoc == CLKS_TRUE) {
                return count;
            }
            if (clks_disk_cluster_valid(next_cluster) == CLKS_FALSE) {
                return count;
            }
            cluster = next_cluster;
            guard++;
        }
    }

    return count;
}

u64 clks_disk_node_count(void) {
    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE) {
        return 0ULL;
    }

    return 1ULL + clks_disk_count_nodes_recursive(clks_disk_fat32.root_cluster, 0U);
}
