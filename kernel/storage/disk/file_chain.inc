static clks_bool clks_disk_prepare_read_cache(u64 required_size) {
    void *new_cache;
    u64 alloc_size;

    if (required_size == 0ULL) {
        return CLKS_TRUE;
    }

    if (required_size <= clks_disk_read_cache_size && clks_disk_read_cache != CLKS_NULL) {
        return CLKS_TRUE;
    }

    if (clks_disk_read_cache != CLKS_NULL) {
        clks_kfree(clks_disk_read_cache);
        clks_disk_read_cache = CLKS_NULL;
        clks_disk_read_cache_size = 0ULL;
    }

    alloc_size = required_size;
    if (alloc_size == 0ULL) {
        alloc_size = 1ULL;
    }

    new_cache = clks_kmalloc((usize)alloc_size);
    if (new_cache == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_disk_read_cache = new_cache;
    clks_disk_read_cache_size = alloc_size;
    return CLKS_TRUE;
}

static clks_bool clks_disk_read_file_chain(u32 first_cluster, void *out_data, u64 size) {
    u32 cluster = first_cluster;
    u64 copied = 0ULL;
    u32 guard = 0U;

    if (size == 0ULL) {
        return CLKS_TRUE;
    }

    if (out_data == CLKS_NULL || clks_disk_cluster_valid(first_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    while (copied < size && clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u64 cluster_offset;
        u64 to_copy;
        u32 next_cluster;
        clks_bool is_eoc;

        if (clks_disk_cluster_first_byte_offset(cluster, &cluster_offset) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        to_copy = size - copied;
        if (to_copy > (u64)clks_disk_fat32.cluster_size_bytes) {
            to_copy = (u64)clks_disk_fat32.cluster_size_bytes;
        }

        if (clks_disk_bytes_range_valid(cluster_offset, to_copy) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        if (clks_disk_ensure_loaded_range(cluster_offset, to_copy) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        clks_memcpy((u8 *)out_data + (usize)copied, clks_disk_bytes + (usize)cluster_offset, (usize)to_copy);
        copied += to_copy;

        if (copied >= size) {
            return CLKS_TRUE;
        }

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

    return (copied == size) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_disk_write_file_chain(u32 first_cluster, const void *data, u64 size) {
    u32 cluster = first_cluster;
    u64 written = 0ULL;
    u32 guard = 0U;

    if (size == 0ULL) {
        return CLKS_TRUE;
    }

    if (data == CLKS_NULL || clks_disk_cluster_valid(first_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    while (written < size && clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u64 cluster_lba;
        u64 cluster_offset;
        u64 to_copy;
        u32 next_cluster;
        clks_bool is_eoc;

        if (clks_disk_cluster_lba(cluster, &cluster_lba) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        cluster_offset = cluster_lba * CLKS_DISK_SECTOR_SIZE;
        to_copy = size - written;
        if (to_copy > (u64)clks_disk_fat32.cluster_size_bytes) {
            to_copy = (u64)clks_disk_fat32.cluster_size_bytes;
        }

        if (clks_disk_bytes_range_valid(cluster_offset, (u64)clks_disk_fat32.cluster_size_bytes) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        clks_memset(clks_disk_bytes + (usize)cluster_offset, 0, (usize)clks_disk_fat32.cluster_size_bytes);
        clks_memcpy(clks_disk_bytes + (usize)cluster_offset, (const u8 *)data + (usize)written, (usize)to_copy);

        if (clks_disk_sync_sector_range_no_flush(cluster_lba, (u64)clks_disk_fat32.sectors_per_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        written += to_copy;
        if (written >= size) {
            return CLKS_TRUE;
        }

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

    if (written != size) {
        return CLKS_FALSE;
    }

    return clks_disk_flush_hw();
}

static clks_bool clks_disk_chain_last_cluster(u32 first_cluster, u32 *out_last_cluster) {
    u32 cluster = first_cluster;
    u32 guard = 0U;

    if (out_last_cluster == CLKS_NULL || clks_disk_cluster_valid(first_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 next_cluster = CLKS_DISK_INVALID_CLUSTER;
        clks_bool is_eoc = CLKS_FALSE;

        if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (is_eoc == CLKS_TRUE) {
            *out_last_cluster = cluster;
            return CLKS_TRUE;
        }

        if (clks_disk_cluster_valid(next_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        cluster = next_cluster;
        guard++;
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_append_file_chain(u32 first_cluster, u64 old_size, const void *data, u64 size,
                                             u32 *io_first_cluster) {
    u64 cluster_size = (u64)clks_disk_fat32.cluster_size_bytes;
    u64 appended = 0ULL;
    u32 last_cluster;

    if (io_first_cluster == CLKS_NULL || size == 0ULL) {
        return CLKS_TRUE;
    }

    if (data == CLKS_NULL || cluster_size == 0ULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_cluster_valid(first_cluster) == CLKS_FALSE) {
        u32 need_clusters;
        u64 need64 = (size + cluster_size - 1ULL) / cluster_size;

        if (need64 == 0ULL || need64 > (u64)clks_disk_fat32.cluster_count) {
            return CLKS_FALSE;
        }

        need_clusters = (u32)need64;
        if (clks_disk_alloc_chain(need_clusters, io_first_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        return clks_disk_write_file_chain(*io_first_cluster, data, size);
    }

    if (clks_disk_chain_last_cluster(first_cluster, &last_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if ((old_size % cluster_size) != 0ULL) {
        u64 cluster_lba;
        u64 cluster_offset;
        u64 within = old_size % cluster_size;
        u64 free_in_cluster = cluster_size - within;
        u64 to_copy = (size < free_in_cluster) ? size : free_in_cluster;
        u64 sync_start_lba;
        u64 sync_sector_count;

        if (clks_disk_cluster_lba(last_cluster, &cluster_lba) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        cluster_offset = (cluster_lba * CLKS_DISK_SECTOR_SIZE) + within;
        if (clks_disk_bytes_range_valid(cluster_offset, to_copy) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        clks_memcpy(clks_disk_bytes + (usize)cluster_offset, data, (usize)to_copy);
        sync_start_lba = cluster_lba + (within / CLKS_DISK_SECTOR_SIZE);
        sync_sector_count = (within + to_copy + (CLKS_DISK_SECTOR_SIZE - 1ULL)) / CLKS_DISK_SECTOR_SIZE -
                            (within / CLKS_DISK_SECTOR_SIZE);
        if (clks_disk_sync_sector_range_no_flush(sync_start_lba, sync_sector_count) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        appended += to_copy;
        if (appended >= size) {
            return clks_disk_flush_hw();
        }
    }

    while (appended < size) {
        u32 new_cluster;
        u64 cluster_lba;
        u64 cluster_offset;
        u64 to_copy = size - appended;

        if (to_copy > cluster_size) {
            to_copy = cluster_size;
        }

        if (clks_disk_alloc_cluster_no_zero(&new_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (clks_disk_fat_write_entry(last_cluster, new_cluster) == CLKS_FALSE) {
            clks_disk_free_chain(new_cluster);
            return CLKS_FALSE;
        }

        last_cluster = new_cluster;
        if (clks_disk_cluster_lba(last_cluster, &cluster_lba) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        cluster_offset = cluster_lba * CLKS_DISK_SECTOR_SIZE;
        if (clks_disk_bytes_range_valid(cluster_offset, cluster_size) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (to_copy < cluster_size) {
            clks_memset(clks_disk_bytes + (usize)cluster_offset, 0, (usize)cluster_size);
        }
        clks_memcpy(clks_disk_bytes + (usize)cluster_offset, (const u8 *)data + (usize)appended, (usize)to_copy);
        if (clks_disk_sync_sector_range_no_flush(cluster_lba, (u64)clks_disk_fat32.sectors_per_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        appended += to_copy;
    }

    return clks_disk_flush_hw();
}
