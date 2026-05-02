static clks_bool clks_disk_bytes_equal(const u8 *left, const u8 *right, usize count) {
    usize i;

    if (left == CLKS_NULL || right == CLKS_NULL) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < count; i++) {
        if (left[i] != right[i]) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

static clks_bool clks_disk_text_prefix_equals(const char *text, const char *prefix, usize prefix_len) {
    usize i;

    if (text == CLKS_NULL || prefix == CLKS_NULL) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < prefix_len; i++) {
        if (text[i] != prefix[i]) {
            return CLKS_FALSE;
        }
    }

    return CLKS_TRUE;
}

static void clks_disk_copy_text(char *dst, usize dst_size, const char *src) {
    usize i = 0U;

    if (dst == CLKS_NULL || dst_size == 0U) {
        return;
    }

    if (src == CLKS_NULL) {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i + 1U < dst_size) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static clks_bool clks_disk_normalize_absolute_path(const char *path, char *out_path, usize out_size) {
    usize in_pos = 0U;
    usize out_pos = 0U;

    if (path == CLKS_NULL || out_path == CLKS_NULL || out_size < 2U) {
        return CLKS_FALSE;
    }

    if (path[0] != '/') {
        return CLKS_FALSE;
    }

    out_path[out_pos++] = '/';

    while (path[in_pos] == '/') {
        in_pos++;
    }

    while (path[in_pos] != '\0') {
        usize comp_start = in_pos;
        usize comp_len;

        while (path[in_pos] != '\0' && path[in_pos] != '/') {
            in_pos++;
        }

        comp_len = in_pos - comp_start;

        if (comp_len == 0U) {
            while (path[in_pos] == '/') {
                in_pos++;
            }
            continue;
        }

        if (comp_len == 1U && path[comp_start] == '.') {
            while (path[in_pos] == '/') {
                in_pos++;
            }
            continue;
        }

        if (comp_len == 2U && path[comp_start] == '.' && path[comp_start + 1U] == '.') {
            return CLKS_FALSE;
        }

        if (out_pos > 1U) {
            if (out_pos + 1U >= out_size) {
                return CLKS_FALSE;
            }
            out_path[out_pos++] = '/';
        }

        if (out_pos + comp_len >= out_size) {
            return CLKS_FALSE;
        }

        clks_memcpy(out_path + out_pos, path + comp_start, comp_len);
        out_pos += comp_len;

        while (path[in_pos] == '/') {
            in_pos++;
        }
    }

    out_path[out_pos] = '\0';
    return CLKS_TRUE;
}

static clks_bool clks_disk_normalize_relative_path(const char *path, char *out_path, usize out_size) {
    usize in_pos = 0U;
    usize out_pos = 0U;

    if (path == CLKS_NULL || out_path == CLKS_NULL || out_size == 0U) {
        return CLKS_FALSE;
    }

    if (path[0] == '/') {
        return CLKS_FALSE;
    }

    while (path[in_pos] == '/') {
        in_pos++;
    }

    while (path[in_pos] != '\0') {
        usize comp_start = in_pos;
        usize comp_len;

        while (path[in_pos] != '\0' && path[in_pos] != '/') {
            in_pos++;
        }

        comp_len = in_pos - comp_start;

        if (comp_len == 0U) {
            while (path[in_pos] == '/') {
                in_pos++;
            }
            continue;
        }

        if (comp_len == 1U && path[comp_start] == '.') {
            while (path[in_pos] == '/') {
                in_pos++;
            }
            continue;
        }

        if (comp_len == 2U && path[comp_start] == '.' && path[comp_start + 1U] == '.') {
            return CLKS_FALSE;
        }

        if (out_pos != 0U) {
            if (out_pos + 1U >= out_size) {
                return CLKS_FALSE;
            }
            out_path[out_pos++] = '/';
        }

        if (out_pos + comp_len >= out_size) {
            return CLKS_FALSE;
        }

        clks_memcpy(out_path + out_pos, path + comp_start, comp_len);
        out_pos += comp_len;

        while (path[in_pos] == '/') {
            in_pos++;
        }
    }

    out_path[out_pos] = '\0';
    return CLKS_TRUE;
}

static clks_bool clks_disk_path_to_relative(const char *path, char *out_relative, usize out_size) {
    char normalized[CLKS_DISK_PATH_MAX];
    usize mount_len;

    if (out_relative == CLKS_NULL || out_size == 0U) {
        return CLKS_FALSE;
    }

    if (clks_disk_mounted == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_normalize_absolute_path(path, normalized, sizeof(normalized)) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    mount_len = clks_strlen(clks_disk_mount_path_buf);

    if (clks_strcmp(normalized, clks_disk_mount_path_buf) == 0) {
        out_relative[0] = '\0';
        return CLKS_TRUE;
    }

    if (mount_len >= clks_strlen(normalized)) {
        return CLKS_FALSE;
    }

    if (clks_disk_text_prefix_equals(normalized, clks_disk_mount_path_buf, mount_len) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (normalized[mount_len] != '/') {
        return CLKS_FALSE;
    }

    return clks_disk_normalize_relative_path(normalized + mount_len + 1U, out_relative, out_size);
}

