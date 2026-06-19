use core::mem;
use core::ptr;

use super::state;
use super::ffi;

pub fn user_ptr_readable(addr: u64, size: u64) -> bool {
    if addr == 0 || size == 0 {
        return false;
    }
    if !state::in_user_exec_context() {
        return true;
    }
    unsafe { ffi::clks_exec_current_user_ptr_readable(addr, size) != 0 }
}

pub fn user_ptr_writable(addr: u64, size: u64) -> bool {
    if addr == 0 || size == 0 {
        return false;
    }
    if !state::in_user_exec_context() {
        return true;
    }
    unsafe { ffi::clks_exec_current_user_ptr_writable(addr, size) != 0 }
}

pub fn copy_user_string(src_addr: u64, dst: &mut [i8]) -> bool {
    if src_addr == 0 || dst.is_empty() {
        return false;
    }

    let mut i = 0usize;
    while i + 1 < dst.len() {
        let char_addr = src_addr.wrapping_add(i as u64);
        if char_addr < src_addr || !user_ptr_readable(char_addr, 1) {
            return false;
        }

        let ch = unsafe { *(char_addr as *const i8) };
        dst[i] = ch;
        if ch == 0 {
            return true;
        }
        i += 1;
    }

    dst[dst.len() - 1] = 0;
    true
}

pub fn copy_optional_user_string(src_addr: u64, dst: &mut [i8]) -> bool {
    if dst.is_empty() {
        return false;
    }
    if src_addr == 0 {
        dst[0] = 0;
        return true;
    }
    copy_user_string(src_addr, dst)
}

pub fn copy_from_user_struct<T: Copy>(addr: u64) -> Option<T> {
    if addr == 0 || !user_ptr_readable(addr, mem::size_of::<T>() as u64) {
        return None;
    }
    Some(unsafe { ptr::read_unaligned(addr as *const T) })
}

pub fn copy_to_user_struct<T: Copy>(addr: u64, value: &T) -> bool {
    if addr == 0 || !user_ptr_writable(addr, mem::size_of::<T>() as u64) {
        return false;
    }
    unsafe {
        ptr::copy_nonoverlapping(value as *const T as *const u8, addr as *mut u8, mem::size_of::<T>());
    }
    true
}

pub fn copy_text_to_user(dst_addr: u64, dst_size: u64, src: *const i8, src_len: usize) -> u64 {
    if dst_addr == 0 || dst_size == 0 || src.is_null() {
        return 0;
    }

    let mut copy_len = src_len;
    if copy_len + 1 > dst_size as usize {
        copy_len = dst_size as usize - 1;
    }

    if !user_ptr_writable(dst_addr, copy_len as u64 + 1) {
        return 0;
    }

    unsafe {
        ptr::copy_nonoverlapping(src as *const u8, dst_addr as *mut u8, copy_len);
        *((dst_addr as *mut u8).add(copy_len)) = 0;
    }
    copy_len as u64
}
