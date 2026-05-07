#include <clks/fs.h>
#include <clks/heap.h>
#include <clks/inputm.h>
#include <clks/keyboard.h>
#include <clks/log.h>
#include <clks/string.h>
#include <clks/tty.h>
#include <clks/types.h>

#define CLKS_INPUTM_MAX 8U
#define CLKS_INPUTM_COMP_MAX 32U
#define CLKS_INPUTM_CAND_MAX 64U
#define CLKS_INPUTM_CAND_PAGE_SIZE 5U
#define CLKS_INPUTM_CAND_BYTES 64U
#define CLKS_INPUTM_SPLIT_MAX_PARTS 6U
#define CLKS_INPUTM_SPLIT_MAX_SYLLABLE 6U
#define CLKS_INPUTM_SPLIT_ALT_MAX 2U
#define CLKS_INPUTM_DICT_PATH "/system/inputm/pinyin.db"
#define CLKS_INPUTM_DIR "/shell/inputm"

struct clks_inputm_entry {
    clks_bool used;
    char name[CLKS_INPUTM_NAME_MAX];
    char path[CLKS_INPUTM_PATH_MAX];
    u64 flags;
};

static struct clks_inputm_entry clks_inputm_entries[CLKS_INPUTM_MAX];
static u64 clks_inputm_entry_count = 0ULL;
static u64 clks_inputm_current_index = 0ULL;
static char clks_inputm_comp[CLKS_INPUTM_COMP_MAX];
static u32 clks_inputm_comp_len = 0U;
static char clks_inputm_candidates[CLKS_INPUTM_CAND_MAX][CLKS_INPUTM_CAND_BYTES];
static u32 clks_inputm_candidate_count = 0U;
static u32 clks_inputm_candidate_page = 0U;
static char clks_inputm_status[CLKS_INPUTM_STATUS_MAX];

static clks_bool clks_inputm_is_alpha(char ch) {
    return ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) ? CLKS_TRUE : CLKS_FALSE;
}

static char clks_inputm_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int clks_inputm_memcmp(const char *left, const char *right, usize count) {
    usize i;

    if (left == right) {
        return 0;
    }
    if (left == CLKS_NULL) {
        return -1;
    }
    if (right == CLKS_NULL) {
        return 1;
    }

    for (i = 0U; i < count; i++) {
        unsigned char l = (unsigned char)left[i];
        unsigned char r = (unsigned char)right[i];
        if (l != r) {
            return (l < r) ? -1 : 1;
        }
    }

    return 0;
}

static void clks_inputm_copy_string(char *dst, usize dst_size, const char *src) {
    usize i = 0U;

    if (dst == CLKS_NULL || dst_size == 0U) {
        return;
    }

    if (src == CLKS_NULL) {
        dst[0] = '\0';
        return;
    }

    while (i + 1U < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void clks_inputm_append_text(char *line, usize line_size, usize *pos, const char *text) {
    usize i = 0U;

    if (line == CLKS_NULL || pos == CLKS_NULL || text == CLKS_NULL || line_size == 0U) {
        return;
    }

    while (text[i] != '\0' && *pos + 1U < line_size) {
        line[*pos] = text[i];
        (*pos)++;
        i++;
    }

    line[*pos] = '\0';
}

static void clks_inputm_append_u32_dec(char *line, usize line_size, usize *pos, u32 value) {
    char tmp[10];
    u32 len = 0U;

    if (value == 0U) {
        clks_inputm_append_text(line, line_size, pos, "0");
        return;
    }

    while (value > 0U && len < (u32)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (len > 0U) {
        char one[2];
        len--;
        one[0] = tmp[len];
        one[1] = '\0';
        clks_inputm_append_text(line, line_size, pos, one);
    }
}

static void clks_inputm_clear_candidates(void) {
    u32 i;

    clks_inputm_candidate_count = 0U;
    clks_inputm_candidate_page = 0U;
    for (i = 0U; i < CLKS_INPUTM_CAND_MAX; i++) {
        clks_inputm_candidates[i][0] = '\0';
    }
}

static clks_bool clks_inputm_has_suffix(const char *text, const char *suffix) {
    usize text_len;
    usize suffix_len;

    if (text == CLKS_NULL || suffix == CLKS_NULL) {
        return CLKS_FALSE;
    }

    text_len = clks_strlen(text);
    suffix_len = clks_strlen(suffix);
    if (suffix_len > text_len) {
        return CLKS_FALSE;
    }

    return (clks_strcmp(text + (text_len - suffix_len), suffix) == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static void clks_inputm_refresh_status(void) {
    char line[CLKS_INPUTM_STATUS_MAX];
    usize pos = 0U;
    const char *name = clks_inputm_current_name();
    u32 page_start;
    u32 page_end;
    u32 i;

    for (i = 0U; i < CLKS_INPUTM_STATUS_MAX; i++) {
        line[i] = '\0';
    }

    clks_inputm_append_text(line, sizeof(line), &pos, "IME:");
    clks_inputm_append_text(line, sizeof(line), &pos, name);

    if (clks_inputm_comp_len > 0U) {
        u32 page_count;

        clks_inputm_append_text(line, sizeof(line), &pos, "  PINYIN:");
        clks_inputm_append_text(line, sizeof(line), &pos, clks_inputm_comp);
        clks_inputm_append_text(line, sizeof(line), &pos, "  ");

        page_start = clks_inputm_candidate_page * CLKS_INPUTM_CAND_PAGE_SIZE;
        if (page_start >= clks_inputm_candidate_count) {
            clks_inputm_candidate_page = 0U;
            page_start = 0U;
        }
        page_end = page_start + CLKS_INPUTM_CAND_PAGE_SIZE;
        if (page_end > clks_inputm_candidate_count) {
            page_end = clks_inputm_candidate_count;
        }

        if (clks_inputm_candidate_count == 0U) {
            clks_inputm_append_text(line, sizeof(line), &pos, "no candidates");
        } else {
            for (i = page_start; i < page_end; i++) {
                clks_inputm_append_u32_dec(line, sizeof(line), &pos, (i - page_start) + 1U);
                clks_inputm_append_text(line, sizeof(line), &pos, ".");
                clks_inputm_append_text(line, sizeof(line), &pos, clks_inputm_candidates[i]);
                clks_inputm_append_text(line, sizeof(line), &pos, " ");
            }

            page_count = (clks_inputm_candidate_count + CLKS_INPUTM_CAND_PAGE_SIZE - 1U) / CLKS_INPUTM_CAND_PAGE_SIZE;
            clks_inputm_append_text(line, sizeof(line), &pos, " +/- ");
            clks_inputm_append_u32_dec(line, sizeof(line), &pos, clks_inputm_candidate_page + 1U);
            clks_inputm_append_text(line, sizeof(line), &pos, "/");
            clks_inputm_append_u32_dec(line, sizeof(line), &pos, page_count);
        }
    }

    line[pos] = '\0';
    clks_inputm_copy_string(clks_inputm_status, sizeof(clks_inputm_status), line);
    clks_tty_status_refresh();
}

static u64 clks_inputm_register_internal(const char *name, const char *path, u64 flags) {
    u64 i;
    struct clks_inputm_entry *entry;

    if (name == CLKS_NULL || name[0] == '\0') {
        return (u64)-1;
    }

    for (i = 0ULL; i < clks_inputm_entry_count; i++) {
        if (clks_inputm_entries[i].used == CLKS_TRUE && clks_strcmp(clks_inputm_entries[i].name, name) == 0) {
            clks_inputm_copy_string(clks_inputm_entries[i].path, sizeof(clks_inputm_entries[i].path), path);
            clks_inputm_entries[i].flags = flags;
            return i;
        }
    }

    if (clks_inputm_entry_count >= CLKS_INPUTM_MAX) {
        return (u64)-1;
    }

    entry = &clks_inputm_entries[clks_inputm_entry_count];
    entry->used = CLKS_TRUE;
    clks_inputm_copy_string(entry->name, sizeof(entry->name), name);
    clks_inputm_copy_string(entry->path, sizeof(entry->path), path);
    entry->flags = flags;
    clks_inputm_entry_count++;
    return clks_inputm_entry_count - 1ULL;
}

static void clks_inputm_detect_userland_imes(void) {
    u64 count;
    u64 i;

    count = clks_fs_count_children(CLKS_INPUTM_DIR);
    if (count == (u64)-1) {
        return;
    }

    for (i = 0ULL; i < count; i++) {
        char child_name[96];
        char path[CLKS_INPUTM_PATH_MAX];

        if (clks_fs_get_child_name(CLKS_INPUTM_DIR, i, child_name, sizeof(child_name)) == CLKS_FALSE) {
            continue;
        }
        if (clks_inputm_has_suffix(child_name, ".elf") == CLKS_FALSE) {
            continue;
        }

        clks_inputm_copy_string(path, sizeof(path), CLKS_INPUTM_DIR "/");
        clks_inputm_copy_string(path + clks_strlen(path), sizeof(path) - clks_strlen(path), child_name);

        if (clks_strcmp(child_name, "pinyin.elf") == 0) {
            (void)clks_inputm_register_internal("PinyinCN", path, CLKS_INPUTM_FLAG_CHINESE_PINYIN);
        } else {
            (void)clks_inputm_register_internal(child_name, path, 0ULL);
        }
    }
}

static clks_bool clks_inputm_lookup_candidate(const char *key, u32 candidate_index, char *out_text, usize out_size) {
    u64 size = 0ULL;
    const char *data = (const char *)clks_fs_read_all(CLKS_INPUTM_DICT_PATH, &size);
    u64 pos = 0ULL;

    if (key == CLKS_NULL || out_text == CLKS_NULL || out_size == 0U || data == CLKS_NULL || size == 0ULL) {
        return CLKS_FALSE;
    }

    while (pos < size) {
        u64 line_start = pos;
        u64 line_end;
        u64 key_pos = 0ULL;

        while (pos < size && data[pos] != '\n' && data[pos] != '\r') {
            pos++;
        }
        line_end = pos;
        while (pos < size && (data[pos] == '\n' || data[pos] == '\r')) {
            pos++;
        }

        if (line_start >= line_end || data[line_start] == '#') {
            continue;
        }

        while (line_start + key_pos < line_end && key[key_pos] != '\0' &&
               data[line_start + key_pos] == key[key_pos]) {
            key_pos++;
        }

        if (key[key_pos] != '\0') {
            continue;
        }

        if (line_start + key_pos >= line_end || data[line_start + key_pos] != '\t') {
            continue;
        }

        {
            u64 cand_start = line_start + key_pos + 1ULL;
            u64 cand_end = cand_start;
            u32 current = 0U;

            while (cand_start < line_end) {
                while (cand_start < line_end && data[cand_start] == ' ') {
                    cand_start++;
                }
                cand_end = cand_start;
                while (cand_end < line_end && data[cand_end] != ' ') {
                    cand_end++;
                }
                if (cand_end > cand_start) {
                    if (current == candidate_index) {
                        u64 len = cand_end - cand_start;
                        if (len + 1ULL > (u64)out_size) {
                            len = (u64)out_size - 1ULL;
                        }
                        clks_memcpy(out_text, data + cand_start, (usize)len);
                        out_text[len] = '\0';
                        return CLKS_TRUE;
                    }
                    current++;
                }
                cand_start = cand_end + 1ULL;
            }
        }
    }

    return CLKS_FALSE;
}

static void clks_inputm_add_candidate(const char *start, u64 len) {
    u64 copy_len;
    u32 i;

    if (start == CLKS_NULL || len == 0ULL || clks_inputm_candidate_count >= CLKS_INPUTM_CAND_MAX) {
        return;
    }

    copy_len = len;
    if (copy_len + 1ULL > CLKS_INPUTM_CAND_BYTES) {
        copy_len = CLKS_INPUTM_CAND_BYTES - 1ULL;
    }

    for (i = 0U; i < clks_inputm_candidate_count; i++) {
        if (clks_strlen(clks_inputm_candidates[i]) == copy_len &&
            clks_inputm_memcmp(clks_inputm_candidates[i], start, (usize)copy_len) == 0) {
            return;
        }
    }

    clks_memcpy(clks_inputm_candidates[clks_inputm_candidate_count], start, (usize)copy_len);
    clks_inputm_candidates[clks_inputm_candidate_count][copy_len] = '\0';
    clks_inputm_candidate_count++;
}

static clks_bool clks_inputm_lookup_candidate_in_data(const char *data, u64 size, const char *key, u64 key_len,
                                                      u32 candidate_index, char *out_text, usize out_size) {
    u64 pos = 0ULL;

    if (data == CLKS_NULL || key == CLKS_NULL || key_len == 0ULL || out_text == CLKS_NULL || out_size == 0U) {
        return CLKS_FALSE;
    }

    while (pos < size) {
        u64 line_start = pos;
        u64 line_end;

        while (pos < size && data[pos] != '\n' && data[pos] != '\r') {
            pos++;
        }
        line_end = pos;
        while (pos < size && (data[pos] == '\n' || data[pos] == '\r')) {
            pos++;
        }

        if (line_start >= line_end || data[line_start] == '#') {
            continue;
        }

        if (line_start + key_len >= line_end || data[line_start + key_len] != '\t') {
            continue;
        }

        if (clks_inputm_memcmp(data + line_start, key, (usize)key_len) != 0) {
            continue;
        }

        {
            u64 cand_start = line_start + key_len + 1ULL;
            u64 cand_end = cand_start;
            u32 current = 0U;

            while (cand_start < line_end) {
                while (cand_start < line_end && data[cand_start] == ' ') {
                    cand_start++;
                }
                cand_end = cand_start;
                while (cand_end < line_end && data[cand_end] != ' ') {
                    cand_end++;
                }
                if (cand_end > cand_start) {
                    if (current == candidate_index) {
                        u64 len = cand_end - cand_start;
                        if (len + 1ULL > (u64)out_size) {
                            len = (u64)out_size - 1ULL;
                        }
                        clks_memcpy(out_text, data + cand_start, (usize)len);
                        out_text[len] = '\0';
                        return CLKS_TRUE;
                    }
                    current++;
                }
                cand_start = cand_end + 1ULL;
            }
        }
    }

    return CLKS_FALSE;
}

static void clks_inputm_append_candidate_bytes(char *out, usize out_size, const char *text) {
    usize out_len;
    usize i = 0U;

    if (out == CLKS_NULL || out_size == 0U || text == CLKS_NULL) {
        return;
    }

    out_len = clks_strlen(out);
    while (text[i] != '\0' && out_len + 1U < out_size) {
        out[out_len++] = text[i++];
    }
    out[out_len] = '\0';
}

static void clks_inputm_rebuild_split_candidates_rec(const char *data, u64 size, u32 start, u32 depth,
                                                     char *phrase, usize phrase_size) {
    u32 end;
    usize phrase_len;

    if (clks_inputm_candidate_count >= CLKS_INPUTM_CAND_MAX || depth >= CLKS_INPUTM_SPLIT_MAX_PARTS) {
        return;
    }

    if (start >= clks_inputm_comp_len) {
        if (phrase != CLKS_NULL && phrase[0] != '\0') {
            clks_inputm_add_candidate(phrase, clks_strlen(phrase));
        }
        return;
    }

    phrase_len = clks_strlen(phrase);
    for (end = start + 1U; end <= clks_inputm_comp_len && end <= start + CLKS_INPUTM_SPLIT_MAX_SYLLABLE; end++) {
        u32 alt;

        for (alt = 0U; alt < CLKS_INPUTM_SPLIT_ALT_MAX; alt++) {
            char cand[CLKS_INPUTM_CAND_BYTES];
            char saved[CLKS_INPUTM_CAND_BYTES];

            if (clks_inputm_lookup_candidate_in_data(data, size, clks_inputm_comp + start, (u64)(end - start), alt,
                                                     cand, sizeof(cand)) == CLKS_FALSE) {
                break;
            }

            clks_inputm_copy_string(saved, sizeof(saved), phrase);
            clks_inputm_append_candidate_bytes(phrase, phrase_size, cand);
            clks_inputm_rebuild_split_candidates_rec(data, size, end, depth + 1U, phrase, phrase_size);
            clks_inputm_copy_string(phrase, phrase_size, saved);
        }
    }

    (void)phrase_len;
}

static void clks_inputm_rebuild_split_candidates(const char *data, u64 size) {
    char phrase[CLKS_INPUTM_CAND_BYTES];

    if (data == CLKS_NULL || size == 0ULL || clks_inputm_comp_len < 2U) {
        return;
    }

    phrase[0] = '\0';
    clks_inputm_rebuild_split_candidates_rec(data, size, 0U, 0U, phrase, sizeof(phrase));
}

static void clks_inputm_rebuild_candidates(void) {
    u64 size = 0ULL;
    const char *data = (const char *)clks_fs_read_all(CLKS_INPUTM_DICT_PATH, &size);
    u64 pos = 0ULL;

    clks_inputm_clear_candidates();
    if (clks_inputm_comp_len == 0U || data == CLKS_NULL || size == 0ULL) {
        return;
    }

    while (pos < size && clks_inputm_candidate_count < CLKS_INPUTM_CAND_MAX) {
        u64 line_start = pos;
        u64 line_end;
        u64 key_pos = 0ULL;

        while (pos < size && data[pos] != '\n' && data[pos] != '\r') {
            pos++;
        }
        line_end = pos;
        while (pos < size && (data[pos] == '\n' || data[pos] == '\r')) {
            pos++;
        }

        if (line_start >= line_end || data[line_start] == '#') {
            continue;
        }

        while (line_start + key_pos < line_end && clks_inputm_comp[key_pos] != '\0' &&
               data[line_start + key_pos] == clks_inputm_comp[key_pos]) {
            key_pos++;
        }

        if (clks_inputm_comp[key_pos] != '\0') {
            continue;
        }

        if (line_start + key_pos >= line_end || data[line_start + key_pos] != '\t') {
            continue;
        }

        {
            u64 cand_start = line_start + key_pos + 1ULL;
            u64 cand_end;

            while (cand_start < line_end && clks_inputm_candidate_count < CLKS_INPUTM_CAND_MAX) {
                while (cand_start < line_end && data[cand_start] == ' ') {
                    cand_start++;
                }
                cand_end = cand_start;
                while (cand_end < line_end && data[cand_end] != ' ') {
                    cand_end++;
                }
                if (cand_end > cand_start) {
                    clks_inputm_add_candidate(data + cand_start, cand_end - cand_start);
                }
                cand_start = cand_end + 1ULL;
            }
        }
    }

    if (clks_inputm_candidate_count < CLKS_INPUTM_CAND_MAX) {
        clks_inputm_rebuild_split_candidates(data, size);
    }
}

static clks_bool clks_inputm_commit_comp(u32 tty_index, u32 candidate_index) {
    u32 absolute_index;

    if (clks_inputm_comp_len == 0U) {
        return CLKS_FALSE;
    }

    absolute_index = (clks_inputm_candidate_page * CLKS_INPUTM_CAND_PAGE_SIZE) + candidate_index;
    if (absolute_index < clks_inputm_candidate_count) {
        (void)clks_keyboard_inject_text_for_tty(tty_index, clks_inputm_candidates[absolute_index]);
    } else if (clks_inputm_lookup_candidate(clks_inputm_comp, absolute_index, clks_inputm_candidates[0],
                                            sizeof(clks_inputm_candidates[0])) == CLKS_TRUE) {
        (void)clks_keyboard_inject_text_for_tty(tty_index, clks_inputm_candidates[0]);
    } else {
        (void)clks_keyboard_inject_text_for_tty(tty_index, clks_inputm_comp);
    }

    clks_inputm_comp_len = 0U;
    clks_inputm_comp[0] = '\0';
    clks_inputm_clear_candidates();
    clks_inputm_refresh_status();
    return CLKS_TRUE;
}

void clks_inputm_init(void) {
    u32 i;

    for (i = 0U; i < CLKS_INPUTM_MAX; i++) {
        clks_inputm_entries[i].used = CLKS_FALSE;
        clks_inputm_entries[i].name[0] = '\0';
        clks_inputm_entries[i].path[0] = '\0';
        clks_inputm_entries[i].flags = 0ULL;
    }

    clks_inputm_entry_count = 0ULL;
    clks_inputm_current_index = 0ULL;
    clks_inputm_comp_len = 0U;
    clks_inputm_comp[0] = '\0';
    clks_inputm_clear_candidates();
    clks_inputm_status[0] = '\0';

    (void)clks_inputm_register_internal("SystemENG", "builtin", 0ULL);
    clks_inputm_detect_userland_imes();
    clks_inputm_refresh_status();

    clks_log(CLKS_LOG_INFO, "INPUTM", "INPUT METHOD FRAMEWORK ONLINE");
    clks_log_hex(CLKS_LOG_INFO, "INPUTM", "COUNT", clks_inputm_entry_count);
}

u64 clks_inputm_count(void) {
    return clks_inputm_entry_count;
}

clks_bool clks_inputm_info_at(u64 index, struct clks_inputm_info *out_info) {
    if (out_info == CLKS_NULL || index >= clks_inputm_entry_count || clks_inputm_entries[index].used == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_memset(out_info, 0, sizeof(*out_info));
    clks_inputm_copy_string(out_info->name, sizeof(out_info->name), clks_inputm_entries[index].name);
    clks_inputm_copy_string(out_info->path, sizeof(out_info->path), clks_inputm_entries[index].path);
    out_info->flags = clks_inputm_entries[index].flags;
    out_info->active = (index == clks_inputm_current_index) ? 1ULL : 0ULL;
    return CLKS_TRUE;
}

u64 clks_inputm_current(void) {
    return clks_inputm_current_index;
}

clks_bool clks_inputm_select(u64 index) {
    if (index >= clks_inputm_entry_count || clks_inputm_entries[index].used == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_inputm_current_index = index;
    clks_inputm_comp_len = 0U;
    clks_inputm_comp[0] = '\0';
    clks_inputm_clear_candidates();
    clks_inputm_refresh_status();
    return CLKS_TRUE;
}

u64 clks_inputm_register(const char *name, const char *path, u64 flags) {
    u64 ret = clks_inputm_register_internal(name, path, flags);
    clks_inputm_refresh_status();
    return ret;
}

void clks_inputm_cycle(void) {
    if (clks_inputm_entry_count == 0ULL) {
        return;
    }

    (void)clks_inputm_select((clks_inputm_current_index + 1ULL) % clks_inputm_entry_count);
}

clks_bool clks_inputm_handle_char(u32 tty_index, char ch) {
    u64 flags;

    if (clks_inputm_current_index >= clks_inputm_entry_count) {
        return CLKS_FALSE;
    }

    flags = clks_inputm_entries[clks_inputm_current_index].flags;
    if ((flags & CLKS_INPUTM_FLAG_CHINESE_PINYIN) == 0ULL) {
        return CLKS_FALSE;
    }

    if (clks_inputm_is_alpha(ch) == CLKS_TRUE) {
        if (clks_inputm_comp_len + 1U < CLKS_INPUTM_COMP_MAX) {
            clks_inputm_comp[clks_inputm_comp_len++] = clks_inputm_lower(ch);
            clks_inputm_comp[clks_inputm_comp_len] = '\0';
            clks_inputm_rebuild_candidates();
            clks_inputm_refresh_status();
        }
        return CLKS_TRUE;
    }

    if (ch >= '1' && ch <= '5' && clks_inputm_comp_len > 0U) {
        return clks_inputm_commit_comp(tty_index, (u32)(ch - '1'));
    }

    if (ch == '+' && clks_inputm_comp_len > 0U) {
        u32 page_count = (clks_inputm_candidate_count + CLKS_INPUTM_CAND_PAGE_SIZE - 1U) / CLKS_INPUTM_CAND_PAGE_SIZE;
        if (page_count > 0U) {
            clks_inputm_candidate_page = (clks_inputm_candidate_page + 1U) % page_count;
            clks_inputm_refresh_status();
        }
        return CLKS_TRUE;
    }

    if (ch == '-' && clks_inputm_comp_len > 0U) {
        u32 page_count = (clks_inputm_candidate_count + CLKS_INPUTM_CAND_PAGE_SIZE - 1U) / CLKS_INPUTM_CAND_PAGE_SIZE;
        if (page_count > 0U) {
            if (clks_inputm_candidate_page == 0U) {
                clks_inputm_candidate_page = page_count - 1U;
            } else {
                clks_inputm_candidate_page--;
            }
            clks_inputm_refresh_status();
        }
        return CLKS_TRUE;
    }

    if (ch == ' ' && clks_inputm_comp_len > 0U) {
        return clks_inputm_commit_comp(tty_index, 0U);
    }

    if (ch == '\b' && clks_inputm_comp_len > 0U) {
        clks_inputm_comp_len--;
        clks_inputm_comp[clks_inputm_comp_len] = '\0';
        clks_inputm_rebuild_candidates();
        clks_inputm_refresh_status();
        return CLKS_TRUE;
    }

    if (ch == 27 && clks_inputm_comp_len > 0U) {
        clks_inputm_comp_len = 0U;
        clks_inputm_comp[0] = '\0';
        clks_inputm_clear_candidates();
        clks_inputm_refresh_status();
        return CLKS_TRUE;
    }

    if (clks_inputm_comp_len > 0U) {
        (void)clks_inputm_commit_comp(tty_index, 0U);
    }

    return CLKS_FALSE;
}

const char *clks_inputm_current_name(void) {
    if (clks_inputm_current_index >= clks_inputm_entry_count) {
        return "SystemENG";
    }
    return clks_inputm_entries[clks_inputm_current_index].name;
}

const char *clks_inputm_status_text(void) {
    return clks_inputm_status;
}

clks_bool clks_inputm_status_is_composing(void) {
    return (clks_inputm_comp_len > 0U) ? CLKS_TRUE : CLKS_FALSE;
}

void clks_inputm_status_set(const char *text) {
    clks_inputm_copy_string(clks_inputm_status, sizeof(clks_inputm_status), text);
    clks_tty_status_refresh();
}

void clks_inputm_status_copy(char *out_text, usize out_size) {
    clks_inputm_copy_string(out_text, out_size, clks_inputm_status);
}
