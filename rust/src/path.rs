use core::slice;

const PATH_POLICY_NONE: u32 = 0;
const PATH_POLICY_USER_DB: u32 = 1 << 0;
const PATH_POLICY_TEMP: u32 = 1 << 1;
const PATH_POLICY_HOME: u32 = 1 << 2;
const PATH_POLICY_HOME_ROOT: u32 = 1 << 3;
const PATH_POLICY_UNDER_HOME_ROOT: u32 = 1 << 4;
const PATH_POLICY_SYSTEM: u32 = 1 << 5;
const PATH_POLICY_SYSTEM_DRIVERS: u32 = 1 << 6;
const PATH_POLICY_USER_PROGRAM: u32 = 1 << 7;

const USER_DB_PATH: &[u8] = b"/system/databases/users.db";

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

unsafe fn write_nul(out: *mut u8, index: usize) {
    *out.add(index) = 0;
}

unsafe fn copy_component(src: &[u8], out: *mut u8, out_pos: &mut usize, out_size: usize) -> bool {
    if *out_pos + src.len() >= out_size {
        return false;
    }

    let mut i = 0usize;
    while i < src.len() {
        *out.add(*out_pos + i) = src[i];
        i += 1;
    }
    *out_pos += src.len();
    true
}

unsafe fn normalize_impl(path: &[u8], absolute: bool, out: *mut u8, out_size: u64, keep_root_slash: bool) -> bool {
    if out.is_null() || out_size == 0 {
        return false;
    }

    let out_size = out_size as usize;
    if absolute {
        if path.first().copied() != Some(b'/') || out_size < 2 {
            return false;
        }
    } else if path.first().copied() == Some(b'/') {
        return false;
    }

    let mut in_pos = 0usize;
    let mut out_pos = 0usize;

    if keep_root_slash {
        *out = b'/';
        out_pos = 1;
    }

    while in_pos < path.len() && path[in_pos] == b'/' {
        in_pos += 1;
    }

    while in_pos < path.len() {
        let comp_start = in_pos;
        while in_pos < path.len() && path[in_pos] != b'/' && path[in_pos] != 0 {
            in_pos += 1;
        }
        let component = &path[comp_start..in_pos];

        if component.is_empty() || component == b"." {
            while in_pos < path.len() && path[in_pos] == b'/' {
                in_pos += 1;
            }
            continue;
        }

        if component == b".." {
            return false;
        }

        if (keep_root_slash && out_pos > 1) || (!keep_root_slash && out_pos != 0) {
            if out_pos + 1 >= out_size {
                return false;
            }
            *out.add(out_pos) = b'/';
            out_pos += 1;
        }

        if !copy_component(component, out, &mut out_pos, out_size) {
            return false;
        }

        while in_pos < path.len() && path[in_pos] == b'/' {
            in_pos += 1;
        }
    }

    if out_pos >= out_size {
        return false;
    }
    write_nul(out, out_pos);
    true
}

fn path_eq(path: &[u8], expected: &[u8]) -> bool {
    path == expected
}

fn path_starts_with(path: &[u8], prefix: &[u8]) -> bool {
    path.len() >= prefix.len() && &path[..prefix.len()] == prefix
}

fn path_at_or_under(path: &[u8], prefix: &[u8]) -> bool {
    if !path_starts_with(path, prefix) {
        return false;
    }
    path.len() == prefix.len() || path.get(prefix.len()).copied() == Some(b'/')
}

fn path_has_suffix(path: &[u8], suffix: &[u8]) -> bool {
    path.len() >= suffix.len() && &path[path.len() - suffix.len()..] == suffix
}

fn path_is_user_program_bytes(path: &[u8]) -> bool {
    if path.first().copied() != Some(b'/') {
        return false;
    }
    if path_at_or_under(path, b"/system") || path_at_or_under(path, b"/system/drivers") {
        return false;
    }
    path_starts_with(path, b"/shell/") || path_has_suffix(path, b".elf")
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_path_normalize_absolute(
    path: *const u8,
    out: *mut u8,
    out_size: u64,
) -> u32 {
    if path.is_null() {
        return 0;
    }

    if normalize_impl(c_str_bytes(path), true, out, out_size, true) { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_path_normalize_relative(
    path: *const u8,
    out: *mut u8,
    out_size: u64,
) -> u32 {
    if path.is_null() {
        return 0;
    }

    if normalize_impl(c_str_bytes(path), false, out, out_size, false) { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_path_normalize_external_internal(
    path: *const u8,
    out: *mut u8,
    out_size: u64,
) -> u32 {
    if path.is_null() {
        return 0;
    }

    if normalize_impl(c_str_bytes(path), true, out, out_size, false) { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_path_at_or_under(path: *const u8, prefix: *const u8) -> u32 {
    if path.is_null() || prefix.is_null() {
        return 0;
    }

    if path_at_or_under(c_str_bytes(path), c_str_bytes(prefix)) { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_path_has_prefix(path: *const u8, prefix: *const u8) -> u32 {
    if path.is_null() || prefix.is_null() {
        return 0;
    }

    if path_starts_with(c_str_bytes(path), c_str_bytes(prefix)) { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_path_is_user_program(path: *const u8) -> u32 {
    if path.is_null() {
        return 0;
    }

    let path = c_str_bytes(path);
    if path_is_user_program_bytes(path) { 1 } else { 0 }
}

#[no_mangle]
pub unsafe extern "C" fn clks_rust_path_policy_flags(path: *const u8, home: *const u8) -> u32 {
    if path.is_null() {
        return PATH_POLICY_NONE;
    }

    let path = c_str_bytes(path);
    let mut flags = PATH_POLICY_NONE;

    if path_eq(path, USER_DB_PATH) {
        flags |= PATH_POLICY_USER_DB;
    }
    if path_at_or_under(path, b"/temp") {
        flags |= PATH_POLICY_TEMP;
    }
    if path_eq(path, b"/home") {
        flags |= PATH_POLICY_HOME_ROOT;
    }
    if path_at_or_under(path, b"/home") {
        flags |= PATH_POLICY_UNDER_HOME_ROOT;
    }
    if path_at_or_under(path, b"/system") {
        flags |= PATH_POLICY_SYSTEM;
    }
    if path_at_or_under(path, b"/system/drivers") {
        flags |= PATH_POLICY_SYSTEM_DRIVERS;
    }
    if path_is_user_program_bytes(path) {
        flags |= PATH_POLICY_USER_PROGRAM;
    }

    if !home.is_null() {
        let home = c_str_bytes(home);
        if !home.is_empty() && path_at_or_under(path, home) {
            flags |= PATH_POLICY_HOME;
        }
    }

    flags
}
