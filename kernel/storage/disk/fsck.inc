static void clks_disk_fsck_free_tail(u32 first_cluster) {
    u32 current = first_cluster;
    u32 guard = 0U;

    while (clks_disk_cluster_valid(current) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 next = CLKS_DISK_INVALID_CLUSTER;
        clks_bool is_eoc = CLKS_FALSE;

        if (clks_disk_fat_next_cluster(current, &next, &is_eoc) == CLKS_FALSE) {
            (void)clks_disk_fat_write_entry(current, CLKS_DISK_FAT32_FREE_CLUSTER);
            return;
        }

        (void)clks_disk_fat_write_entry(current, CLKS_DISK_FAT32_FREE_CLUSTER);
        if (is_eoc == CLKS_TRUE || clks_disk_cluster_valid(next) == CLKS_FALSE) {
            return;
        }

        current = next;
        guard++;
    }
}

static clks_bool clks_disk_fsck_mark_chain(u32 first_cluster, u64 expected_size, clks_bool is_dir, u8 *marks,
                                           u64 flags, struct clks_disk_fsck_result *result) {
    u32 current = first_cluster;
    u32 prev = CLKS_DISK_INVALID_CLUSTER;
    u64 chain_clusters = 0ULL;
    u64 needed_clusters = 0ULL;
    u32 guard = 0U;

    if (result == CLKS_NULL || marks == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (is_dir == CLKS_FALSE && expected_size > 0ULL) {
        needed_clusters =
            (expected_size + (u64)clks_disk_fat32.cluster_size_bytes - 1ULL) / (u64)clks_disk_fat32.cluster_size_bytes;
    }

    if (first_cluster == CLKS_DISK_INVALID_CLUSTER || first_cluster == 0U) {
        if (expected_size > 0ULL && is_dir == CLKS_FALSE) {
            result->size_mismatches++;
        }
        return CLKS_TRUE;
    }

    if (clks_disk_cluster_valid(first_cluster) == CLKS_FALSE) {
        result->bad_entries++;
        return CLKS_TRUE;
    }

    while (clks_disk_cluster_valid(current) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 next = CLKS_DISK_INVALID_CLUSTER;
        clks_bool is_eoc = CLKS_FALSE;
        u64 mark_index = (u64)(current - 2U);

        if (marks[mark_index] != 0U) {
            result->loops++;
            if ((flags & CLKS_DISK_FSCK_FLAG_FIX) != 0ULL && prev != CLKS_DISK_INVALID_CLUSTER) {
                if (clks_disk_fat_write_entry(prev, CLKS_DISK_FAT32_EOC) == CLKS_TRUE) {
                    result->fixed_entries++;
                }
            }
            break;
        }

        marks[mark_index] = 1U;
        chain_clusters++;

        if (clks_disk_fat_next_cluster(current, &next, &is_eoc) == CLKS_FALSE) {
            result->bad_entries++;
            if ((flags & CLKS_DISK_FSCK_FLAG_FIX) != 0ULL) {
                if (clks_disk_fat_write_entry(current, CLKS_DISK_FAT32_EOC) == CLKS_TRUE) {
                    result->fixed_entries++;
                }
            }
            break;
        }

        if (is_dir == CLKS_FALSE && needed_clusters > 0ULL && chain_clusters == needed_clusters &&
            is_eoc == CLKS_FALSE) {
            result->size_mismatches++;
            if ((flags & CLKS_DISK_FSCK_FLAG_FIX) != 0ULL) {
                if (clks_disk_fat_write_entry(current, CLKS_DISK_FAT32_EOC) == CLKS_TRUE) {
                    result->fixed_entries++;
                    clks_disk_fsck_free_tail(next);
                }
            }
            break;
        }

        if (is_eoc == CLKS_TRUE) {
            break;
        }

        if (clks_disk_cluster_valid(next) == CLKS_FALSE) {
            result->bad_entries++;
            if ((flags & CLKS_DISK_FSCK_FLAG_FIX) != 0ULL) {
                if (clks_disk_fat_write_entry(current, CLKS_DISK_FAT32_EOC) == CLKS_TRUE) {
                    result->fixed_entries++;
                }
            }
            break;
        }

        prev = current;
        current = next;
        guard++;
    }

    if (guard >= clks_disk_fat32.cluster_count) {
        result->loops++;
    }

    if (is_dir == CLKS_FALSE && expected_size > 0ULL) {
        if (needed_clusters == 0ULL || chain_clusters < needed_clusters) {
            result->size_mismatches++;
        }
    }

    return CLKS_TRUE;
}

static clks_bool clks_disk_fsck_scan_directory(u32 dir_cluster, u8 *marks, u64 flags,
                                               struct clks_disk_fsck_result *result, u32 depth) {
    u32 cluster = dir_cluster;
    u32 guard = 0U;

    if (marks == CLKS_NULL || result == CLKS_NULL || depth > 32U) {
        return CLKS_FALSE;
    }

    if (clks_disk_fsck_mark_chain(dir_cluster, 0ULL, CLKS_TRUE, marks, flags, result) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);
            u8 attr;
            u32 first_cluster;
            u32 size;

            if (entry == CLKS_NULL) {
                result->bad_entries++;
                return CLKS_TRUE;
            }

            if (entry[0] == 0x00U || entry[0] == 0xE5U) {
                continue;
            }

            attr = entry[11U];
            if (attr == CLKS_DISK_ATTR_LFN || (attr & CLKS_DISK_ATTR_VOLUME_ID) != 0U) {
                continue;
            }

            first_cluster = clks_disk_dirent_first_cluster(entry);
            size = clks_disk_dirent_size(entry);

            if ((attr & CLKS_DISK_ATTR_DIRECTORY) != 0U) {
                if (clks_disk_dirent_is_dot_like(entry) == CLKS_TRUE) {
                    continue;
                }

                if (first_cluster == 0U || clks_disk_cluster_valid(first_cluster) == CLKS_FALSE) {
                    result->bad_entries++;
                    continue;
                }

                if (clks_disk_fsck_scan_directory(first_cluster, marks, flags, result, depth + 1U) == CLKS_FALSE) {
                    return CLKS_FALSE;
                }
            } else if (first_cluster != 0U || size > 0U) {
                if (size == 0U && first_cluster != 0U) {
                    result->size_mismatches++;
                    if ((flags & CLKS_DISK_FSCK_FLAG_FIX) != 0ULL) {
                        u64 entry_lba = 0ULL;

                        clks_disk_fsck_free_tail(first_cluster);
                        clks_disk_dirent_set_first_cluster(entry, CLKS_DISK_INVALID_CLUSTER);
                        if (clks_disk_dir_entry_ptr(cluster, entry_index, &entry_lba) != CLKS_NULL &&
                            clks_disk_sync_sector_range(entry_lba, 1ULL) == CLKS_TRUE) {
                            result->fixed_entries++;
                        }
                    }
                    continue;
                }

                if (clks_disk_fsck_mark_chain(first_cluster, (u64)size, CLKS_FALSE, marks, flags, result) == CLKS_FALSE) {
                    return CLKS_FALSE;
                }
            }
        }

        {
            u32 next_cluster = CLKS_DISK_INVALID_CLUSTER;
            clks_bool is_eoc = CLKS_FALSE;

            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                result->bad_entries++;
                return CLKS_TRUE;
            }

            if (is_eoc == CLKS_TRUE) {
                return CLKS_TRUE;
            }

            if (clks_disk_cluster_valid(next_cluster) == CLKS_FALSE) {
                result->bad_entries++;
                if ((flags & CLKS_DISK_FSCK_FLAG_FIX) != 0ULL) {
                    if (clks_disk_fat_write_entry(cluster, CLKS_DISK_FAT32_EOC) == CLKS_TRUE) {
                        result->fixed_entries++;
                    }
                }
                return CLKS_TRUE;
            }

            cluster = next_cluster;
            guard++;
        }
    }

    result->loops++;
    return CLKS_TRUE;
}

clks_bool clks_disk_fsck_fat32(u64 flags, struct clks_disk_fsck_result *out_result) {
    u8 *marks;
    u32 cluster;

    if (out_result == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_memset(out_result, 0, sizeof(*out_result));

    if (clks_disk_ready == CLKS_FALSE || clks_disk_formatted == CLKS_FALSE || clks_disk_fat32.valid == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    marks = (u8 *)clks_kmalloc((usize)clks_disk_fat32.cluster_count);
    if (marks == CLKS_NULL) {
        return CLKS_FALSE;
    }
    clks_memset(marks, 0, (usize)clks_disk_fat32.cluster_count);

    out_result->checked_clusters = (u64)clks_disk_fat32.cluster_count;

    {
        u32 fat0 = 0U;
        u32 fat1 = 0U;
        if (clks_disk_fat_read_entry(0U, &fat0) == CLKS_FALSE || clks_disk_fat_read_entry(1U, &fat1) == CLKS_FALSE ||
            fat0 == CLKS_DISK_FAT32_FREE_CLUSTER || fat1 == CLKS_DISK_FAT32_FREE_CLUSTER) {
            out_result->bad_entries++;
        }
    }

    if (clks_disk_fsck_scan_directory(clks_disk_fat32.root_cluster, marks, flags, out_result, 0U) == CLKS_FALSE) {
        clks_kfree(marks);
        return CLKS_FALSE;
    }

    for (cluster = 2U; cluster < clks_disk_fat32.cluster_count + 2U; cluster++) {
        u32 value = 0U;
        u64 mark_index = (u64)(cluster - 2U);

        if (clks_disk_fat_read_entry(cluster, &value) == CLKS_FALSE) {
            out_result->bad_entries++;
            continue;
        }

        if (value == CLKS_DISK_FAT32_FREE_CLUSTER) {
            out_result->free_clusters++;
            continue;
        }

        out_result->used_clusters++;

        if (marks[mark_index] == 0U) {
            out_result->orphan_clusters++;
            if ((flags & CLKS_DISK_FSCK_FLAG_FIX) != 0ULL) {
                (void)clks_disk_fat_write_entry(cluster, CLKS_DISK_FAT32_FREE_CLUSTER);
                out_result->fixed_orphans++;
            }
        }
    }

    if ((flags & CLKS_DISK_FSCK_FLAG_FIX) != 0ULL) {
        (void)clks_disk_flush_hw();
    }

    out_result->status = (out_result->bad_entries == 0ULL && out_result->loops == 0ULL &&
                          out_result->size_mismatches == 0ULL && out_result->orphan_clusters == 0ULL)
                             ? 0ULL
                             : 1ULL;

    clks_kfree(marks);
    return CLKS_TRUE;
}
