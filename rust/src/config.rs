use core::slice;

#[repr(C)]
pub struct ClksRustKvEntry {
    pub key: *const u8,
    pub out_value: *mut u8,
    pub out_size: u64,
    pub found: u32,
    pub truncated: u32,
}

fn is_space(byte: u8) -> bool {
    matches!(byte, b' ' | b'\t' | b'\r' | b'\n')
}

unsafe fn c_strlen(ptr: *const u8) -> usize {
    if ptr.is_null() {
        return 0;
    }

    let mut len = 0usize;
    while *ptr.add(len) != 0 {
        len += 1;
    }
    len
}

unsafe fn c_str_bytes<'a>(ptr: *const u8) -> &'a [u8] {
    let len = c_strlen(ptr);
    slice::from_raw_parts(ptr, len)
}

fn trim_range(data: &[u8], mut start: usize, mut end: usize) -> (usize, usize) {
    while start < end && is_space(data[start]) {
        start += 1;
    }
    while end > start && is_space(data[end - 1]) {
        end -= 1;
    }
    (start, end)
}

unsafe fn copy_bytes_trimmed(src: &[u8], out: *mut u8, out_size: u64) -> u32 {
    if out.is_null() || out_size == 0 {
        return 1;
    }

    let (start, end) = trim_range(src, 0, src.len());
    let value = &src[start..end];
    let cap = out_size as usize;
    let copy_len = if value.len() + 1 > cap {
        cap.saturating_sub(1)
    } else {
        value.len()
    };

    let mut i = 0usize;
    while i < copy_len {
        *out.add(i) = value[i];
        i += 1;
    }
    *out.add(copy_len) = 0;

    if copy_len < value.len() { 1 } else { 0 }
}

fn key_equals(data: &[u8], key_start: usize, key_end: usize, key: &[u8]) -> bool {
    data.get(key_start..key_end) == Some(key)
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_trim_copy(data: *const u8, data_len: u64, out: *mut u8, out_size: u64) -> u32 {
    if data.is_null() || out.is_null() || out_size == 0 {
        return 0;
    }

    let input = slice::from_raw_parts(data, data_len as usize);
    copy_bytes_trimmed(input, out, out_size);
    1
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_parse_key_values(
    data: *const u8,
    data_len: u64,
    entries: *mut ClksRustKvEntry,
    entry_count: u64,
) -> u32 {
    if data.is_null() || entries.is_null() {
        return 0;
    }

    let input = slice::from_raw_parts(data, data_len as usize);
    let entries_slice = slice::from_raw_parts_mut(entries, entry_count as usize);

    for entry in entries_slice.iter_mut() {
        entry.found = 0;
        entry.truncated = 0;
        if !entry.out_value.is_null() && entry.out_size > 0 {
            *entry.out_value = 0;
        }
    }

    let mut pos = 0usize;
    while pos < input.len() {
        let line_start = pos;
        while pos < input.len() && input[pos] != b'\n' && input[pos] != b'\r' {
            pos += 1;
        }
        let line_end = pos;
        while pos < input.len() && (input[pos] == b'\n' || input[pos] == b'\r') {
            pos += 1;
        }

        let (key_start, mut trimmed_end) = trim_range(input, line_start, line_end);
        if key_start >= trimmed_end || input[key_start] == b'#' || input[key_start] == b';' {
            continue;
        }

        let mut eq = key_start;
        while eq < trimmed_end && input[eq] != b'=' {
            eq += 1;
        }
        if eq >= trimmed_end || input[eq] != b'=' {
            continue;
        }

        while trimmed_end > eq + 1 && is_space(input[trimmed_end - 1]) {
            trimmed_end -= 1;
        }

        let mut key_end = eq;
        while key_end > key_start && is_space(input[key_end - 1]) {
            key_end -= 1;
        }

        let value_start_raw = eq + 1;
        let (value_start, value_end) = trim_range(input, value_start_raw, trimmed_end);
        let value = &input[value_start..value_end];

        for entry in entries_slice.iter_mut() {
            if entry.key.is_null() || entry.out_value.is_null() || entry.out_size == 0 {
                continue;
            }

            let key = c_str_bytes(entry.key);
            if key_equals(input, key_start, key_end, key) {
                entry.found = 1;
                entry.truncated = copy_bytes_trimmed(value, entry.out_value, entry.out_size);
                break;
            }
        }
    }

    1
}

fn token_end(cmdline: &[u8], mut pos: usize) -> usize {
    while pos < cmdline.len() && cmdline[pos] != b' ' && cmdline[pos] != b'\t' {
        pos += 1;
    }
    pos
}

fn boolish_enabled(value: &[u8]) -> bool {
    if value.is_empty() {
        return false;
    }
    matches!(value[0], b'1' | b'y' | b'Y' | b't' | b'T')
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_cmdline_flag_enabled(cmdline: *const u8, name: *const u8) -> u32 {
    if cmdline.is_null() || name.is_null() {
        return 0;
    }

    let cmdline = c_str_bytes(cmdline);
    let name = c_str_bytes(name);
    if name.is_empty() {
        return 0;
    }

    let mut pos = 0usize;
    while pos < cmdline.len() {
        while pos < cmdline.len() && (cmdline[pos] == b' ' || cmdline[pos] == b'\t') {
            pos += 1;
        }
        let end = token_end(cmdline, pos);
        if end == pos {
            break;
        }

        let token = &cmdline[pos..end];
        if token == name {
            return 1;
        }
        if token.len() > name.len() && token.starts_with(name) && token[name.len()] == b'=' {
            return if boolish_enabled(&token[name.len() + 1..]) { 1 } else { 0 };
        }

        pos = end;
    }

    0
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_cmdline_get_value(
    cmdline: *const u8,
    name: *const u8,
    out: *mut u8,
    out_size: u64,
) -> u32 {
    if cmdline.is_null() || name.is_null() || out.is_null() || out_size == 0 {
        return 0;
    }

    *out = 0;
    let cmdline = c_str_bytes(cmdline);
    let name = c_str_bytes(name);
    if name.is_empty() {
        return 0;
    }

    let mut pos = 0usize;
    while pos < cmdline.len() {
        while pos < cmdline.len() && (cmdline[pos] == b' ' || cmdline[pos] == b'\t') {
            pos += 1;
        }
        let end = token_end(cmdline, pos);
        if end == pos {
            break;
        }

        let token = &cmdline[pos..end];
        if token.len() > name.len() && token.starts_with(name) && token[name.len()] == b'=' {
            let value = &token[name.len() + 1..];
            copy_bytes_trimmed(value, out, out_size);
            return if value.is_empty() { 0 } else { 1 };
        }

        pos = end;
    }

    0
}
