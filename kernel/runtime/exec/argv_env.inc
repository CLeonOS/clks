static void clks_exec_clear_items(char items[][CLKS_EXEC_ITEM_MAX], u32 max_count) {
    u32 i;

    for (i = 0U; i < max_count; i++) {
        items[i][0] = '\0';
    }
}

static u32 clks_exec_parse_whitespace_items(const char *line, char items[][CLKS_EXEC_ITEM_MAX], u32 max_count) {
    u32 count = 0U;
    usize i = 0U;

    if (line == CLKS_NULL || items == CLKS_NULL || max_count == 0U) {
        return 0U;
    }

    while (line[i] != '\0') {
        usize p = 0U;

        while (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n') {
            i++;
        }

        if (line[i] == '\0') {
            break;
        }

        if (count >= max_count) {
            break;
        }

        while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '\n') {
            if (p + 1U < CLKS_EXEC_ITEM_MAX) {
                items[count][p++] = line[i];
            }

            i++;
        }

        items[count][p] = '\0';
        count++;
    }

    return count;
}

static u32 clks_exec_parse_env_items(const char *line, char items[][CLKS_EXEC_ITEM_MAX], u32 max_count) {
    u32 count = 0U;
    usize i = 0U;

    if (line == CLKS_NULL || items == CLKS_NULL || max_count == 0U) {
        return 0U;
    }

    while (line[i] != '\0') {
        usize p = 0U;
        usize end_trim = 0U;

        while (line[i] == ' ' || line[i] == '\t' || line[i] == ';' || line[i] == '\r' || line[i] == '\n') {
            i++;
        }

        if (line[i] == '\0') {
            break;
        }

        if (count >= max_count) {
            break;
        }

        while (line[i] != '\0' && line[i] != ';' && line[i] != '\r' && line[i] != '\n') {
            if (p + 1U < CLKS_EXEC_ITEM_MAX) {
                items[count][p++] = line[i];
            }
            i++;
        }

        while (p > 0U && (items[count][p - 1U] == ' ' || items[count][p - 1U] == '\t')) {
            p--;
        }

        end_trim = p;
        items[count][end_trim] = '\0';

        if (end_trim > 0U) {
            count++;
        }
    }

    return count;
}

