static clks_bool clks_disk_zero_cluster(u32 cluster) {
    u64 lba;
    u64 offset;
    u64 byte_count;

    if (clks_disk_cluster_lba(cluster, &lba) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    offset = lba * CLKS_DISK_SECTOR_SIZE;
    byte_count = (u64)clks_disk_fat32.sectors_per_cluster * CLKS_DISK_SECTOR_SIZE;

    if (clks_disk_bytes_range_valid(offset, byte_count) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_memset(clks_disk_bytes + (usize)offset, 0, (usize)byte_count);
    return clks_disk_sync_sector_range(lba, (u64)clks_disk_fat32.sectors_per_cluster);
}

static clks_bool clks_disk_find_free_cluster(u32 *out_cluster) {
    u32 start;
    u32 cluster;

    if (out_cluster == CLKS_NULL || clks_disk_fat32.valid == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    start = clks_disk_alloc_hint;
    if (start < 2U || start >= (clks_disk_fat32.cluster_count + 2U)) {
        start = 2U;
    }

    for (cluster = start; cluster < (clks_disk_fat32.cluster_count + 2U); cluster++) {
        u32 fat_value;
        if (clks_disk_fat_read_entry(cluster, &fat_value) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if (fat_value == CLKS_DISK_FAT32_FREE_CLUSTER) {
            *out_cluster = cluster;
            clks_disk_alloc_hint = cluster + 1U;
            if (clks_disk_alloc_hint >= (clks_disk_fat32.cluster_count + 2U)) {
                clks_disk_alloc_hint = 2U;
            }
            return CLKS_TRUE;
        }
    }

    for (cluster = 2U; cluster < start; cluster++) {
        u32 fat_value;
        if (clks_disk_fat_read_entry(cluster, &fat_value) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if (fat_value == CLKS_DISK_FAT32_FREE_CLUSTER) {
            *out_cluster = cluster;
            clks_disk_alloc_hint = cluster + 1U;
            if (clks_disk_alloc_hint >= (clks_disk_fat32.cluster_count + 2U)) {
                clks_disk_alloc_hint = 2U;
            }
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static void clks_disk_free_chain(u32 first_cluster) {
    u32 current;
    u32 guard = 0U;

    if (clks_disk_cluster_valid(first_cluster) == CLKS_FALSE) {
        return;
    }

    current = first_cluster;
    while (clks_disk_cluster_valid(current) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 next = CLKS_DISK_INVALID_CLUSTER;
        clks_bool is_eoc = CLKS_FALSE;

        if (clks_disk_fat_next_cluster(current, &next, &is_eoc) == CLKS_FALSE) {
            break;
        }

        (void)clks_disk_fat_write_entry(current, CLKS_DISK_FAT32_FREE_CLUSTER);

        if (is_eoc == CLKS_TRUE) {
            break;
        }

        if (clks_disk_cluster_valid(next) == CLKS_FALSE) {
            break;
        }

        current = next;
        guard++;
    }
}

static clks_bool clks_disk_alloc_cluster(u32 *out_cluster) {
    u32 cluster;

    if (out_cluster == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_find_free_cluster(&cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_fat_write_entry(cluster, CLKS_DISK_FAT32_EOC) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_zero_cluster(cluster) == CLKS_FALSE) {
        (void)clks_disk_fat_write_entry(cluster, CLKS_DISK_FAT32_FREE_CLUSTER);
        return CLKS_FALSE;
    }

    *out_cluster = cluster;
    return CLKS_TRUE;
}

static clks_bool clks_disk_alloc_chain(u32 needed_clusters, u32 *out_first_cluster) {
    u32 first = CLKS_DISK_INVALID_CLUSTER;
    u32 prev = CLKS_DISK_INVALID_CLUSTER;
    u32 i;

    if (out_first_cluster == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (needed_clusters == 0U) {
        *out_first_cluster = CLKS_DISK_INVALID_CLUSTER;
        return CLKS_TRUE;
    }

    for (i = 0U; i < needed_clusters; i++) {
        u32 current;

        if (clks_disk_alloc_cluster(&current) == CLKS_FALSE) {
            if (first != CLKS_DISK_INVALID_CLUSTER) {
                clks_disk_free_chain(first);
            }
            return CLKS_FALSE;
        }

        if (prev != CLKS_DISK_INVALID_CLUSTER) {
            if (clks_disk_fat_write_entry(prev, current) == CLKS_FALSE) {
                clks_disk_free_chain(first);
                return CLKS_FALSE;
            }
        }

        if (first == CLKS_DISK_INVALID_CLUSTER) {
            first = current;
        }
        prev = current;
    }

    *out_first_cluster = first;
    return CLKS_TRUE;
}

static clks_bool clks_disk_cluster_first_byte_offset(u32 cluster, u64 *out_offset) {
    u64 lba;

    if (out_offset == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_cluster_lba(cluster, &lba) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    *out_offset = lba * CLKS_DISK_SECTOR_SIZE;
    return CLKS_TRUE;
}

