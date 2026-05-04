#include <clks/fs.h>
#include <clks/locale.h>
#include <clks/log.h>
#include <clks/string.h>
#include <clks/types.h>

static char clks_locale_value[CLKS_LOCALE_MAX] = CLKS_LOCALE_DEFAULT;

static void clks_locale_copy(char *dst, usize dst_size, const char *src) {
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

static void clks_locale_trim_line(char *text) {
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

clks_bool clks_locale_is_valid(const char *locale) {
    usize i;
    usize len;

    if (locale == CLKS_NULL || locale[0] == '\0') {
        return CLKS_FALSE;
    }

    len = clks_strlen(locale);
    if (len >= CLKS_LOCALE_MAX) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < len; i++) {
        char ch = locale[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' ||
            ch == '-' || ch == '.') {
            continue;
        }
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

const char *clks_locale_current(void) {
    return clks_locale_value;
}

clks_bool clks_locale_set(const char *locale, clks_bool persist) {
    char payload[CLKS_LOCALE_MAX + 1U];
    usize len;

    if (clks_locale_is_valid(locale) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_locale_copy(clks_locale_value, sizeof(clks_locale_value), locale);

    if (persist == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    len = clks_strlen(clks_locale_value);
    clks_memcpy(payload, clks_locale_value, len);
    payload[len] = '\n';
    payload[len + 1U] = '\0';

    return clks_fs_write_all(CLKS_LOCALE_CONFIG_PATH, payload, (u64)(len + 1U));
}

void clks_locale_init(void) {
    const char *data;
    u64 size = 0ULL;
    char candidate[CLKS_LOCALE_MAX];
    u64 copy_len;

    clks_locale_copy(clks_locale_value, sizeof(clks_locale_value), CLKS_LOCALE_DEFAULT);

    data = (const char *)clks_fs_read_all(CLKS_LOCALE_CONFIG_PATH, &size);
    if (data != CLKS_NULL && size > 0ULL) {
        copy_len = size;
        if (copy_len >= (u64)sizeof(candidate)) {
            copy_len = (u64)sizeof(candidate) - 1ULL;
        }

        clks_memcpy(candidate, data, (usize)copy_len);
        candidate[copy_len] = '\0';
        clks_locale_trim_line(candidate);

        if (clks_locale_is_valid(candidate) == CLKS_TRUE) {
            clks_locale_copy(clks_locale_value, sizeof(clks_locale_value), candidate);
        }
    } else {
        (void)clks_locale_set(CLKS_LOCALE_DEFAULT, CLKS_TRUE);
    }

    clks_log(CLKS_LOG_INFO, "LOCALE", clks_locale_value);
}
