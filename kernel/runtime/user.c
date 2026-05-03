#include <clks/disk.h>
#include <clks/exec.h>
#include <clks/fs.h>
#include <clks/log.h>
#include <clks/string.h>
#include <clks/types.h>
#include <clks/user.h>

typedef struct clks_user_sha256_ctx {
    u8 data[64];
    u32 datalen;
    u64 bitlen;
    u32 state[8];
} clks_user_sha256_ctx;

struct clks_user_record {
    u64 uid;
    u64 role;
    char name[CLKS_USER_NAME_MAX];
    char hash[CLKS_USER_HASH_HEX_LEN + 1U];
    char home[CLKS_USER_HOME_MAX];
};

static clks_bool clks_user_disk_login_required = CLKS_FALSE;
static const char clks_user_empty_file = 0;

static void clks_user_copy(char *dst, usize dst_size, const char *src) {
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

static void clks_user_append(char *dst, usize dst_size, const char *src) {
    usize len;

    if (dst == CLKS_NULL || dst_size == 0U || src == CLKS_NULL) {
        return;
    }

    len = clks_strlen(dst);
    if (len >= dst_size) {
        dst[dst_size - 1U] = '\0';
        return;
    }

    clks_user_copy(dst + len, dst_size - len, src);
}

static clks_bool clks_user_streq(const char *left, const char *right) {
    return (left != CLKS_NULL && right != CLKS_NULL && clks_strcmp(left, right) == 0) ? CLKS_TRUE : CLKS_FALSE;
}

static clks_bool clks_user_has_prefix(const char *text, const char *prefix) {
    usize i = 0U;

    if (text == CLKS_NULL || prefix == CLKS_NULL) {
        return CLKS_FALSE;
    }

    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return CLKS_FALSE;
        }
        i++;
    }

    return CLKS_TRUE;
}

static clks_bool clks_user_path_is_at_or_under(const char *path, const char *prefix) {
    usize prefix_len;

    if (path == CLKS_NULL || prefix == CLKS_NULL) {
        return CLKS_FALSE;
    }

    prefix_len = clks_strlen(prefix);
    if (clks_user_has_prefix(path, prefix) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return (path[prefix_len] == '\0' || path[prefix_len] == '/') ? CLKS_TRUE : CLKS_FALSE;
}

static void clks_user_trim(char *text) {
    usize start = 0U;
    usize len;

    if (text == CLKS_NULL) {
        return;
    }

    while (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n') {
        start++;
    }

    if (start > 0U) {
        clks_memmove(text, text + start, clks_strlen(text + start) + 1U);
    }

    len = clks_strlen(text);
    while (len > 0U) {
        char ch = text[len - 1U];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        text[len - 1U] = '\0';
        len--;
    }
}

static clks_bool clks_user_name_valid(const char *name) {
    usize i;
    usize len;

    if (name == CLKS_NULL || name[0] == '\0') {
        return CLKS_FALSE;
    }

    len = clks_strlen(name);
    if (len >= CLKS_USER_NAME_MAX) {
        return CLKS_FALSE;
    }

    for (i = 0U; name[i] != '\0'; i++) {
        char ch = name[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' ||
            ch == '-') {
            continue;
        }
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static u32 clks_user_rotr(u32 value, u32 count) {
    return (value >> count) | (value << (32U - count));
}

static u32 clks_user_load_be32(const u8 *data) {
    return (((u32)data[0]) << 24U) | (((u32)data[1]) << 16U) | (((u32)data[2]) << 8U) | ((u32)data[3]);
}

static void clks_user_store_be32(u32 value, u8 *out) {
    out[0] = (u8)((value >> 24U) & 0xFFU);
    out[1] = (u8)((value >> 16U) & 0xFFU);
    out[2] = (u8)((value >> 8U) & 0xFFU);
    out[3] = (u8)(value & 0xFFU);
}

static void clks_user_sha256_transform(clks_user_sha256_ctx *ctx, const u8 data[64]) {
    static const u32 k[64] = {
        0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU, 0x59F111F1U, 0x923F82A4U,
        0xAB1C5ED5U, 0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U, 0x72BE5D74U, 0x80DEB1FEU,
        0x9BDC06A7U, 0xC19BF174U, 0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU, 0x2DE92C6FU,
        0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU, 0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
        0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U, 0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU,
        0x53380D13U, 0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U, 0xA2BFE8A1U, 0xA81A664BU,
        0xC24B8B70U, 0xC76C51A3U, 0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U, 0x19A4C116U,
        0x1E376C08U, 0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
        0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U, 0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U,
        0xC67178F2U};
    u32 m[64];
    u32 a;
    u32 b;
    u32 c;
    u32 d;
    u32 e;
    u32 f;
    u32 g;
    u32 h;
    u32 i;

    for (i = 0U; i < 16U; i++) {
        m[i] = clks_user_load_be32(data + (i * 4U));
    }

    for (i = 16U; i < 64U; i++) {
        u32 s0 = clks_user_rotr(m[i - 15U], 7U) ^ clks_user_rotr(m[i - 15U], 18U) ^ (m[i - 15U] >> 3U);
        u32 s1 = clks_user_rotr(m[i - 2U], 17U) ^ clks_user_rotr(m[i - 2U], 19U) ^ (m[i - 2U] >> 10U);
        m[i] = m[i - 16U] + s0 + m[i - 7U] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0U; i < 64U; i++) {
        u32 s1 = clks_user_rotr(e, 6U) ^ clks_user_rotr(e, 11U) ^ clks_user_rotr(e, 25U);
        u32 ch = (e & f) ^ ((~e) & g);
        u32 temp1 = h + s1 + ch + k[i] + m[i];
        u32 s0 = clks_user_rotr(a, 2U) ^ clks_user_rotr(a, 13U) ^ clks_user_rotr(a, 22U);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        u32 temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void clks_user_sha256_init(clks_user_sha256_ctx *ctx) {
    ctx->datalen = 0U;
    ctx->bitlen = 0ULL;
    ctx->state[0] = 0x6A09E667U;
    ctx->state[1] = 0xBB67AE85U;
    ctx->state[2] = 0x3C6EF372U;
    ctx->state[3] = 0xA54FF53AU;
    ctx->state[4] = 0x510E527FU;
    ctx->state[5] = 0x9B05688CU;
    ctx->state[6] = 0x1F83D9ABU;
    ctx->state[7] = 0x5BE0CD19U;
}

static void clks_user_sha256_update(clks_user_sha256_ctx *ctx, const u8 *data, u64 len) {
    u64 i;

    for (i = 0ULL; i < len; i++) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64U) {
            clks_user_sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512ULL;
            ctx->datalen = 0U;
        }
    }
}

static void clks_user_sha256_final(clks_user_sha256_ctx *ctx, u8 hash[32]) {
    u32 i = ctx->datalen;

    if (ctx->datalen < 56U) {
        ctx->data[i++] = 0x80U;
        while (i < 56U) {
            ctx->data[i++] = 0U;
        }
    } else {
        ctx->data[i++] = 0x80U;
        while (i < 64U) {
            ctx->data[i++] = 0U;
        }
        clks_user_sha256_transform(ctx, ctx->data);
        clks_memset(ctx->data, 0, 56U);
    }

    ctx->bitlen += ((u64)ctx->datalen) * 8ULL;
    ctx->data[63] = (u8)(ctx->bitlen);
    ctx->data[62] = (u8)(ctx->bitlen >> 8U);
    ctx->data[61] = (u8)(ctx->bitlen >> 16U);
    ctx->data[60] = (u8)(ctx->bitlen >> 24U);
    ctx->data[59] = (u8)(ctx->bitlen >> 32U);
    ctx->data[58] = (u8)(ctx->bitlen >> 40U);
    ctx->data[57] = (u8)(ctx->bitlen >> 48U);
    ctx->data[56] = (u8)(ctx->bitlen >> 56U);
    clks_user_sha256_transform(ctx, ctx->data);

    for (i = 0U; i < 8U; i++) {
        clks_user_store_be32(ctx->state[i], hash + (i * 4U));
    }
}

static char clks_user_hex_digit(u64 value) {
    value &= 0xFULL;
    return (value < 10ULL) ? (char)('0' + value) : (char)('a' + (value - 10ULL));
}

static clks_bool clks_user_parse_u64_dec(const char *text, u64 *out_value) {
    u64 value = 0ULL;
    usize i;

    if (text == CLKS_NULL || text[0] == '\0' || out_value == CLKS_NULL) {
        return CLKS_FALSE;
    }

    for (i = 0U; text[i] != '\0'; i++) {
        u64 digit;
        if (text[i] < '0' || text[i] > '9') {
            return CLKS_FALSE;
        }
        digit = (u64)(text[i] - '0');
        if (value > ((~0ULL) - digit) / 10ULL) {
            return CLKS_FALSE;
        }
        value = (value * 10ULL) + digit;
    }

    *out_value = value;
    return CLKS_TRUE;
}

static void clks_user_append_u64_dec(char *out, usize out_size, u64 value) {
    char tmp[32];
    usize len = 0U;

    if (out == CLKS_NULL || out_size == 0U) {
        return;
    }

    if (value == 0ULL) {
        clks_user_append(out, out_size, "0");
        return;
    }

    while (value != 0ULL && len < sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    }

    while (len > 0U) {
        char one[2];
        one[0] = tmp[--len];
        one[1] = '\0';
        clks_user_append(out, out_size, one);
    }
}

static u64 clks_user_uid_for_name(const char *name) {
    u64 hash = 5381ULL;
    usize i;

    if (clks_user_streq(name, "root") == CLKS_TRUE) {
        return CLKS_USER_UID_ROOT;
    }

    if (name == CLKS_NULL || name[0] == '\0') {
        return CLKS_USER_UID_NOBODY;
    }

    for (i = 0U; name[i] != '\0'; i++) {
        hash = ((hash << 5ULL) + hash) ^ (u64)(u8)name[i];
    }

    return CLKS_USER_UID_BASE + (hash % 60000ULL);
}

static void clks_user_hash_password(const char *password, char out_hex[CLKS_USER_HASH_HEX_LEN + 1U]) {
    clks_user_sha256_ctx ctx;
    u8 hash[32];
    u64 i;

    clks_user_sha256_init(&ctx);
    if (password != CLKS_NULL) {
        clks_user_sha256_update(&ctx, (const u8 *)password, (u64)clks_strlen(password));
    }
    clks_user_sha256_final(&ctx, hash);

    for (i = 0ULL; i < 32ULL; i++) {
        out_hex[i * 2ULL] = clks_user_hex_digit(((u64)hash[i]) >> 4U);
        out_hex[(i * 2ULL) + 1ULL] = clks_user_hex_digit((u64)hash[i]);
    }
    out_hex[CLKS_USER_HASH_HEX_LEN] = '\0';
}

static clks_bool clks_user_read_db(char *out, u64 out_size, u64 *out_len) {
    const void *data;
    u64 size = 0ULL;

    if (out == CLKS_NULL || out_size == 0ULL) {
        return CLKS_FALSE;
    }

    out[0] = '\0';
    if (out_len != CLKS_NULL) {
        *out_len = 0ULL;
    }

    data = clks_fs_read_all(CLKS_USER_DB_PATH, &size);
    if (data == CLKS_NULL || size + 1ULL > out_size) {
        return CLKS_FALSE;
    }

    if (size > 0ULL) {
        clks_memcpy(out, data, (usize)size);
    }
    out[size] = '\0';

    if (out_len != CLKS_NULL) {
        *out_len = size;
    }

    return CLKS_TRUE;
}

static char *clks_user_next_field(char **io_cursor) {
    char *start;

    if (io_cursor == CLKS_NULL || *io_cursor == CLKS_NULL) {
        return CLKS_NULL;
    }

    start = *io_cursor;
    while (**io_cursor != '\0' && **io_cursor != ':') {
        (*io_cursor)++;
    }

    if (**io_cursor == ':') {
        **io_cursor = '\0';
        (*io_cursor)++;
    } else {
        *io_cursor = CLKS_NULL;
    }

    return start;
}

static clks_bool clks_user_parse_record(const char *line, struct clks_user_record *out_record) {
    char tmp[CLKS_USER_RECORD_MAX];
    char *cursor;
    char *name;
    char *role_text;
    char *hash;
    char *home;
    char *uid_text;
    u64 uid;

    if (line == CLKS_NULL || out_record == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_user_copy(tmp, sizeof(tmp), line);
    clks_user_trim(tmp);

    if (tmp[0] == '\0' || tmp[0] == '#') {
        return CLKS_FALSE;
    }

    cursor = tmp;
    name = clks_user_next_field(&cursor);
    role_text = clks_user_next_field(&cursor);
    hash = clks_user_next_field(&cursor);
    home = clks_user_next_field(&cursor);
    uid_text = (cursor != CLKS_NULL) ? cursor : CLKS_NULL;

    if (name == CLKS_NULL || role_text == CLKS_NULL || hash == CLKS_NULL || home == CLKS_NULL) {
        return CLKS_FALSE;
    }

    clks_user_trim(home);
    if (uid_text != CLKS_NULL) {
        clks_user_trim(uid_text);
    }
    if (clks_user_name_valid(name) == CLKS_FALSE || clks_strlen(hash) != CLKS_USER_HASH_HEX_LEN ||
        home[0] != '/') {
        return CLKS_FALSE;
    }

    uid = clks_user_uid_for_name(name);
    if (uid_text != CLKS_NULL && uid_text[0] != '\0' && clks_user_parse_u64_dec(uid_text, &uid) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_memset(out_record, 0, sizeof(*out_record));
    clks_user_copy(out_record->name, sizeof(out_record->name), name);
    clks_user_copy(out_record->hash, sizeof(out_record->hash), hash);
    clks_user_copy(out_record->home, sizeof(out_record->home), home);
    out_record->role =
        (clks_user_streq(role_text, "admin") == CLKS_TRUE || clks_user_streq(role_text, "root") == CLKS_TRUE)
            ? CLKS_USER_ROLE_ADMIN
            : CLKS_USER_ROLE_USER;
    out_record->uid = uid;
    return CLKS_TRUE;
}

static void clks_user_record_line(const struct clks_user_record *record, char *out, usize out_size) {
    if (out == CLKS_NULL || out_size == 0U) {
        return;
    }

    out[0] = '\0';
    if (record == CLKS_NULL) {
        return;
    }

    clks_user_append(out, out_size, record->name);
    clks_user_append(out, out_size, ":");
    clks_user_append(out, out_size, (record->role == CLKS_USER_ROLE_ADMIN) ? "admin" : "user");
    clks_user_append(out, out_size, ":");
    clks_user_append(out, out_size, record->hash);
    clks_user_append(out, out_size, ":");
    clks_user_append(out, out_size, record->home);
    clks_user_append(out, out_size, ":");
    clks_user_append_u64_dec(out, out_size, record->uid);
    clks_user_append(out, out_size, "\n");
}

static void clks_user_home_for(char *out_home, usize out_size, const char *name) {
    if (out_home == CLKS_NULL || out_size == 0U) {
        return;
    }

    out_home[0] = '\0';
    clks_user_append(out_home, out_size, "/home/");
    clks_user_append(out_home, out_size, name);
}

static clks_bool clks_user_find(const char *name, struct clks_user_record *out_record) {
    static char db[4096];
    u64 len = 0ULL;
    u64 start = 0ULL;
    u64 i;

    if (clks_user_name_valid(name) == CLKS_FALSE || clks_user_read_db(db, sizeof(db), &len) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    for (i = 0ULL; i <= len; i++) {
        if (i == len || db[i] == '\n') {
            char line[CLKS_USER_RECORD_MAX];
            u64 line_len = i - start;
            struct clks_user_record record;

            if (line_len >= sizeof(line)) {
                line_len = sizeof(line) - 1U;
            }
            clks_memcpy(line, db + start, (usize)line_len);
            line[line_len] = '\0';

            if (clks_user_parse_record(line, &record) == CLKS_TRUE) {
                if (clks_user_streq(record.name, name) == CLKS_TRUE) {
                    if (out_record != CLKS_NULL) {
                        *out_record = record;
                    }
                    return CLKS_TRUE;
                }
            }

            start = i + 1ULL;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_user_verify_password(const char *name, const char *password, struct clks_user_record *out_record) {
    struct clks_user_record record;
    char hash[CLKS_USER_HASH_HEX_LEN + 1U];

    if (password == CLKS_NULL || clks_user_find(name, &record) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_user_hash_password(password, hash);
    if (clks_user_streq(record.hash, hash) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (out_record != CLKS_NULL) {
        *out_record = record;
    }
    return CLKS_TRUE;
}

static void clks_user_public_from_record(const struct clks_user_record *record, u64 logged_in,
                                         struct clks_user_public_info *out_info) {
    if (record == CLKS_NULL || out_info == CLKS_NULL) {
        return;
    }

    clks_memset(out_info, 0, sizeof(*out_info));
    out_info->uid = record->uid;
    out_info->role = record->role;
    out_info->logged_in = logged_in;
    out_info->disk_login_required = (clks_user_disk_login_required == CLKS_TRUE) ? 1ULL : 0ULL;
    clks_user_copy(out_info->name, sizeof(out_info->name), record->name);
    clks_user_copy(out_info->home, sizeof(out_info->home), record->home);
}

static clks_bool clks_user_db_rewrite_with(const struct clks_user_record *replace_record, const char *remove_name) {
    static char db[4096];
    static char next_db[4096];
    u64 len = 0ULL;
    u64 next_len = 0ULL;
    u64 start = 0ULL;
    u64 i;
    clks_bool found = CLKS_FALSE;

    if (clks_user_read_db(db, sizeof(db), &len) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    next_db[0] = '\0';
    for (i = 0ULL; i <= len; i++) {
        if (i == len || db[i] == '\n') {
            char line[CLKS_USER_RECORD_MAX];
            char record_line[CLKS_USER_RECORD_MAX];
            u64 line_len = i - start;
            struct clks_user_record record;
            clks_bool emit_original = CLKS_TRUE;

            if (line_len >= sizeof(line)) {
                line_len = sizeof(line) - 1U;
            }
            clks_memcpy(line, db + start, (usize)line_len);
            line[line_len] = '\0';

            if (clks_user_parse_record(line, &record) == CLKS_TRUE) {
                if (remove_name != CLKS_NULL && clks_user_streq(record.name, remove_name) == CLKS_TRUE) {
                    found = CLKS_TRUE;
                    emit_original = CLKS_FALSE;
                } else if (replace_record != CLKS_NULL &&
                           clks_user_streq(record.name, replace_record->name) == CLKS_TRUE) {
                    found = CLKS_TRUE;
                    clks_user_record_line(replace_record, record_line, sizeof(record_line));
                    if (next_len + clks_strlen(record_line) + 1ULL >= sizeof(next_db)) {
                        return CLKS_FALSE;
                    }
                    clks_user_append(next_db, sizeof(next_db), record_line);
                    next_len += clks_strlen(record_line);
                    emit_original = CLKS_FALSE;
                }
            }

            if (emit_original == CLKS_TRUE && line[0] != '\0') {
                if (next_len + line_len + 2ULL >= sizeof(next_db)) {
                    return CLKS_FALSE;
                }
                clks_user_append(next_db, sizeof(next_db), line);
                clks_user_append(next_db, sizeof(next_db), "\n");
                next_len += line_len + 1ULL;
            }

            start = i + 1ULL;
        }
    }

    if (found == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return clks_fs_write_all(CLKS_USER_DB_PATH, next_db, clks_strlen(next_db));
}

static clks_bool clks_user_db_ensure(void) {
    struct clks_fs_node_info info;

    if (clks_fs_stat(CLKS_USER_DB_PATH, &info) == CLKS_TRUE && info.type == CLKS_FS_NODE_FILE) {
        return CLKS_TRUE;
    }

    return clks_fs_write_all(CLKS_USER_DB_PATH, &clks_user_empty_file, 0ULL);
}

void clks_user_init(void) {
    clks_user_disk_login_required = clks_user_is_disk_boot();
    clks_log(CLKS_LOG_INFO, "USER", "KERNEL USER SYSTEM ONLINE");
    clks_log_hex(CLKS_LOG_INFO, "USER", "DISK_LOGIN_REQUIRED",
                 (clks_user_disk_login_required == CLKS_TRUE) ? 1ULL : 0ULL);
}

clks_bool clks_user_is_disk_boot(void) {
    const char *mount_path;

    if (clks_disk_is_mounted() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    mount_path = clks_disk_mount_path();
    return clks_user_streq(mount_path, "/");
}

clks_bool clks_user_current_info(struct clks_user_public_info *out_info) {
    u64 uid = CLKS_USER_UID_NOBODY;
    u64 role = CLKS_USER_ROLE_USER;
    clks_bool logged_in = CLKS_FALSE;
    clks_bool disk_login_required = clks_user_disk_login_required;
    char name[CLKS_USER_NAME_MAX];
    char home[CLKS_USER_HOME_MAX];

    if (out_info == CLKS_NULL) {
        return CLKS_FALSE;
    }

    name[0] = '\0';
    home[0] = '\0';
    if (clks_exec_current_user_info(&uid, &role, name, sizeof(name), home, sizeof(home), &logged_in,
                                    &disk_login_required) == CLKS_FALSE) {
        disk_login_required = clks_user_disk_login_required;
    }

    if (clks_user_disk_login_required == CLKS_FALSE) {
        uid = CLKS_USER_UID_ROOT;
        role = CLKS_USER_ROLE_ADMIN;
        logged_in = CLKS_TRUE;
        disk_login_required = CLKS_FALSE;
        clks_user_copy(name, sizeof(name), "root");
        clks_user_copy(home, sizeof(home), "/");
    } else {
        disk_login_required = CLKS_TRUE;
        if (logged_in == CLKS_FALSE) {
            uid = CLKS_USER_UID_NOBODY;
            role = CLKS_USER_ROLE_USER;
            clks_user_copy(name, sizeof(name), "nobody");
            clks_user_copy(home, sizeof(home), "/");
        }
    }

    clks_memset(out_info, 0, sizeof(*out_info));
    out_info->uid = uid;
    out_info->role = role;
    out_info->logged_in = (logged_in == CLKS_TRUE) ? 1ULL : 0ULL;
    out_info->disk_login_required = (disk_login_required == CLKS_TRUE) ? 1ULL : 0ULL;
    clks_user_copy(out_info->name, sizeof(out_info->name), name);
    clks_user_copy(out_info->home, sizeof(out_info->home), home);
    return CLKS_TRUE;
}

clks_bool clks_user_current_is_admin(void) {
    struct clks_user_public_info info;

    if (clks_user_disk_login_required == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    if (clks_user_current_info(&info) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return (info.logged_in != 0ULL && info.role == CLKS_USER_ROLE_ADMIN) ? CLKS_TRUE : CLKS_FALSE;
}

clks_bool clks_user_login(const char *name, const char *password, struct clks_user_public_info *out_info) {
    struct clks_user_record record;

    if (clks_user_disk_login_required == CLKS_FALSE) {
        struct clks_user_record root_record;
        clks_memset(&root_record, 0, sizeof(root_record));
        root_record.uid = CLKS_USER_UID_ROOT;
        root_record.role = CLKS_USER_ROLE_ADMIN;
        clks_user_copy(root_record.name, sizeof(root_record.name), "root");
        clks_user_copy(root_record.home, sizeof(root_record.home), "/");
        (void)clks_exec_current_set_user(root_record.uid, root_record.role, root_record.name, root_record.home,
                                         CLKS_TRUE, CLKS_FALSE);
        clks_user_public_from_record(&root_record, 1ULL, out_info);
        return CLKS_TRUE;
    }

    if (clks_user_verify_password(name, password, &record) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_exec_current_set_user(record.uid, record.role, record.name, record.home, CLKS_TRUE, CLKS_TRUE) ==
        CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_user_public_from_record(&record, 1ULL, out_info);
    return CLKS_TRUE;
}

void clks_user_logout(void) {
    if (clks_user_disk_login_required == CLKS_FALSE) {
        (void)clks_exec_current_set_user(CLKS_USER_UID_ROOT, CLKS_USER_ROLE_ADMIN, "root", "/", CLKS_TRUE, CLKS_FALSE);
        return;
    }

    clks_exec_current_clear_user(CLKS_TRUE);
}

u64 clks_user_count(void) {
    static char db[4096];
    u64 len = 0ULL;
    u64 start = 0ULL;
    u64 i;
    u64 count = 0ULL;

    if (clks_user_read_db(db, sizeof(db), &len) == CLKS_FALSE) {
        return 0ULL;
    }

    for (i = 0ULL; i <= len; i++) {
        if (i == len || db[i] == '\n') {
            char line[CLKS_USER_RECORD_MAX];
            u64 line_len = i - start;
            struct clks_user_record record;

            if (line_len >= sizeof(line)) {
                line_len = sizeof(line) - 1U;
            }
            clks_memcpy(line, db + start, (usize)line_len);
            line[line_len] = '\0';

            if (clks_user_parse_record(line, &record) == CLKS_TRUE) {
                count++;
            }

            start = i + 1ULL;
        }
    }

    return count;
}

clks_bool clks_user_at(u64 index, struct clks_user_public_info *out_info) {
    static char db[4096];
    u64 len = 0ULL;
    u64 start = 0ULL;
    u64 i;
    u64 current = 0ULL;

    if (out_info == CLKS_NULL || clks_user_read_db(db, sizeof(db), &len) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    for (i = 0ULL; i <= len; i++) {
        if (i == len || db[i] == '\n') {
            char line[CLKS_USER_RECORD_MAX];
            u64 line_len = i - start;
            struct clks_user_record record;

            if (line_len >= sizeof(line)) {
                line_len = sizeof(line) - 1U;
            }
            clks_memcpy(line, db + start, (usize)line_len);
            line[line_len] = '\0';

            if (clks_user_parse_record(line, &record) == CLKS_TRUE) {
                if (current == index) {
                    clks_user_public_from_record(&record, 1ULL, out_info);
                    return CLKS_TRUE;
                }
                current++;
            }

            start = i + 1ULL;
        }
    }

    return CLKS_FALSE;
}

clks_bool clks_user_create(const char *name, const char *password, u64 role) {
    struct clks_user_record existing;
    struct clks_user_record record;
    struct clks_fs_node_info home_info;
    char line[CLKS_USER_RECORD_MAX];

    if (clks_user_current_is_admin() == CLKS_FALSE || clks_user_name_valid(name) == CLKS_FALSE || password == CLKS_NULL ||
        password[0] == '\0') {
        return CLKS_FALSE;
    }

    if (clks_user_db_ensure() == CLKS_FALSE || clks_user_find(name, &existing) == CLKS_TRUE) {
        return CLKS_FALSE;
    }

    clks_memset(&record, 0, sizeof(record));
    record.uid = clks_user_uid_for_name(name);
    record.role = (role == CLKS_USER_ROLE_ADMIN) ? CLKS_USER_ROLE_ADMIN : CLKS_USER_ROLE_USER;
    clks_user_copy(record.name, sizeof(record.name), name);
    clks_user_hash_password(password, record.hash);
    clks_user_home_for(record.home, sizeof(record.home), name);

    if (clks_fs_stat("/home", &home_info) == CLKS_FALSE) {
        if (clks_fs_mkdir("/home") == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    } else if (home_info.type != CLKS_FS_NODE_DIR) {
        return CLKS_FALSE;
    }

    if (clks_fs_mkdir(record.home) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_user_record_line(&record, line, sizeof(line));
    return clks_fs_append(CLKS_USER_DB_PATH, line, clks_strlen(line));
}

clks_bool clks_user_change_password(const char *name, const char *old_password, const char *new_password) {
    struct clks_user_record record;
    struct clks_user_public_info current;

    if (clks_user_name_valid(name) == CLKS_FALSE || new_password == CLKS_NULL || new_password[0] == '\0' ||
        clks_user_find(name, &record) == CLKS_FALSE || clks_user_current_info(&current) == CLKS_FALSE ||
        current.logged_in == 0ULL) {
        return CLKS_FALSE;
    }

    if (current.role != CLKS_USER_ROLE_ADMIN || clks_user_streq(current.name, name) == CLKS_TRUE) {
        if (clks_user_verify_password(name, old_password, CLKS_NULL) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
    }

    clks_user_hash_password(new_password, record.hash);
    return clks_user_db_rewrite_with(&record, CLKS_NULL);
}

clks_bool clks_user_set_role(const char *name, u64 role) {
    struct clks_user_record record;

    if (clks_user_current_is_admin() == CLKS_FALSE || clks_user_find(name, &record) == CLKS_FALSE ||
        clks_user_streq(name, "root") == CLKS_TRUE) {
        return CLKS_FALSE;
    }

    record.role = (role == CLKS_USER_ROLE_ADMIN) ? CLKS_USER_ROLE_ADMIN : CLKS_USER_ROLE_USER;
    return clks_user_db_rewrite_with(&record, CLKS_NULL);
}

clks_bool clks_user_remove(const char *name) {
    if (clks_user_current_is_admin() == CLKS_FALSE || clks_user_name_valid(name) == CLKS_FALSE ||
        clks_user_streq(name, "root") == CLKS_TRUE) {
        return CLKS_FALSE;
    }

    return clks_user_db_rewrite_with(CLKS_NULL, name);
}

clks_bool clks_user_path_read_allowed(const char *path) {
    struct clks_user_public_info info;
    char home_prefix[CLKS_USER_HOME_MAX + 2U];

    if (path == CLKS_NULL || path[0] != '/') {
        return CLKS_FALSE;
    }

    if (clks_user_disk_login_required == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    if (clks_user_current_info(&info) == CLKS_FALSE || info.logged_in == 0ULL) {
        return CLKS_FALSE;
    }

    if (info.role == CLKS_USER_ROLE_ADMIN || info.uid == CLKS_USER_UID_ROOT) {
        return CLKS_TRUE;
    }

    if (clks_user_streq(path, CLKS_USER_DB_PATH) == CLKS_TRUE) {
        return CLKS_FALSE;
    }

    if (clks_user_path_is_at_or_under(path, "/temp") == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    clks_user_copy(home_prefix, sizeof(home_prefix), info.home);
    clks_user_append(home_prefix, sizeof(home_prefix), "/");
    if (clks_user_streq(path, info.home) == CLKS_TRUE || clks_user_has_prefix(path, home_prefix) == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    if (clks_user_path_is_at_or_under(path, "/home") == CLKS_TRUE) {
        return (clks_user_streq(path, "/home") == CLKS_TRUE) ? CLKS_TRUE : CLKS_FALSE;
    }

    return CLKS_TRUE;
}

clks_bool clks_user_path_write_allowed(const char *path) {
    struct clks_user_public_info info;
    char home_prefix[CLKS_USER_HOME_MAX + 2U];

    if (path == CLKS_NULL || path[0] != '/') {
        return CLKS_FALSE;
    }

    if (clks_user_disk_login_required == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    if (clks_user_current_info(&info) == CLKS_FALSE || info.logged_in == 0ULL) {
        return CLKS_FALSE;
    }

    if (info.role == CLKS_USER_ROLE_ADMIN || info.uid == CLKS_USER_UID_ROOT) {
        return CLKS_TRUE;
    }

    if (clks_user_path_is_at_or_under(path, "/temp") == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    clks_user_copy(home_prefix, sizeof(home_prefix), info.home);
    clks_user_append(home_prefix, sizeof(home_prefix), "/");

    if (clks_user_streq(path, info.home) == CLKS_TRUE || clks_user_has_prefix(path, home_prefix) == CLKS_TRUE) {
        return CLKS_TRUE;
    }

    return CLKS_FALSE;
}

clks_bool clks_user_privileged_operation_allowed(void) {
    return clks_user_current_is_admin();
}
