use core::slice;

pub const UTF8_STATUS_NEED_MORE: u32 = 0;
pub const UTF8_STATUS_CODEPOINT: u32 = 1;
pub const UTF8_STATUS_REPLACEMENT: u32 = 2;
const REPLACEMENT: u32 = 0xFFFD;

#[repr(C)]
pub struct ClksRustUtf8State {
    pub codepoint: u32,
    pub remaining: u8,
    pub expected: u8,
}

fn is_cont(byte: u8) -> bool {
    (byte & 0xC0) == 0x80
}

fn valid_finished(codepoint: u32, expected: u8) -> bool {
    !((expected == 2 && codepoint < 0x80)
        || (expected == 3 && codepoint < 0x800)
        || (expected == 4 && codepoint < 0x10000)
        || codepoint > 0x10FFFF
        || (0xD800..=0xDFFF).contains(&codepoint))
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_utf8_state_reset(state: *mut ClksRustUtf8State) {
    if state.is_null() {
        return;
    }
    (*state).codepoint = 0;
    (*state).remaining = 0;
    (*state).expected = 0;
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_utf8_decode_byte(
    state: *mut ClksRustUtf8State,
    byte: u8,
    out_codepoint: *mut u32,
) -> u32 {
    if state.is_null() || out_codepoint.is_null() {
        return UTF8_STATUS_REPLACEMENT;
    }

    if (*state).remaining == 0 {
        if byte < 0x80 {
            *out_codepoint = byte as u32;
            return UTF8_STATUS_CODEPOINT;
        }

        if (byte & 0xE0) == 0xC0 {
            (*state).codepoint = (byte & 0x1F) as u32;
            (*state).remaining = 1;
            (*state).expected = 2;
            return UTF8_STATUS_NEED_MORE;
        }

        if (byte & 0xF0) == 0xE0 {
            (*state).codepoint = (byte & 0x0F) as u32;
            (*state).remaining = 2;
            (*state).expected = 3;
            return UTF8_STATUS_NEED_MORE;
        }

        if (byte & 0xF8) == 0xF0 {
            (*state).codepoint = (byte & 0x07) as u32;
            (*state).remaining = 3;
            (*state).expected = 4;
            return UTF8_STATUS_NEED_MORE;
        }

        *out_codepoint = REPLACEMENT;
        return UTF8_STATUS_REPLACEMENT;
    }

    if !is_cont(byte) {
        clks_rust_utf8_state_reset(state);
        *out_codepoint = REPLACEMENT;
        return UTF8_STATUS_REPLACEMENT;
    }

    (*state).codepoint = ((*state).codepoint << 6) | ((byte & 0x3F) as u32);
    (*state).remaining -= 1;

    if (*state).remaining != 0 {
        return UTF8_STATUS_NEED_MORE;
    }

    let codepoint = (*state).codepoint;
    let expected = (*state).expected;
    clks_rust_utf8_state_reset(state);

    if valid_finished(codepoint, expected) {
        *out_codepoint = codepoint;
        UTF8_STATUS_CODEPOINT
    } else {
        *out_codepoint = REPLACEMENT;
        UTF8_STATUS_REPLACEMENT
    }
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_utf8_next(
    text: *const u8,
    text_len: u64,
    io_index: *mut u64,
    out_codepoint: *mut u32,
) -> u32 {
    clks_rust_utf8_next_impl(text, text_len, io_index, out_codepoint, false)
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_utf8_next_strict(
    text: *const u8,
    text_len: u64,
    io_index: *mut u64,
    out_codepoint: *mut u32,
) -> u32 {
    clks_rust_utf8_next_impl(text, text_len, io_index, out_codepoint, true)
}

unsafe fn clks_rust_utf8_next_impl(
    text: *const u8,
    text_len: u64,
    io_index: *mut u64,
    out_codepoint: *mut u32,
    strict: bool,
) -> u32 {
    if text.is_null() || io_index.is_null() || out_codepoint.is_null() || *io_index >= text_len {
        return 0;
    }

    let input = slice::from_raw_parts(text, text_len as usize);
    let mut index = *io_index as usize;
    let b0 = input[index];
    index += 1;

    if b0 < 0x80 {
        *io_index = index as u64;
        *out_codepoint = b0 as u32;
        return 1;
    }

    let (mut value, need, expected) = if (b0 & 0xE0) == 0xC0 {
        ((b0 & 0x1F) as u32, 1usize, 2u8)
    } else if (b0 & 0xF0) == 0xE0 {
        ((b0 & 0x0F) as u32, 2usize, 3u8)
    } else if (b0 & 0xF8) == 0xF0 {
        ((b0 & 0x07) as u32, 3usize, 4u8)
    } else {
        *io_index = index as u64;
        *out_codepoint = REPLACEMENT;
        return if strict { 0 } else { 1 };
    };

    if index + need > input.len() {
        *io_index = input.len() as u64;
        *out_codepoint = REPLACEMENT;
        return if strict { 0 } else { 1 };
    }

    let mut i = 0usize;
    while i < need {
        let bx = input[index];
        index += 1;
        if !is_cont(bx) {
            *io_index = index as u64;
            *out_codepoint = REPLACEMENT;
            return if strict { 0 } else { 1 };
        }
        value = (value << 6) | ((bx & 0x3F) as u32);
        i += 1;
    }

    *io_index = index as u64;
    if valid_finished(value, expected) {
        *out_codepoint = value;
        1
    } else {
        *out_codepoint = REPLACEMENT;
        if strict { 0 } else { 1 }
    }
}

#[no_mangle]
pub extern "C" fn clks_rust_unicode_width(codepoint: u32) -> u32 {
    if codepoint == 0 {
        return 0;
    }

    if (0x0300..=0x036F).contains(&codepoint)
        || (0x0483..=0x0489).contains(&codepoint)
        || (0x0591..=0x05BD).contains(&codepoint)
        || codepoint == 0x05BF
        || (0x05C1..=0x05C2).contains(&codepoint)
        || (0x05C4..=0x05C5).contains(&codepoint)
        || codepoint == 0x05C7
        || (0x0610..=0x061A).contains(&codepoint)
        || (0x064B..=0x065F).contains(&codepoint)
        || codepoint == 0x0670
        || (0x06D6..=0x06DC).contains(&codepoint)
        || (0x06DF..=0x06E4).contains(&codepoint)
        || (0x06E7..=0x06E8).contains(&codepoint)
        || (0x06EA..=0x06ED).contains(&codepoint)
        || (0x1AB0..=0x1AFF).contains(&codepoint)
        || (0x1DC0..=0x1DFF).contains(&codepoint)
        || (0x20D0..=0x20FF).contains(&codepoint)
        || (0xFE00..=0xFE0F).contains(&codepoint)
        || (0xE0100..=0xE01EF).contains(&codepoint)
        || codepoint == 0x200D
    {
        return 0;
    }

    if (0x1F000..=0x1FAFF).contains(&codepoint) || (0x2600..=0x27BF).contains(&codepoint) {
        return 2;
    }

    if codepoint < 0x1100 {
        return 1;
    }

    if (0x1100..=0x115F).contains(&codepoint)
        || codepoint == 0x2329
        || codepoint == 0x232A
        || (0x2E80..=0xA4CF).contains(&codepoint)
        || (0xAC00..=0xD7A3).contains(&codepoint)
        || (0xF900..=0xFAFF).contains(&codepoint)
        || (0xFE10..=0xFE19).contains(&codepoint)
        || (0xFE30..=0xFE6F).contains(&codepoint)
        || (0xFF00..=0xFF60).contains(&codepoint)
        || (0xFFE0..=0xFFE6).contains(&codepoint)
        || (0x20000..=0x3FFFD).contains(&codepoint)
    {
        return 2;
    }

    1
}
