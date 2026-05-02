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

        if (clks_disk_sync_sector_range(cluster_lba, (u64)clks_disk_fat32.sectors_per_cluster) == CLKS_FALSE) {
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

    return (written == size) ? CLKS_TRUE : CLKS_FALSE;
}

