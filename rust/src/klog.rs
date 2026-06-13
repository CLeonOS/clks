use core::slice;

const KLOG_CAP: usize = 512;
const KLOG_LINE_MAX: usize = 256;
const KLOG_TAG_MAX: usize = 32;

#[derive(Copy, Clone)]
struct KlogEntry {
    level: u8,
    tag: [u8; KLOG_TAG_MAX],
    line: [u8; KLOG_LINE_MAX],
}

impl KlogEntry {
    const fn empty() -> Self {
        Self {
            level: 0,
            tag: [0; KLOG_TAG_MAX],
            line: [0; KLOG_LINE_MAX],
        }
    }
}

static mut JOURNAL: [KlogEntry; KLOG_CAP] = [KlogEntry::empty(); KLOG_CAP];
static mut HEAD: usize = 0;
static mut COUNT: usize = 0;

unsafe fn c_strlen(ptr: *const u8, max_len: usize) -> usize {
    if ptr.is_null() {
        return 0;
    }

    let mut len = 0usize;
    while len < max_len && *ptr.add(len) != 0 {
        len += 1;
    }
    len
}

unsafe fn copy_cstr_to_array<const N: usize>(dst: &mut [u8; N], src: *const u8) {
    let len = c_strlen(src, N.saturating_sub(1));
    let bytes = if src.is_null() {
        &[][..]
    } else {
        slice::from_raw_parts(src, len)
    };

    let mut i = 0usize;
    while i < N {
        dst[i] = 0;
        i += 1;
    }

    i = 0;
    while i < bytes.len() && i + 1 < N {
        dst[i] = bytes[i];
        i += 1;
    }
}

unsafe fn copy_array_to_out(src: &[u8], out: *mut u8, out_size: u64) -> u32 {
    if out.is_null() || out_size == 0 {
        return 0;
    }

    let cap = out_size as usize;
    let mut len = 0usize;
    while len < src.len() && src[len] != 0 {
        len += 1;
    }

    let copy_len = if len + 1 > cap { cap.saturating_sub(1) } else { len };
    let mut i = 0usize;
    while i < copy_len {
        *out.add(i) = src[i];
        i += 1;
    }
    *out.add(copy_len) = 0;
    1
}

fn tag_matches(entry: &KlogEntry, tag: *const u8) -> bool {
    if tag.is_null() {
        return true;
    }

    unsafe {
        let tag_len = c_strlen(tag, KLOG_TAG_MAX - 1);
        let tag_bytes = slice::from_raw_parts(tag, tag_len);
        let mut entry_len = 0usize;
        while entry_len < KLOG_TAG_MAX && entry.tag[entry_len] != 0 {
            entry_len += 1;
        }
        &entry.tag[..entry_len] == tag_bytes
    }
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_klog_init() {
    HEAD = 0;
    COUNT = 0;
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_klog_push(level: u32, tag: *const u8, line: *const u8) -> u32 {
    if line.is_null() {
        return 0;
    }

    let slot = HEAD;
    JOURNAL[slot].level = if level > 255 { 255 } else { level as u8 };
    copy_cstr_to_array(&mut JOURNAL[slot].tag, tag);
    copy_cstr_to_array(&mut JOURNAL[slot].line, line);

    HEAD = (HEAD + 1) % KLOG_CAP;
    if COUNT < KLOG_CAP {
        COUNT += 1;
    }

    1
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_klog_count() -> u64 {
    COUNT as u64
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_klog_read(index_from_oldest: u64, out: *mut u8, out_size: u64) -> u32 {
    if index_from_oldest >= COUNT as u64 {
        if !out.is_null() && out_size > 0 {
            *out = 0;
        }
        return 0;
    }

    let oldest = (HEAD + KLOG_CAP - COUNT) % KLOG_CAP;
    let slot = (oldest + index_from_oldest as usize) % KLOG_CAP;
    copy_array_to_out(&JOURNAL[slot].line, out, out_size)
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_klog_count_filtered(min_level: u32, tag: *const u8) -> u64 {
    let mut matched = 0u64;
    let oldest = (HEAD + KLOG_CAP - COUNT) % KLOG_CAP;
    let mut i = 0usize;

    while i < COUNT {
        let slot = (oldest + i) % KLOG_CAP;
        let entry = &JOURNAL[slot];
        if entry.level as u32 >= min_level && tag_matches(entry, tag) {
            matched += 1;
        }
        i += 1;
    }

    matched
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_klog_read_filtered(
    min_level: u32,
    tag: *const u8,
    index_from_oldest_matching: u64,
    out: *mut u8,
    out_size: u64,
) -> u32 {
    if !out.is_null() && out_size > 0 {
        *out = 0;
    }

    let oldest = (HEAD + KLOG_CAP - COUNT) % KLOG_CAP;
    let mut seen = 0u64;
    let mut i = 0usize;

    while i < COUNT {
        let slot = (oldest + i) % KLOG_CAP;
        let entry = &JOURNAL[slot];
        if entry.level as u32 >= min_level && tag_matches(entry, tag) {
            if seen == index_from_oldest_matching {
                return copy_array_to_out(&entry.line, out, out_size);
            }
            seen += 1;
        }
        i += 1;
    }

    0
}
