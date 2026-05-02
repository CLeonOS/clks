static u16 clks_disk_read_u16(const u8 *ptr) {
    return (u16)((u16)ptr[0] | ((u16)ptr[1] << 8U));
}

static u32 clks_disk_read_u32(const u8 *ptr) {
    return (u32)((u32)ptr[0] | ((u32)ptr[1] << 8U) | ((u32)ptr[2] << 16U) | ((u32)ptr[3] << 24U));
}

static void clks_disk_write_u16(u8 *ptr, u16 value) {
    ptr[0] = (u8)(value & 0xFFU);
    ptr[1] = (u8)((value >> 8U) & 0xFFU);
}

static void clks_disk_write_u32(u8 *ptr, u32 value) {
    ptr[0] = (u8)(value & 0xFFU);
    ptr[1] = (u8)((value >> 8U) & 0xFFU);
    ptr[2] = (u8)((value >> 16U) & 0xFFU);
    ptr[3] = (u8)((value >> 24U) & 0xFFU);
}
