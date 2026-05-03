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

static char clks_disk_ascii_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch + ('a' - 'A'));
    }
    return ch;
}

static usize clks_disk_component_len(const char *component) {
    usize len = 0U;

    if (component == CLKS_NULL) {
        return 0U;
    }

    while (component[len] != '\0') {
        len++;
    }

    return len;
}

static clks_bool clks_disk_text_equals_casefold(const char *left, const char *right) {
    usize i = 0U;

    if (left == CLKS_NULL || right == CLKS_NULL) {
        return CLKS_FALSE;
    }

    while (left[i] != '\0' && right[i] != '\0') {
        if (clks_disk_ascii_upper(left[i]) != clks_disk_ascii_upper(right[i])) {
            return CLKS_FALSE;
        }
        i++;
    }

    return (left[i] == '\0' && right[i] == '\0') ? CLKS_TRUE : CLKS_FALSE;
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
        out_name[pos++] = clks_disk_ascii_lower((char)entry[i]);
    }

    if (ext_end > 0U && pos + 1U < out_size) {
        out_name[pos++] = '.';
        for (i = 0U; i < ext_end && pos + 1U < out_size; i++) {
            out_name[pos++] = clks_disk_ascii_lower((char)entry[8U + i]);
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

static u8 clks_disk_lfn_checksum(const u8 sfn[CLKS_DISK_DIRENT_NAME_LEN]) {
    u8 sum = 0U;
    usize i;

    if (sfn == CLKS_NULL) {
        return 0U;
    }

    for (i = 0U; i < CLKS_DISK_DIRENT_NAME_LEN; i++) {
        sum = (u8)(((sum & 1U) != 0U ? 0x80U : 0U) + (sum >> 1U) + sfn[i]);
    }

    return sum;
}

static void clks_disk_lfn_put_utf16(u8 *entry, usize ordinal_index, u16 value) {
    static const u8 offsets[13] = {1U, 3U, 5U, 7U, 9U, 14U, 16U, 18U, 20U, 22U, 24U, 28U, 30U};
    u8 off;

    if (entry == CLKS_NULL || ordinal_index >= 13U) {
        return;
    }

    off = offsets[ordinal_index];
    entry[off] = (u8)(value & 0xFFU);
    entry[off + 1U] = (u8)((value >> 8U) & 0xFFU);
}

static u16 clks_disk_lfn_get_utf16(const u8 *entry, usize ordinal_index) {
    static const u8 offsets[13] = {1U, 3U, 5U, 7U, 9U, 14U, 16U, 18U, 20U, 22U, 24U, 28U, 30U};
    u8 off;

    if (entry == CLKS_NULL || ordinal_index >= 13U) {
        return 0U;
    }

    off = offsets[ordinal_index];
    return (u16)entry[off] | ((u16)entry[off + 1U] << 8U);
}

static void clks_disk_build_lfn_entry(u8 *entry_out, const char *name, usize name_len, u8 ordinal, u8 checksum) {
    usize i;
    usize start;

    if (entry_out == CLKS_NULL || name == CLKS_NULL || ordinal == 0U) {
        return;
    }

    clks_memset(entry_out, 0xFF, CLKS_DISK_DIRENT_SIZE);
    entry_out[0U] = ordinal;
    entry_out[11U] = CLKS_DISK_ATTR_LFN;
    entry_out[12U] = 0U;
    entry_out[13U] = checksum;
    entry_out[26U] = 0U;
    entry_out[27U] = 0U;

    start = ((usize)(ordinal & 0x1FU) - 1U) * 13U;
    for (i = 0U; i < 13U; i++) {
        usize src = start + i;
        u16 value;

        if (src < name_len) {
            value = (u16)(u8)name[src];
        } else if (src == name_len) {
            value = 0U;
        } else {
            value = 0xFFFFU;
        }

        clks_disk_lfn_put_utf16(entry_out, i, value);
    }
}

static clks_bool clks_disk_lfn_prepend_part(const u8 *entry, char *out_name, usize out_size) {
    char tmp[CLKS_DISK_PATH_MAX];
    usize part_len = 0U;
    usize old_len;
    usize i;

    if (entry == CLKS_NULL || out_name == CLKS_NULL || out_size == 0U) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < 13U; i++) {
        u16 value = clks_disk_lfn_get_utf16(entry, i);

        if (value == 0U || value == 0xFFFFU) {
            break;
        }

        if (value > 0x7FU || part_len + 1U >= sizeof(tmp)) {
            return CLKS_FALSE;
        }

        tmp[part_len++] = (char)value;
    }
    tmp[part_len] = '\0';

    old_len = clks_strlen(out_name);
    if (part_len + old_len + 1U > out_size) {
        return CLKS_FALSE;
    }

    for (i = old_len + 1U; i > 0U; i--) {
        out_name[part_len + i - 1U] = out_name[i - 1U];
    }
    for (i = 0U; i < part_len; i++) {
        out_name[i] = tmp[i];
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

static clks_bool clks_disk_dir_find_entry_by_component(u32 dir_cluster, const char *component,
                                                       clks_bool *out_found, struct clks_disk_dir_slot *out_slot,
                                                       struct clks_disk_dir_info *out_info,
                                                       u8 out_sfn[CLKS_DISK_DIRENT_NAME_LEN]) {
    u32 cluster = dir_cluster;
    u32 guard = 0U;
    clks_bool have_target_sfn;
    u8 target_sfn[CLKS_DISK_DIRENT_NAME_LEN];
    char lfn_name[CLKS_DISK_PATH_MAX];
    u8 lfn_checksum = 0U;
    u8 lfn_expected_seq = 0U;
    clks_bool lfn_active = CLKS_FALSE;

    if (out_found == CLKS_NULL || out_slot == CLKS_NULL || out_info == CLKS_NULL || component == CLKS_NULL) {
        return CLKS_FALSE;
    }

    *out_found = CLKS_FALSE;
    clks_memset(out_slot, 0, sizeof(*out_slot));
    clks_memset(out_info, 0, sizeof(*out_info));
    if (out_sfn != CLKS_NULL) {
        clks_memset(out_sfn, ' ', CLKS_DISK_DIRENT_NAME_LEN);
    }

    have_target_sfn = clks_disk_component_to_sfn(component, target_sfn);
    lfn_name[0] = '\0';

    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);
            u8 attr;

            if (entry == CLKS_NULL) {
                return CLKS_FALSE;
            }

            if (entry[0] == 0x00U) {
                lfn_active = CLKS_FALSE;
                lfn_name[0] = '\0';
                continue;
            }

            if (entry[0] == 0xE5U) {
                lfn_active = CLKS_FALSE;
                lfn_name[0] = '\0';
                continue;
            }

            attr = entry[11U];
            if (attr == CLKS_DISK_ATTR_LFN) {
                u8 order = (u8)(entry[0U] & 0x1FU);
                u8 checksum = entry[13U];

                if ((entry[0U] & 0x40U) != 0U) {
                    lfn_active = CLKS_TRUE;
                    lfn_expected_seq = order;
                    lfn_checksum = checksum;
                    lfn_name[0] = '\0';
                } else if (lfn_active == CLKS_FALSE || order + 1U != lfn_expected_seq || checksum != lfn_checksum) {
                    lfn_active = CLKS_FALSE;
                    lfn_name[0] = '\0';
                    continue;
                }

                if (order == 0U || clks_disk_lfn_prepend_part(entry, lfn_name, sizeof(lfn_name)) == CLKS_FALSE) {
                    lfn_active = CLKS_FALSE;
                    lfn_name[0] = '\0';
                    continue;
                }

                lfn_expected_seq = order;
                continue;
            }

            if ((attr & CLKS_DISK_ATTR_VOLUME_ID) != 0U) {
                lfn_active = CLKS_FALSE;
                lfn_name[0] = '\0';
                continue;
            }

            if ((lfn_active == CLKS_TRUE && lfn_checksum == clks_disk_lfn_checksum(entry) &&
                 clks_disk_text_equals_casefold(lfn_name, component) == CLKS_TRUE) ||
                (have_target_sfn == CLKS_TRUE && clks_disk_dirent_name_matches(entry, target_sfn) == CLKS_TRUE)) {
                *out_found = CLKS_TRUE;
                out_slot->cluster = cluster;
                out_slot->entry_index = entry_index;
                out_info->attr = attr;
                out_info->first_cluster = clks_disk_dirent_first_cluster(entry);
                out_info->size = clks_disk_dirent_size(entry);
                if (out_sfn != CLKS_NULL) {
                    clks_memcpy(out_sfn, entry, CLKS_DISK_DIRENT_NAME_LEN);
                }
                return CLKS_TRUE;
            }

            lfn_active = CLKS_FALSE;
            lfn_name[0] = '\0';
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

static clks_bool clks_disk_dir_find_free_slots(u32 dir_cluster, u32 needed, struct clks_disk_dir_slot *out_slot) {
    u32 cluster = dir_cluster;
    u32 guard = 0U;

    if (out_slot == CLKS_NULL || clks_disk_cluster_valid(dir_cluster) == CLKS_FALSE || needed == 0U) {
        return CLKS_FALSE;
    }

    clks_memset(out_slot, 0, sizeof(*out_slot));

    while (clks_disk_cluster_valid(cluster) == CLKS_TRUE && guard < clks_disk_fat32.cluster_count) {
        u32 entry_index;
        u32 run_start = 0U;
        u32 run_len = 0U;

        for (entry_index = 0U; entry_index < clks_disk_fat32.entries_per_cluster; entry_index++) {
            u8 *entry = clks_disk_dir_entry_ptr(cluster, entry_index, CLKS_NULL);
            clks_bool free_entry;

            if (entry == CLKS_NULL) {
                return CLKS_FALSE;
            }

            free_entry = (entry[0] == 0x00U || entry[0] == 0xE5U) ? CLKS_TRUE : CLKS_FALSE;
            if (free_entry == CLKS_TRUE) {
                if (run_len == 0U) {
                    run_start = entry_index;
                }
                run_len++;
                if (run_len >= needed) {
                    out_slot->cluster = cluster;
                    out_slot->entry_index = run_start;
                    return CLKS_TRUE;
                }
            } else {
                run_len = 0U;
            }
        }

        {
            u32 next_cluster = CLKS_DISK_INVALID_CLUSTER;
            clks_bool is_eoc = CLKS_FALSE;

            if (clks_disk_fat_next_cluster(cluster, &next_cluster, &is_eoc) == CLKS_FALSE) {
                return CLKS_FALSE;
            }

            if (is_eoc == CLKS_TRUE) {
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

static clks_bool clks_disk_write_dir_entry_at(u32 cluster, u32 entry_index, const u8 *entry_data) {
    struct clks_disk_dir_slot slot;

    slot.cluster = cluster;
    slot.entry_index = entry_index;
    return clks_disk_write_dir_entry(&slot, entry_data);
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

static clks_bool clks_disk_component_generate_sfn(const char *component, u8 out_name[CLKS_DISK_DIRENT_NAME_LEN]) {
    char clean[8];
    char ext[3];
    usize clean_len = 0U;
    usize ext_len = 0U;
    usize i;
    usize dot_pos = (usize)-1;

    if (component == CLKS_NULL || out_name == CLKS_NULL || component[0] == '\0') {
        return CLKS_FALSE;
    }

    if (clks_disk_component_to_sfn(component, out_name) == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    for (i = 0U; i < CLKS_DISK_DIRENT_NAME_LEN; i++) {
        out_name[i] = ' ';
    }

    i = 0U;
    while (component[i] != '\0') {
        if (component[i] == '.') {
            dot_pos = i;
        }
        i++;
    }

    i = 0U;
    while (component[i] != '\0' && clean_len < sizeof(clean)) {
        char ch;
        if (i == dot_pos) {
            i++;
            continue;
        }

        ch = clks_disk_ascii_upper(component[i]);
        if (clks_disk_sfn_char_valid(ch) == CLKS_FALSE) {
            ch = '_';
        }

        clean[clean_len++] = ch;
        i++;
    }

    if (clean_len == 0U) {
        clean[clean_len++] = '_';
    }

    if (dot_pos != (usize)-1) {
        i = dot_pos + 1U;
        while (component[i] != '\0' && ext_len < sizeof(ext)) {
            char ch = clks_disk_ascii_upper(component[i]);
            if (clks_disk_sfn_char_valid(ch) == CLKS_FALSE) {
                ch = '_';
            }
            ext[ext_len++] = ch;
            i++;
        }
    }

    for (i = 0U; i < clean_len && i < 8U; i++) {
        out_name[i] = (u8)clean[i];
    }
    for (i = 0U; i < ext_len && i < 3U; i++) {
        out_name[8U + i] = (u8)ext[i];
    }

    return CLKS_TRUE;
}

static const char *clks_disk_relative_basename(const char *relative_path) {
    usize len;
    usize i;

    if (relative_path == CLKS_NULL) {
        return "";
    }

    len = clks_strlen(relative_path);
    if (len == 0U) {
        return "";
    }

    for (i = len; i != 0U; i--) {
        if (relative_path[i - 1U] == '/') {
            return &relative_path[i];
        }
    }

    return relative_path;
}

static clks_bool clks_disk_write_named_dir_entry(u32 parent_cluster, const char *component, u8 attr, u32 first_cluster,
                                                 u32 size, struct clks_disk_dir_slot *out_slot) {
    u8 sfn[CLKS_DISK_DIRENT_NAME_LEN];
    u8 entry[CLKS_DISK_DIRENT_SIZE];
    struct clks_disk_dir_slot free_slot;
    usize name_len;
    u32 lfn_count;
    u32 needed;
    u8 checksum;
    u32 i;

    if (component == CLKS_NULL || out_slot == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_component_generate_sfn(component, sfn) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    name_len = clks_disk_component_len(component);
    lfn_count = (name_len + 12U) / 13U;
    if (lfn_count == 0U || lfn_count > CLKS_DISK_LFN_MAX_ENTRIES) {
        return CLKS_FALSE;
    }

    needed = lfn_count + 1U;
    if (clks_disk_dir_find_free_slots(parent_cluster, needed, &free_slot) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    checksum = clks_disk_lfn_checksum(sfn);
    for (i = 0U; i < lfn_count; i++) {
        u8 lfn_entry[CLKS_DISK_DIRENT_SIZE];
        u8 order = (u8)(lfn_count - i);

        if (i == 0U) {
            order |= 0x40U;
        }

        clks_disk_build_lfn_entry(lfn_entry, component, name_len, order, checksum);
        if (clks_disk_write_dir_entry_at(free_slot.cluster, free_slot.entry_index + i, lfn_entry) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    clks_disk_build_dir_entry(entry, sfn, attr, first_cluster, size);
    if (clks_disk_write_dir_entry_at(free_slot.cluster, free_slot.entry_index + lfn_count, entry) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    out_slot->cluster = free_slot.cluster;
    out_slot->entry_index = free_slot.entry_index + lfn_count;
    return CLKS_TRUE;
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
        clks_bool last = CLKS_FALSE;
        clks_bool found = CLKS_FALSE;
        struct clks_disk_dir_slot slot;
        struct clks_disk_dir_info info;
        u8 sfn[CLKS_DISK_DIRENT_NAME_LEN];

        if (clks_disk_next_component(relative_path, &pos, component, sizeof(component), &last) == CLKS_FALSE) {
            clks_log(CLKS_LOG_WARN, "DISK", "LOOKUP FAILED: COMPONENT");
            clks_log(CLKS_LOG_WARN, "DISK", relative_path);
            return CLKS_FALSE;
        }

        if (clks_disk_dir_find_entry_by_component(current_dir, component, &found, &slot, &info, sfn) == CLKS_FALSE) {
            clks_log(CLKS_LOG_WARN, "DISK", "LOOKUP FAILED: DIR SCAN");
            clks_log(CLKS_LOG_WARN, "DISK", relative_path);
            clks_log(CLKS_LOG_WARN, "DISK", component);
            clks_log_hex(CLKS_LOG_WARN, "DISK", "DIR_CLUSTER", (u64)current_dir);
            return CLKS_FALSE;
        }

        if (last == CLKS_TRUE) {
            out_lookup->found = found;
            out_lookup->parent_cluster = current_dir;
            if (found == CLKS_TRUE) {
                clks_memcpy(out_lookup->sfn, sfn, CLKS_DISK_DIRENT_NAME_LEN);
            } else if (clks_disk_component_generate_sfn(component, out_lookup->sfn) == CLKS_FALSE) {
                return CLKS_FALSE;
            }
            if (found == CLKS_TRUE) {
                out_lookup->slot = slot;
                out_lookup->info = info;
            }
            return CLKS_TRUE;
        }

        if (found == CLKS_FALSE) {
            clks_log(CLKS_LOG_WARN, "DISK", "LOOKUP FAILED: PARENT MISSING");
            clks_log(CLKS_LOG_WARN, "DISK", relative_path);
            clks_log(CLKS_LOG_WARN, "DISK", component);
            return CLKS_FALSE;
        }

        if ((info.attr & CLKS_DISK_ATTR_DIRECTORY) == 0U) {
            clks_log(CLKS_LOG_WARN, "DISK", "LOOKUP FAILED: NOT DIR");
            clks_log(CLKS_LOG_WARN, "DISK", relative_path);
            clks_log(CLKS_LOG_WARN, "DISK", component);
            return CLKS_FALSE;
        }

        if (clks_disk_cluster_valid(info.first_cluster) == CLKS_FALSE) {
            clks_log(CLKS_LOG_WARN, "DISK", "LOOKUP FAILED: BAD DIR CLUSTER");
            clks_log(CLKS_LOG_WARN, "DISK", relative_path);
            clks_log(CLKS_LOG_WARN, "DISK", component);
            clks_log_hex(CLKS_LOG_WARN, "DISK", "FIRST_CLUSTER", (u64)info.first_cluster);
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
                continue;
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
