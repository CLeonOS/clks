static void clks_exec_serial_write_hex64(u64 value) {
    int nibble;

    clks_serial_write("0X");

    for (nibble = 15; nibble >= 0; nibble--) {
        u8 current = (u8)((value >> (nibble * 4)) & 0x0FULL);
        char out = (current < 10U) ? (char)('0' + current) : (char)('A' + (current - 10U));
        clks_serial_write_char(out);
    }
}

static void clks_exec_log_info_serial(const char *message) {
    /* Serial logs stay because they save our ass when TTY is dead. */
    if (CLKS_CFG_EXEC_SERIAL_LOG == 0) {
        (void)message;
        return;
    }

    clks_serial_write("[INFO][EXEC] ");

    if (message != CLKS_NULL) {
        clks_serial_write(message);
    }

    clks_serial_write("\n");
}

static void clks_exec_log_hex_serial(const char *label, u64 value) {
    if (CLKS_CFG_EXEC_SERIAL_LOG == 0) {
        (void)label;
        (void)value;
        return;
    }

    clks_serial_write("[INFO][EXEC] ");

    if (label != CLKS_NULL) {
        clks_serial_write(label);
    }

    clks_serial_write(": ");
    clks_exec_serial_write_hex64(value);
    clks_serial_write("\n");
}

static void clks_exec_log_context_switch_serial(const char *message) {
    if (CLKS_CFG_EXEC_SERIAL_LOG == 0 || CLKS_CFG_EXEC_CONTEXT_SWITCH_LOG == 0) {
        (void)message;
        return;
    }

    clks_exec_log_info_serial(message);
}

static void clks_exec_log_context_switch_hex_serial(const char *label, u64 value) {
    if (CLKS_CFG_EXEC_SERIAL_LOG == 0 || CLKS_CFG_EXEC_CONTEXT_SWITCH_LOG == 0) {
        (void)label;
        (void)value;
        return;
    }

    clks_exec_log_hex_serial(label, value);
}
