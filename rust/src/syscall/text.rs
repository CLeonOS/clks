use core::ffi::c_char;

pub unsafe fn c_strlen(ptr: *const c_char) -> usize {
    if ptr.is_null() {
        return 0;
    }
    let mut len = 0usize;
    while *ptr.add(len) != 0 {
        len += 1;
    }
    len
}

pub unsafe fn c_str_eq(ptr: *const c_char, bytes: &[u8]) -> bool {
    if ptr.is_null() {
        return false;
    }
    let mut i = 0usize;
    while i < bytes.len() {
        if *ptr.add(i) as u8 != bytes[i] {
            return false;
        }
        i += 1;
    }
    *ptr.add(i) == 0
}

pub fn buf_eq(buf: &[c_char], bytes: &[u8]) -> bool {
    let mut i = 0usize;
    while i < bytes.len() {
        if i >= buf.len() || buf[i] as u8 != bytes[i] {
            return false;
        }
        i += 1;
    }
    i < buf.len() && buf[i] == 0
}

pub fn buf_starts_with(buf: &[c_char], bytes: &[u8]) -> bool {
    if bytes.len() > buf.len() {
        return false;
    }
    let mut i = 0usize;
    while i < bytes.len() {
        if buf[i] as u8 != bytes[i] {
            return false;
        }
        i += 1;
    }
    true
}

pub fn copy_const_text(dst: &mut [c_char], src: &[u8]) {
    if dst.is_empty() {
        return;
    }
    let mut i = 0usize;
    while i + 1 < dst.len() && i < src.len() && src[i] != 0 {
        dst[i] = src[i] as c_char;
        i += 1;
    }
    dst[i] = 0;
}

pub fn append_char(out: &mut [c_char], pos: usize, ch: u8) -> usize {
    if pos + 1 >= out.len() {
        return pos;
    }
    out[pos] = ch as c_char;
    out[pos + 1] = 0;
    pos + 1
}

pub fn append_bytes(out: &mut [c_char], mut pos: usize, text: &[u8]) -> usize {
    let mut i = 0usize;
    while i < text.len() && text[i] != 0 {
        pos = append_char(out, pos, text[i]);
        i += 1;
    }
    pos
}

pub fn append_c_n(out: &mut [c_char], mut pos: usize, text: *const c_char, text_len: usize) -> usize {
    if text.is_null() {
        return pos;
    }
    let mut i = 0usize;
    unsafe {
        while i < text_len {
            pos = append_char(out, pos, *text.add(i) as u8);
            i += 1;
        }
    }
    pos
}

pub fn append_u64_dec(out: &mut [c_char], pos: usize, mut value: u64) -> usize {
    let mut digits = [0u8; 20];
    let mut count = 0usize;
    if value == 0 {
        return append_char(out, pos, b'0');
    }
    while value > 0 && count < digits.len() {
        digits[count] = b'0' + (value % 10) as u8;
        value /= 10;
        count += 1;
    }
    let mut p = pos;
    while count > 0 {
        count -= 1;
        p = append_char(out, p, digits[count]);
    }
    p
}

pub fn append_u64_hex(out: &mut [c_char], mut pos: usize, value: u64) -> usize {
    pos = append_bytes(out, pos, b"0x");
    let mut started = false;
    let mut nibble = 16usize;
    while nibble > 0 {
        nibble -= 1;
        let current = ((value >> ((nibble * 4) as u64)) & 0x0f) as u8;
        if current != 0 || started || nibble == 0 {
            started = true;
            let ch = if current < 10 { b'0' + current } else { b'a' + (current - 10) };
            pos = append_char(out, pos, ch);
        }
    }
    pos
}

pub fn parse_u64_dec(buf: &[c_char]) -> Option<u64> {
    let mut value = 0u64;
    let mut any = false;
    let mut i = 0usize;
    while i < buf.len() && buf[i] != 0 {
        let ch = buf[i] as u8;
        if !(b'0'..=b'9').contains(&ch) {
            return None;
        }
        let digit = (ch - b'0') as u64;
        value = value.checked_mul(10)?.checked_add(digit)?;
        any = true;
        i += 1;
    }
    if any { Some(value) } else { None }
}
