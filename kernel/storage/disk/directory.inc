static u32 clks_disk_dirent_first_cluster(const u8 *entry) {
    u32 high;
    u32 low;
    if (entry == CLKS_NULL) {
        return CLKS_DISK_INVALID_CLUSTER;
    }
    high = (u32)clks_disk_read_u16(entry + 20U);
    low = (u32)clks_disk_read_u16(entry + 26U);
    return (high << 16U) | low;
}

static void clks_disk_dirent_set_first_cluster(u8 *entry, u32 cluster) {
    if (entry == CLKS_NULL) {
        return;
    }
    clks_disk_write_u16(entry + 20U, (u16)((cluster >> 16U) & 0xFFFFU));
    clks_disk_write_u16(entry + 26U, (u16)(cluster & 0xFFFFU));
}

static u32 clks_disk_dirent_size(const u8 *entry) {
    if (entry == CLKS_NULL) {
        return 0U;
    }
    return clks_disk_read_u32(entry + 28U);
}

static clks_bool clks_disk_dirent_is_dot_like(const u8 *entry) {
    if (entry == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (entry[0] == '.' && entry[1] == ' ') {
        return CLKS_TRUE;
    }

    if (entry[0] == '.' && entry[1] == '.' && entry[2] == ' ') {
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_dirent_is_visible(const u8 *entry) {
    u8 attr;

    if (entry == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (entry[0] == 0x00U || entry[0] == 0xE5U) {
        return CLKS_FALSE;
    }

    attr = entry[11U];
    if (attr == CLKS_DISK_ATTR_LFN) {
        return CLKS_FALSE;
    }
    if ((attr & CLKS_DISK_ATTR_VOLUME_ID) != 0U) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static char clks_disk_ascii_upper(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - ('a' - 'A'));
    }
    return ch;
}

static clks_bool clks_disk_sfn_char_valid(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return CLKS_TRUE;
    }
    if (ch >= '0' && ch <= '9') {
        return CLKS_TRUE;
    }

    switch (ch) {
    case '$':
    case '%':
    case '\'':
    case '-':
    case '_':
    case '@':
    case '~':
    case '`':
    case '!':
    case '(':
    case ')':
    case '{':
    case '}':
    case '^':
    case '#':
    case '&':
    case '+':
    case ',':
    case ';':
    case '=':
        return CLKS_TRUE;
    default:
        break;
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_component_to_sfn(const char *component, u8 out_name[CLKS_DISK_DIRENT_NAME_LEN]) {
    usize i = 0U;
    usize base_len = 0U;
    usize ext_len = 0U;
    clks_bool in_ext = CLKS_FALSE;

    if (component == CLKS_NULL || out_name == CLKS_NULL || component[0] == '\0') {
        return CLKS_FALSE;
    }

    for (i = 0U; i < CLKS_DISK_DIRENT_NAME_LEN; i++) {
        out_name[i] = ' ';
    }

    i = 0U;
    while (component[i] != '\0') {
        char ch = component[i];

        if (ch == '.') {
            if (in_ext == CLKS_TRUE || base_len == 0U) {
                return CLKS_FALSE;
            }
            in_ext = CLKS_TRUE;
            i++;
            continue;
        }

        ch = clks_disk_ascii_upper(ch);
        if (clks_disk_sfn_char_valid(ch) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (in_ext == CLKS_FALSE) {
            if (base_len >= 8U) {
                return CLKS_FALSE;
            }
            out_name[base_len++] = (u8)ch;
        } else {
            if (ext_len >= 3U) {
                return CLKS_FALSE;
            }
            out_name[8U + ext_len++] = (u8)ch;
        }

        i++;
    }

    if (base_len == 0U) {
        return CLKS_FALSE;
    }

    if (in_ext == CLKS_TRUE && ext_len == 0U) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static void clks_disk_sfn_to_text(const u8 *entry, char *out_name, usize out_size) {
    usize base_end = 8U;
    usize ext_end = 3U;
    usize pos = 0U;
    usize i;

    if (out_name == CLKS_NULL || out_size == 0U) {
        return;
    }

    out_name[0] = '\0';
    if (entry == CLKS_NULL) {
        return;
    }

    while (base_end > 0U && entry[base_end - 1U] == ' ') {
        base_end--;
    }
    while (ext_end > 0U && entry[8U + ext_end - 1U] == ' ') {
        ext_end--;
    }

    for (i = 0U; i < base_end && pos + 1U < out_size; i++) {
        out_name[pos++] = (char)entry[i];
    }

    if (ext_end > 0U && pos + 1U < out_size) {
        out_name[pos++] = '.';
        for (i = 0U; i < ext_end && pos + 1U < out_size; i++) {
            out_name[pos++] = (char)entry[8U + i];
        }
    }

    out_name[pos] = '\0';
}

static clks_bool clks_disk_dirent_name_matches(const u8 *entry, const u8 sfn[CLKS_DISK_DIRENT_NAME_LEN]) {
    usize i;

    if (entry == CLKS_NULL || sfn == CLKS_NULL) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < CLKS_DISK_DIRENT_NAME_LEN; i++) {
        if (entry[i] != sfn[i]) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

static u8 *clks_disk_dir_entry_ptr(u32 cluster, u32 entry_index, u64 *out_lba) {
    u64 cluster_lba;
    u64 lba;
    u32 entries_per_sector;
    u32 sector_in_cluster;
    u32 entry_in_sector;
    u64 entry_offset;
    u64 disk_offset;

    if (clks_disk_cluster_lba(cluster, &cluster_lba) == CLKS_FALSE) {
        return CLKS_NULL;
    }

    if (entry_index >= clks_disk_fat32.entries_per_cluster) {
        return CLKS_NULL;
    }

    entries_per_sector = (u32)CLKS_DISK_SECTOR_SIZE / CLKS_DISK_DIRENT_SIZE;
    sector_in_cluster = entry_index / entries_per_sector;
    entry_in_sector = entry_index % entries_per_sector;

    lba = cluster_lba + (u64)sector_in_cluster;
    entry_offset = (u64)entry_in_sector * CLKS_DISK_DIRENT_SIZE;
    disk_offset = lba * CLKS_DISK_SECTOR_SIZE + entry_offset;

    if (clks_disk_bytes_range_valid(disk_offset, CLKS_DISK_DIRENT_SIZE) == CLKS_FALSE) {
        return CLKS_NULL;
    }
    if (clks_disk_ensure_loaded_range(disk_offset, CLKS_DISK_DIRENT_SIZE) == CLKS_FALSE) {
        return CLKS_NULL;
    }

    if (out_lba != CLKS_NULL) {
        *out_lba = lba;
    }

    return clks_disk_bytes + (usize)disk_offset;
}

static clks_bool clks_disk_dir_find_entry(u32 dir_cluster, const u8 target_sfn[CLKS_DISK_DIRENT_NAME_LEN],
                                          clks_bool *out_found, struct clks_disk_dir_slot *out_slot,
                                          struct clks_disk_dir_info *out_info) {
    u32 cluster = dir_cluster;
    u32 guard = 0U;

    if (out_found == CLKS_NULL || out_slot == CLKS_NULL || out_info == CLKS_NULL || target_sfn == CLKS_NULL) {
        return CLKS_FALSE;
    }

    *out_found = CLKS_FALSE;
    clks_memset(out_slot, 0, sizeof(*out_slot));
    clks_memset(out_info, 0, sizeof(*out_info));

    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);
            u8 attr;

            if (entry == CLKS_NULL) {
                return CLKS_FALSE;
            }

            if (entry[0] == 0x00U) {
                return CLKS_TRUE;
            }

            if (entry[0] == 0xE5U) {
                continue;
            }

            attr = entry[11U];
            if (attr == CLKS_DISK_ATTR_LFN || (attr & CLKS_DISK_ATTR_VOLUME_ID) != 0U) {
                continue;
            }

            if (clks_disk_dirent_name_matches(entry, target_sfn) == CLKS_TRUE) {
                *out_found = CLKS_TRUE;
                out_slot->cluster = cluster;
                out_slot->entry_index = entry_index;
                out_info->attr = attr;
                out_info->first_cluster = clks_disk_dirent_first_cluster(entry);
                out_info->size = clks_disk_dirent_size(entry);
                return CLKS_TRUE;
            }
        }

        {
            u32 next_cluster = CLKS_DISK_INVALID_CLUSTER;
            clks_bool is_eoc = CLKS_FALSE;
            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                return CLKS_FALSE;
            }
            if (is_eoc == CLKS_TRUE) {
                return CLKS_TRUE;
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

static clks_bool clks_disk_dir_find_free_slot(u32 dir_cluster, struct clks_disk_dir_slot *out_slot) {
    u32 cluster = dir_cluster;
    u32 guard = 0U;
    clks_bool deleted_found = CLKS_FALSE;
    struct clks_disk_dir_slot deleted_slot;

    if (out_slot == CLKS_NULL || clks_disk_cluster_valid(dir_cluster) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_memset(out_slot, 0, sizeof(*out_slot));
    clks_memset(&deleted_slot, 0, sizeof(deleted_slot));

    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);

            if (entry == CLKS_NULL) {
                return CLKS_FALSE;
            }

            if (entry[0] == 0xE5U && deleted_found == CLKS_FALSE) {
                deleted_found = CLKS_TRUE;
                deleted_slot.cluster = cluster;
                deleted_slot.entry_index = entry_index;
            }

            if (entry[0] == 0x00U) {
                if (deleted_found == CLKS_TRUE) {
                    *out_slot = deleted_slot;
                } else {
                    out_slot->cluster = cluster;
                    out_slot->entry_index = entry_index;
                }
                return CLKS_TRUE;
            }
        }

        {
            u32 next_cluster = CLKS_DISK_INVALID_CLUSTER;
            clks_bool is_eoc = CLKS_FALSE;

            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                return CLKS_FALSE;
            }

            if (is_eoc == CLKS_TRUE) {
                if (deleted_found == CLKS_TRUE) {
                    *out_slot = deleted_slot;
                    return CLKS_TRUE;
                }

                {
                    u32 new_cluster;
                    if (clks_disk_alloc_cluster(&new_cluster) == CLKS_FALSE) {
                        return CLKS_FALSE;
                    }

                    if (clks_disk_fat_write_entry(cluster, new_cluster) == CLKS_FALSE) {
                        clks_disk_free_chain(new_cluster);
                        return CLKS_FALSE;
                    }

                    out_slot->cluster = new_cluster;
                    out_slot->entry_index = 0U;
                    return CLKS_TRUE;
                }
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

static clks_bool clks_disk_write_dir_entry(const struct clks_disk_dir_slot *slot, const u8 *entry_data) {
    u64 lba = 0ULL;
    u8 *entry;

    if (slot == CLKS_NULL || entry_data == CLKS_NULL) {
        return CLKS_FALSE;
    }

    entry = clks_disk_dir_entry_ptr(slot->cluster, slot->entry_index, &lba);
    if (entry == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_memcpy(entry, entry_data, CLKS_DISK_DIRENT_SIZE);
    return clks_disk_sync_sector_range(lba, 1ULL);
}

static void clks_disk_build_dir_entry(u8 *entry_out, const u8 name[CLKS_DISK_DIRENT_NAME_LEN], u8 attr,
                                      u32 first_cluster, u32 size) {
    if (entry_out == CLKS_NULL || name == CLKS_NULL) {
        return;
    }

    clks_memset(entry_out, 0, CLKS_DISK_DIRENT_SIZE);
    clks_memcpy(entry_out, name, CLKS_DISK_DIRENT_NAME_LEN);
    entry_out[11U] = attr;
    clks_disk_dirent_set_first_cluster(entry_out, first_cluster);
    clks_disk_write_u32(entry_out + 28U, size);
}

static clks_bool clks_disk_init_directory_cluster(u32 cluster, u32 parent_cluster) {
    static const u8 dot_name[CLKS_DISK_DIRENT_NAME_LEN] = {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    static const u8 dotdot_name[CLKS_DISK_DIRENT_NAME_LEN] = {'.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    u64 cluster_offset;
    u64 byte_count;
    u8 dot_entry[CLKS_DISK_DIRENT_SIZE];
    u8 dotdot_entry[CLKS_DISK_DIRENT_SIZE];
    u64 cluster_lba;

    if (clks_disk_cluster_first_byte_offset(cluster, &cluster_offset) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    byte_count = (u64)clks_disk_fat32.cluster_size_bytes;
    if (clks_disk_bytes_range_valid(cluster_offset, byte_count) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_cluster_lba(cluster, &cluster_lba) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_memset(clks_disk_bytes + (usize)cluster_offset, 0, (usize)byte_count);
    clks_disk_build_dir_entry(dot_entry, dot_name, CLKS_DISK_ATTR_DIRECTORY, cluster, 0U);
    clks_disk_build_dir_entry(dotdot_entry, dotdot_name, CLKS_DISK_ATTR_DIRECTORY, parent_cluster, 0U);

    clks_memcpy(clks_disk_bytes + (usize)cluster_offset + 0U * CLKS_DISK_DIRENT_SIZE, dot_entry, CLKS_DISK_DIRENT_SIZE);
    clks_memcpy(clks_disk_bytes + (usize)cluster_offset + 1U * CLKS_DISK_DIRENT_SIZE, dotdot_entry,
                CLKS_DISK_DIRENT_SIZE);

    return clks_disk_sync_sector_range(cluster_lba, (u64)clks_disk_fat32.sectors_per_cluster);
}

static clks_bool clks_disk_next_component(const char *path, usize *io_pos, char *out_component, usize out_size,
                                          clks_bool *out_last) {
    usize start;
    usize end;
    usize len;

    if (path == CLKS_NULL || io_pos == CLKS_NULL || out_component == CLKS_NULL || out_size == 0U ||
        out_last == CLKS_NULL) {
        return CLKS_FALSE;
    }

    start = *io_pos;
    if (path[start] == '\0') {
        return CLKS_FALSE;
    }

    end = start;
    while (path[end] != '\0' && path[end] != '/') {
        end++;
    }

    len = end - start;
    if (len == 0U || len + 1U > out_size) {
        return CLKS_FALSE;
    }

    clks_memcpy(out_component, path + start, len);
    out_component[len] = '\0';

    if (path[end] == '/') {
        *io_pos = end + 1U;
    } else {
        *io_pos = end;
    }

    *out_last = (path[*io_pos] == '\0') ? CLKS_TRUE : CLKS_FALSE;
    return CLKS_TRUE;
}

static clks_bool clks_disk_lookup_relative_path(const char *relative_path, struct clks_disk_path_lookup *out_lookup) {
    u32 current_dir;
    usize pos = 0U;

    if (relative_path == CLKS_NULL || out_lookup == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_memset(out_lookup, 0, sizeof(*out_lookup));

    if (relative_path[0] == '\0') {
        out_lookup->found = CLKS_TRUE;
        out_lookup->parent_cluster = clks_disk_fat32.root_cluster;
        out_lookup->info.attr = CLKS_DISK_ATTR_DIRECTORY;
        out_lookup->info.first_cluster = clks_disk_fat32.root_cluster;
        out_lookup->info.size = 0U;
        return CLKS_TRUE;
    }

    current_dir = clks_disk_fat32.root_cluster;

    while (relative_path[pos] != '\0') {
        char component[CLKS_DISK_PATH_MAX];
        u8 sfn[CLKS_DISK_DIRENT_NAME_LEN];
        clks_bool last = CLKS_FALSE;
        clks_bool found = CLKS_FALSE;
        struct clks_disk_dir_slot slot;
        struct clks_disk_dir_info info;

        if (clks_disk_next_component(relative_path, &pos, component, sizeof(component), &last) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (clks_disk_component_to_sfn(component, sfn) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (clks_disk_dir_find_entry(current_dir, sfn, &found, &slot, &info) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if (last == CLKS_TRUE) {
            out_lookup->found = found;
            out_lookup->parent_cluster = current_dir;
            clks_memcpy(out_lookup->sfn, sfn, CLKS_DISK_DIRENT_NAME_LEN);
            if (found == CLKS_TRUE) {
                out_lookup->slot = slot;
                out_lookup->info = info;
            }
            return CLKS_TRUE;
        }

        if (found == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        if ((info.attr & CLKS_DISK_ATTR_DIRECTORY) == 0U) {
            return CLKS_FALSE;
        }

        if (clks_disk_cluster_valid(info.first_cluster) == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        current_dir = info.first_cluster;
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_dir_is_empty(u32 dir_cluster) {
    u32 cluster = dir_cluster;
    u32 guard = 0U;

    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);

            if (entry == CLKS_NULL) {
                return CLKS_FALSE;
            }

            if (entry[0] == 0x00U) {
                return CLKS_TRUE;
            }

            if (clks_disk_dirent_is_visible(entry) == CLKS_FALSE) {
                continue;
            }

            if (clks_disk_dirent_is_dot_like(entry) == CLKS_TRUE) {
                continue;
            }

            return CLKS_FALSE;
        }

        {
            u32 next_cluster;
            clks_bool is_eoc;
            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                return CLKS_FALSE;
            }
            if (is_eoc == CLKS_TRUE) {
                return CLKS_TRUE;
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

