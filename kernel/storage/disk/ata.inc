#if defined(CLKS_ARCH_X86_64)
static inline u16 clks_disk_ata_reg_data(void) {
    return (u16)(clks_disk_ata_io_base + 0U);
}

static inline u16 clks_disk_ata_reg_sector_count(void) {
    return (u16)(clks_disk_ata_io_base + 2U);
}

static inline u16 clks_disk_ata_reg_lba_low(void) {
    return (u16)(clks_disk_ata_io_base + 3U);
}

static inline u16 clks_disk_ata_reg_lba_mid(void) {
    return (u16)(clks_disk_ata_io_base + 4U);
}

static inline u16 clks_disk_ata_reg_lba_high(void) {
    return (u16)(clks_disk_ata_io_base + 5U);
}

static inline u16 clks_disk_ata_reg_drive(void) {
    return (u16)(clks_disk_ata_io_base + 6U);
}

static inline u16 clks_disk_ata_reg_status(void) {
    return (u16)(clks_disk_ata_io_base + 7U);
}

static inline u16 clks_disk_ata_reg_command(void) {
    return (u16)(clks_disk_ata_io_base + 7U);
}

static inline u16 clks_disk_ata_reg_alt_status(void) {
    return (u16)(clks_disk_ata_ctrl_base + 0U);
}

static void clks_disk_ata_select_device(u16 io_base, u16 ctrl_base, u8 drive_select, u8 drive_lba) {
    clks_disk_ata_io_base = io_base;
    clks_disk_ata_ctrl_base = ctrl_base;
    clks_disk_ata_drive_select = drive_select;
    clks_disk_ata_drive_lba = drive_lba;
}

static inline void clks_disk_ata_outb(u16 port, u8 value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 clks_disk_ata_inb(u16 port) {
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void clks_disk_ata_outw(u16 port, u16 value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline u16 clks_disk_ata_inw(u16 port) {
    u16 value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void clks_disk_ata_delay_400ns(void) {
    (void)clks_disk_ata_inb(clks_disk_ata_reg_alt_status());
    (void)clks_disk_ata_inb(clks_disk_ata_reg_alt_status());
    (void)clks_disk_ata_inb(clks_disk_ata_reg_alt_status());
    (void)clks_disk_ata_inb(clks_disk_ata_reg_alt_status());
}

static clks_bool clks_disk_ata_wait_not_busy(void) {
    u32 i;

    for (i = 0U; i < 1000000U; i++) {
        u8 status = clks_disk_ata_inb(clks_disk_ata_reg_status());

        if ((status & CLKS_DISK_ATA_STATUS_BSY) == 0U) {
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_wait_drq_ready(void) {
    u32 i;

    for (i = 0U; i < 1000000U; i++) {
        u8 status = clks_disk_ata_inb(clks_disk_ata_reg_status());

        if ((status & CLKS_DISK_ATA_STATUS_BSY) != 0U) {
            continue;
        }

        if ((status & (CLKS_DISK_ATA_STATUS_ERR | CLKS_DISK_ATA_STATUS_DF)) != 0U) {
            return CLKS_FALSE;
        }

        if ((status & CLKS_DISK_ATA_STATUS_DRQ) != 0U) {
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_wait_ready_no_drq(void) {
    u32 i;

    for (i = 0U; i < 1000000U; i++) {
        u8 status = clks_disk_ata_inb(clks_disk_ata_reg_status());

        if ((status & CLKS_DISK_ATA_STATUS_BSY) != 0U) {
            continue;
        }

        if ((status & (CLKS_DISK_ATA_STATUS_ERR | CLKS_DISK_ATA_STATUS_DF)) != 0U) {
            return CLKS_FALSE;
        }

        if ((status & CLKS_DISK_ATA_STATUS_DRDY) != 0U) {
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_identify(u64 *out_sector_total) {
    u16 identify_words[256];
    u32 i;
    u64 sector_total;
    u8 status;
    u8 sig_mid;
    u8 sig_high;

    if (out_sector_total == CLKS_NULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_ata_wait_not_busy() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_disk_ata_outb(clks_disk_ata_reg_drive(), clks_disk_ata_drive_select);
    clks_disk_ata_delay_400ns();

    clks_disk_ata_outb(clks_disk_ata_reg_sector_count(), 0U);
    clks_disk_ata_outb(clks_disk_ata_reg_lba_low(), 0U);
    clks_disk_ata_outb(clks_disk_ata_reg_lba_mid(), 0U);
    clks_disk_ata_outb(clks_disk_ata_reg_lba_high(), 0U);
    clks_disk_ata_outb(clks_disk_ata_reg_command(), CLKS_DISK_ATA_CMD_IDENTIFY);

    status = clks_disk_ata_inb(clks_disk_ata_reg_status());
    if (status == 0U || status == 0xFFU) {
        return CLKS_FALSE;
    }

    if (clks_disk_ata_wait_not_busy() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_disk_ata_delay_400ns();
    sig_mid = clks_disk_ata_inb(clks_disk_ata_reg_lba_mid());
    sig_high = clks_disk_ata_inb(clks_disk_ata_reg_lba_high());
    if (sig_mid != 0U || sig_high != 0U) {
        return CLKS_FALSE;
    }

    if (clks_disk_ata_wait_drq_ready() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < 256U; i++) {
        identify_words[i] = clks_disk_ata_inw(clks_disk_ata_reg_data());
    }

    sector_total = (u64)identify_words[60] | ((u64)identify_words[61] << 16U);
    if (sector_total == 0ULL) {
        /* Some devices report 28-bit size as 0 and only provide 48-bit LBA size. */
        sector_total = (u64)identify_words[100] | ((u64)identify_words[101] << 16U) |
                       ((u64)identify_words[102] << 32U) | ((u64)identify_words[103] << 48U);
    }

    if (sector_total == 0ULL) {
        return CLKS_FALSE;
    }

    *out_sector_total = sector_total;
    return CLKS_TRUE;
}

static clks_bool clks_disk_ata_probe(u64 *out_sector_total) {
    static const struct {
        u16 io_base;
        u16 ctrl_base;
        u8 drive_select;
        u8 drive_lba;
    } probes[] = {
        {CLKS_DISK_ATA_PRIMARY_IO_BASE, CLKS_DISK_ATA_PRIMARY_CTRL_BASE, CLKS_DISK_ATA_DRIVE_MASTER,
         CLKS_DISK_ATA_DRIVE_LBA_MASTER},
        {CLKS_DISK_ATA_PRIMARY_IO_BASE, CLKS_DISK_ATA_PRIMARY_CTRL_BASE, CLKS_DISK_ATA_DRIVE_SLAVE,
         CLKS_DISK_ATA_DRIVE_LBA_SLAVE},
        {CLKS_DISK_ATA_SECONDARY_IO_BASE, CLKS_DISK_ATA_SECONDARY_CTRL_BASE, CLKS_DISK_ATA_DRIVE_MASTER,
         CLKS_DISK_ATA_DRIVE_LBA_MASTER},
        {CLKS_DISK_ATA_SECONDARY_IO_BASE, CLKS_DISK_ATA_SECONDARY_CTRL_BASE, CLKS_DISK_ATA_DRIVE_SLAVE,
         CLKS_DISK_ATA_DRIVE_LBA_SLAVE},
    };
    u32 i;

    if (out_sector_total == CLKS_NULL) {
        return CLKS_FALSE;
    }

    for (i = 0U; i < (u32)(sizeof(probes) / sizeof(probes[0])); i++) {
        u64 sectors = 0ULL;

        clks_disk_ata_select_device(probes[i].io_base, probes[i].ctrl_base, probes[i].drive_select,
                                    probes[i].drive_lba);
        if (clks_disk_ata_identify(&sectors) == CLKS_TRUE && sectors != 0ULL) {
            *out_sector_total = sectors;
            clks_log(CLKS_LOG_INFO, "DISK", "ATA DISK DETECTED");
            clks_log_hex(CLKS_LOG_INFO, "DISK", "ATA_IO_BASE", probes[i].io_base);
            clks_log_hex(CLKS_LOG_INFO, "DISK", "ATA_CTRL_BASE", probes[i].ctrl_base);
            clks_log_hex(CLKS_LOG_INFO, "DISK", "ATA_DRIVE", probes[i].drive_select);
            return CLKS_TRUE;
        }
    }

    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_read_sectors_hw(u64 lba, u16 sector_count, void *out_buffer) {
    u8 *out = (u8 *)out_buffer;
    u32 sector_index;
    u32 i;
    u8 status;

    if (out_buffer == CLKS_NULL || sector_count == 0U || sector_count > 255U || lba > 0x0FFFFFFFULL) {
        return CLKS_FALSE;
    }

    if (lba + (u64)sector_count - 1ULL > 0x0FFFFFFFULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_ata_wait_not_busy() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_disk_ata_outb(clks_disk_ata_reg_drive(), (u8)(clks_disk_ata_drive_lba | ((u8)((lba >> 24U) & 0x0FU))));
    clks_disk_ata_outb(clks_disk_ata_reg_sector_count(), (u8)sector_count);
    clks_disk_ata_outb(clks_disk_ata_reg_lba_low(), (u8)(lba & 0xFFULL));
    clks_disk_ata_outb(clks_disk_ata_reg_lba_mid(), (u8)((lba >> 8U) & 0xFFULL));
    clks_disk_ata_outb(clks_disk_ata_reg_lba_high(), (u8)((lba >> 16U) & 0xFFULL));
    clks_disk_ata_outb(clks_disk_ata_reg_command(), CLKS_DISK_ATA_CMD_READ_SECTORS);

    for (sector_index = 0U; sector_index < (u32)sector_count; sector_index++) {
        if (clks_disk_ata_wait_drq_ready() == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        for (i = 0U; i < 256U; i++) {
            u16 word = clks_disk_ata_inw(clks_disk_ata_reg_data());
            usize offset = ((usize)sector_index * (usize)CLKS_DISK_SECTOR_SIZE) + ((usize)i * 2U);
            out[offset + 0U] = (u8)(word & 0x00FFU);
            out[offset + 1U] = (u8)((word >> 8U) & 0x00FFU);
        }
    }

    clks_disk_ata_delay_400ns();
    status = clks_disk_ata_inb(clks_disk_ata_reg_status());
    if ((status & (CLKS_DISK_ATA_STATUS_ERR | CLKS_DISK_ATA_STATUS_DF)) != 0U) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static clks_bool clks_disk_ata_read_sector_hw(u64 lba, void *out_sector) {
    return clks_disk_ata_read_sectors_hw(lba, 1U, out_sector);
}

static clks_bool clks_disk_ata_write_sectors_hw(u64 lba, u16 sector_count, const void *buffer) {
    const u8 *src = (const u8 *)buffer;
    u32 sector_index;
    u32 i;

    if (buffer == CLKS_NULL || sector_count == 0U || sector_count > 255U || lba > 0x0FFFFFFFULL) {
        return CLKS_FALSE;
    }

    if (lba + (u64)sector_count - 1ULL > 0x0FFFFFFFULL) {
        return CLKS_FALSE;
    }

    if (clks_disk_ata_wait_not_busy() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_disk_ata_outb(clks_disk_ata_reg_drive(), (u8)(clks_disk_ata_drive_lba | ((u8)((lba >> 24U) & 0x0FU))));
    clks_disk_ata_outb(clks_disk_ata_reg_sector_count(), (u8)sector_count);
    clks_disk_ata_outb(clks_disk_ata_reg_lba_low(), (u8)(lba & 0xFFULL));
    clks_disk_ata_outb(clks_disk_ata_reg_lba_mid(), (u8)((lba >> 8U) & 0xFFULL));
    clks_disk_ata_outb(clks_disk_ata_reg_lba_high(), (u8)((lba >> 16U) & 0xFFULL));
    clks_disk_ata_outb(clks_disk_ata_reg_command(), CLKS_DISK_ATA_CMD_WRITE_SECTORS);

    for (sector_index = 0U; sector_index < (u32)sector_count; sector_index++) {
        if (clks_disk_ata_wait_drq_ready() == CLKS_FALSE) {
            return CLKS_FALSE;
        }

        for (i = 0U; i < 256U; i++) {
            usize offset = ((usize)sector_index * (usize)CLKS_DISK_SECTOR_SIZE) + ((usize)i * 2U);
            u16 word = (u16)src[offset + 0U] | ((u16)src[offset + 1U] << 8U);
            clks_disk_ata_outw(clks_disk_ata_reg_data(), word);
        }
    }

    clks_disk_ata_delay_400ns();
    if (clks_disk_ata_wait_ready_no_drq() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static clks_bool clks_disk_ata_write_sector_hw(u64 lba, const void *sector_data) {
    return clks_disk_ata_write_sectors_hw(lba, 1U, sector_data);
}

static clks_bool clks_disk_ata_cache_flush_hw(void) {
    if (clks_disk_ata_wait_not_busy() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    clks_disk_ata_outb(clks_disk_ata_reg_command(), CLKS_DISK_ATA_CMD_CACHE_FLUSH);
    return clks_disk_ata_wait_ready_no_drq();
}
#else
static clks_bool clks_disk_ata_identify(u64 *out_sector_total) {
    (void)out_sector_total;
    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_probe(u64 *out_sector_total) {
    return clks_disk_ata_identify(out_sector_total);
}

static clks_bool clks_disk_ata_read_sector_hw(u64 lba, void *out_sector) {
    (void)lba;
    (void)out_sector;
    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_read_sectors_hw(u64 lba, u16 sector_count, void *out_buffer) {
    (void)lba;
    (void)sector_count;
    (void)out_buffer;
    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_write_sector_hw(u64 lba, const void *sector_data) {
    (void)lba;
    (void)sector_data;
    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_write_sectors_hw(u64 lba, u16 sector_count, const void *buffer) {
    (void)lba;
    (void)sector_count;
    (void)buffer;
    return CLKS_FALSE;
}

static clks_bool clks_disk_ata_cache_flush_hw(void) {
    return CLKS_FALSE;
}
#endif
