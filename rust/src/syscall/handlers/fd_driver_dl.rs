use core::{ffi::c_void, mem, ptr};

use crate::syscall::consts::*;
use crate::syscall::{ffi, usercopy};

#[repr(C)]
#[derive(Copy, Clone)]
struct DriverInfoUser {
    name: [i8; ffi::DRIVER_NAME_MAX],
    path: [i8; ffi::DRIVER_PATH_MAX],
    kind: u64,
    state: u64,
    driver_class: u64,
    from_elf: u64,
    image_size: u64,
    elf_entry: u64,
    load_id: u64,
    owner_pid: u64,
}

static mut FD_IO_BOUNCE: [u8; FS_IO_CHUNK_LEN] = [0; FS_IO_CHUNK_LEN];

fn fd_bounce_mut_ptr() -> *mut u8 {
    core::ptr::addr_of_mut!(FD_IO_BOUNCE) as *mut u8
}

fn fd_bounce_ptr() -> *const u8 {
    core::ptr::addr_of!(FD_IO_BOUNCE) as *const u8
}

pub fn fd_open(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    let mut path = [0i8; PATH_MAX];
    if !usercopy::copy_user_string(arg0, &mut path) {
        return U64_MAX;
    }
    let writey = (arg1 & EXEC_FD_ACCESS_MASK) == EXEC_O_WRONLY
        || (arg1 & EXEC_FD_ACCESS_MASK) == EXEC_O_RDWR
        || (arg1 & EXEC_O_CREAT) != 0
        || (arg1 & EXEC_O_TRUNC) != 0
        || (arg1 & EXEC_O_APPEND) != 0;
    if writey && unsafe { ffi::clks_user_path_write_allowed(path.as_ptr()) } == 0 {
        return U64_MAX;
    }
    unsafe { ffi::clks_exec_fd_open(path.as_ptr(), arg1, arg2) }
}

pub fn fd_read(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    let mut done = 0u64;
    if arg2 > 0 && arg1 == 0 {
        return U64_MAX;
    }
    if arg2 == 0 {
        return 0;
    }
    while done < arg2 {
        let mut chunk_len = arg2 - done;
        if chunk_len > FS_IO_CHUNK_LEN as u64 {
            chunk_len = FS_IO_CHUNK_LEN as u64;
        }
        let dst = arg1.wrapping_add(done);
        if dst < arg1 || !usercopy::user_ptr_writable(dst, chunk_len) {
            return if done > 0 { done } else { U64_MAX };
        }
        let got = unsafe { ffi::clks_exec_fd_read(arg0, fd_bounce_mut_ptr() as *mut c_void, chunk_len) };
        if got == U64_MAX {
            return if done > 0 { done } else { U64_MAX };
        }
        if got == 0 {
            break;
        }
        if got > chunk_len {
            return if done > 0 { done } else { U64_MAX };
        }
        unsafe {
            ptr::copy_nonoverlapping(fd_bounce_ptr(), dst as *mut u8, got as usize);
        }
        done += got;
        if got < chunk_len {
            break;
        }
    }
    done
}

pub fn fd_write(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    if arg2 > 0 && arg1 == 0 {
        return U64_MAX;
    }
    if arg2 > 0 && !usercopy::user_ptr_readable(arg1, arg2) {
        return U64_MAX;
    }
    unsafe { ffi::clks_exec_fd_write(arg0, arg1 as *const c_void, arg2) }
}

pub fn fd_close(arg0: u64) -> u64 {
    unsafe { ffi::clks_exec_fd_close(arg0) }
}
pub fn fd_dup(arg0: u64) -> u64 {
    unsafe { ffi::clks_exec_fd_dup(arg0) }
}
pub fn pty_open() -> u64 {
    unsafe { ffi::clks_exec_fd_open_pty() }
}

pub fn driver_count() -> u64 {
    unsafe { ffi::clks_driver_count() }
}

pub fn driver_info(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    if arg1 == 0 || arg2 < mem::size_of::<DriverInfoUser>() as u64 {
        return 0;
    }
    if !usercopy::user_ptr_writable(arg1, mem::size_of::<DriverInfoUser>() as u64) {
        return 0;
    }
    let mut info = ffi::DriverInfo {
        name: [0; ffi::DRIVER_NAME_MAX],
        path: [0; ffi::DRIVER_PATH_MAX],
        kind: 0,
        state: 0,
        driver_class: 0,
        from_elf: 0,
        image_size: 0,
        elf_entry: 0,
        load_id: 0,
        owner_pid: 0,
    };
    if unsafe { ffi::clks_driver_get(arg0, &mut info as *mut ffi::DriverInfo) } == 0 {
        return 0;
    }
    let mut out = DriverInfoUser {
        name: [0; ffi::DRIVER_NAME_MAX],
        path: [0; ffi::DRIVER_PATH_MAX],
        kind: info.kind as u64,
        state: info.state as u64,
        driver_class: info.driver_class as u64,
        from_elf: if info.from_elf != 0 { 1 } else { 0 },
        image_size: info.image_size,
        elf_entry: info.elf_entry,
        load_id: info.load_id,
        owner_pid: info.owner_pid,
    };
    out.name.copy_from_slice(&info.name);
    out.path.copy_from_slice(&info.path);
    if usercopy::copy_to_user_struct(arg1, &out) { 1 } else { 0 }
}

pub fn driver_load(arg0: u64) -> u64 {
    if unsafe { ffi::clks_user_privileged_operation_allowed() } == 0 {
        return 0;
    }
    let mut path = [0i8; PATH_MAX];
    if !usercopy::copy_user_string(arg0, &mut path) {
        return 0;
    }
    unsafe { ffi::clks_driver_load_path(path.as_ptr()) }
}

pub fn driver_unload(arg0: u64) -> u64 {
    if unsafe { ffi::clks_user_privileged_operation_allowed() } == 0 {
        return 0;
    }
    let mut name_or_path = [0i8; PATH_MAX];
    if !usercopy::copy_user_string(arg0, &mut name_or_path) {
        return 0;
    }
    if unsafe { ffi::clks_driver_unload(name_or_path.as_ptr()) } != 0 { 1 } else { 0 }
}

pub fn driver_reload() -> u64 {
    if unsafe { ffi::clks_user_privileged_operation_allowed() } == 0 {
        return 0;
    }
    unsafe { ffi::clks_driver_reload_elf_dir() }
}

pub fn dl_open(arg0: u64) -> u64 {
    let mut path = [0i8; PATH_MAX];
    if !usercopy::copy_user_string(arg0, &mut path) {
        return U64_MAX;
    }
    unsafe { ffi::clks_exec_dl_open(path.as_ptr()) }
}

pub fn dl_close(arg0: u64) -> u64 {
    unsafe { ffi::clks_exec_dl_close(arg0) }
}

pub fn dl_sym(arg0: u64, arg1: u64) -> u64 {
    let mut symbol = [0i8; NAME_MAX];
    if !usercopy::copy_user_string(arg1, &mut symbol) {
        return 0;
    }
    unsafe { ffi::clks_exec_dl_sym(arg0, symbol.as_ptr()) }
}
