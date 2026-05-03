static clks_bool clks_disk_bytes_range_valid(u64 offset, u64 size) {
    if (clks_disk_bytes == CLKS_NULL || clks_disk_bytes_len == 0ULL) {
        return CLKS_FALSE;
    }

    if (offset > clks_disk_bytes_len) {
        return CLKS_FALSE;
    }

    if (size > (clks_disk_bytes_len - offset)) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}

static u64 clks_disk_loaded_bitmap_required_bytes(u64 sector_count) {
    return (sector_count + 7ULL) / 8ULL;
}

static clks_bool clks_disk_loaded_test(u64 lba) {
    u64 byte_index;
    u8 bit_mask;

    if (clks_disk_loaded_bitmap == CLKS_NULL || clks_disk_loaded_bitmap_bytes == 0ULL) {
        return CLKS_FALSE;
    }

    byte_index = lba >> 3U;
    if (byte_index >= clks_disk_loaded_bitmap_bytes) {
        return CLKS_FALSE;
    }

    bit_mask = (u8)(1U << (u8)(lba & 7ULL));
    return ((clks_disk_loaded_bitmap[byte_index] & bit_mask) != 0U) ? CLKS_TRUE : CLKS_FALSE;
}

static void clks_disk_loaded_mark_range(u64 start_lba, u64 sector_count) {
    u64 lba;

    if (clks_disk_loaded_bitmap == CLKS_NULL || clks_disk_loaded_bitmap_bytes == 0ULL || sector_count == 0ULL) {
        return;
    }

    for (lba = start_lba; lba < start_lba + sector_count; lba++) {
        u64 byte_index = lba >> 3U;
        u8 bit_mask;

        if (byte_index >= clks_disk_loaded_bitmap_bytes) {
            break;
        }

        bit_mask = (u8)(1U << (u8)(lba & 7ULL));
        clks_disk_loaded_bitmap[byte_index] |= bit_mask;
    }
}

static clks_bool clks_disk_ensure_loaded_range(u64 offset, u64 size) {
    u64 start_lba;
    u64 end_lba;
    u64 lba;

    if (size == 0ULL) {
        return CLKS_TRUE;
    }

    if (clks_disk_bytes_range_valid(offset, size) == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    if (clks_disk_hw_backed == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    if (clks_disk_loaded_bitmap == CLKS_NULL || clks_disk_loaded_bitmap_bytes == 0ULL) {
        return CLKS_FALSE;
    }

    start_lba = offset / CLKS_DISK_SECTOR_SIZE;
    end_lba = (offset + size + (CLKS_DISK_SECTOR_SIZE - 1ULL)) / CLKS_DISK_SECTOR_SIZE;

    lba = start_lba;
    while (lba < end_lba) {
        if (clks_disk_loaded_test(lba) == CLKS_TRUE) {
            lba++;
            continue;
        }

        {
            u64 batch_start = lba;
            u16 batch_count;
            u8 *batch_ptr;

            while (lba < end_lba && clks_disk_loaded_test(lba) == CLKS_FALSE &&
                   (lba - batch_start) < (u64)CLKS_DISK_ATA_BATCH_SECTORS) {
                lba++;
            }

            batch_count = (u16)(lba - batch_start);
            if (batch_count == 0U) {
                continue;
            }

            batch_ptr = clks_disk_bytes + (usize)(batch_start * CLKS_DISK_SECTOR_SIZE);
            if (clks_disk_ata_read_sectors_hw(batch_start, batch_count, batch_ptr) == CLKS_FALSE) {
                return CLKS_FALSE;
            }
            clks_disk_loaded_mark_range(batch_start, (u64)batch_count);
        }
    }

    return CLKS_TRUE;
}

static clks_bool clks_disk_sync_sector_range(u64 start_lba, u64 sector_count) {
    u64 remaining;
    u64 lba;

    if (sector_count == 0ULL) {
        return CLKS_TRUE;
    }

    if (clks_disk_hw_backed == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    if (start_lba >= clks_disk_sector_total) {
        return CLKS_FALSE;
    }

    if (sector_count > (clks_disk_sector_total - start_lba)) {
        return CLKS_FALSE;
    }

    remaining = sector_count;
    lba = start_lba;
    while (remaining > 0ULL) {
        u16 batch = (remaining > (u64)CLKS_DISK_ATA_BATCH_SECTORS) ? (u16)CLKS_DISK_ATA_BATCH_SECTORS : (u16)remaining;
        const u8 *batch_ptr = clks_disk_bytes + (usize)(lba * CLKS_DISK_SECTOR_SIZE);
        if (clks_disk_ata_write_sectors_hw(lba, batch, batch_ptr) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        clks_disk_loaded_mark_range(lba, (u64)batch);
        lba += (u64)batch;
        remaining -= (u64)batch;
    }

    return clks_disk_ata_cache_flush_hw();
}

static clks_bool clks_disk_sync_sector_range_no_flush(u64 start_lba, u64 sector_count) {
    u64 remaining;
    u64 lba;

    if (sector_count == 0ULL) {
        return CLKS_TRUE;
    }

    if (clks_disk_hw_backed == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    if (start_lba >= clks_disk_sector_total) {
        return CLKS_FALSE;
    }

    if (sector_count > (clks_disk_sector_total - start_lba)) {
        return CLKS_FALSE;
    }

    remaining = sector_count;
    lba = start_lba;
    while (remaining > 0ULL) {
        u16 batch = (remaining > (u64)CLKS_DISK_ATA_BATCH_SECTORS) ? (u16)CLKS_DISK_ATA_BATCH_SECTORS : (u16)remaining;
        const u8 *batch_ptr = clks_disk_bytes + (usize)(lba * CLKS_DISK_SECTOR_SIZE);
        if (clks_disk_ata_write_sectors_hw(lba, batch, batch_ptr) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        clks_disk_loaded_mark_range(lba, (u64)batch);
        lba += (u64)batch;
        remaining -= (u64)batch;
    }

    return CLKS_TRUE;
}

static clks_bool clks_disk_flush_hw(void) {
    if (clks_disk_hw_backed == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    return clks_disk_ata_cache_flush_hw();
}

static clks_bool clks_disk_sync_bytes_to_hw(u64 used_bytes) {
    u64 sectors_to_sync;
    u64 remaining;
    u64 lba;

    if (clks_disk_hw_backed == CLKS_FALSE) {
        return CLKS_TRUE;
    }

    if (clks_disk_bytes == CLKS_NULL || clks_disk_sector_total == 0ULL) {
        return CLKS_FALSE;
    }

    if (used_bytes == 0ULL) {
        return CLKS_TRUE;
    }

    if (used_bytes > clks_disk_bytes_len) {
        used_bytes = clks_disk_bytes_len;
    }

    sectors_to_sync = (used_bytes + (CLKS_DISK_SECTOR_SIZE - 1ULL)) / CLKS_DISK_SECTOR_SIZE;
    if (sectors_to_sync > clks_disk_sector_total) {
        sectors_to_sync = clks_disk_sector_total;
    }

    remaining = sectors_to_sync;
    lba = 0ULL;
    while (remaining > 0ULL) {
        u16 batch = (remaining > (u64)CLKS_DISK_ATA_BATCH_SECTORS) ? (u16)CLKS_DISK_ATA_BATCH_SECTORS : (u16)remaining;
        const u8 *batch_ptr = clks_disk_bytes + (usize)(lba * CLKS_DISK_SECTOR_SIZE);
        if (clks_disk_ata_write_sectors_hw(lba, batch, batch_ptr) == CLKS_FALSE) {
            return CLKS_FALSE;
        }
        clks_disk_loaded_mark_range(lba, (u64)batch);
        lba += (u64)batch;
        remaining -= (u64)batch;
    }

    if (clks_disk_ata_cache_flush_hw() == CLKS_FALSE) {
        return CLKS_FALSE;
    }

    return CLKS_TRUE;
}
