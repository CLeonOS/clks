use core::{mem, ptr};

use crate::syscall::consts::*;
use crate::syscall::{ffi, usercopy};

#[repr(C)]
#[derive(Copy, Clone)]
pub struct FbInfoUser {
    width: u64,
    height: u64,
    pitch: u64,
    bpp: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct FbBlitReq {
    pixels_ptr: u64,
    src_width: u64,
    src_height: u64,
    src_pitch_bytes: u64,
    dst_x: u64,
    dst_y: u64,
    scale: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct DisplayInfoUser {
    target: u64,
    physical_width: u64,
    physical_height: u64,
    logical_width: u64,
    logical_height: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct DisplaySetModeReq {
    target: u64,
    logical_width: u64,
    logical_height: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct TtyGridInfoUser {
    cols: u64,
    rows: u64,
}

pub fn log_write(arg0: u64, arg1: u64) -> u64 {
    let mut len = arg1;
    let mut buf = [0i8; LOG_MAX_LEN + 1];
    if arg0 == 0 || len == 0 {
        return 0;
    }
    if len > LOG_MAX_LEN as u64 {
        len = LOG_MAX_LEN as u64;
    }
    if !usercopy::user_ptr_readable(arg0, len) {
        return 0;
    }
    unsafe {
        ptr::copy_nonoverlapping(arg0 as *const i8, buf.as_mut_ptr(), len as usize);
    }
    buf[len as usize] = 0;
    if crate::syscall::text::buf_starts_with(&buf, b"[DRIVER]") {
        return len;
    }
    unsafe {
        ffi::clks_log(LOG_INFO, b"SYSCALL\0".as_ptr() as *const i8, buf.as_ptr());
    }
    len
}

pub fn tty_write(arg0: u64, arg1: u64) -> u64 {
    let mut len = arg1;
    let mut buf = [0i8; TTY_MAX_LEN + 1];
    if arg0 == 0 || len == 0 {
        return 0;
    }
    if len > TTY_MAX_LEN as u64 {
        len = TTY_MAX_LEN as u64;
    }
    if !usercopy::user_ptr_readable(arg0, len) {
        return 0;
    }
    unsafe {
        ptr::copy_nonoverlapping(arg0 as *const i8, buf.as_mut_ptr(), len as usize);
        buf[len as usize] = 0;
        ffi::clks_tty_write(buf.as_ptr());
    }
    len
}

pub fn tty_write_char(arg0: u64) -> u64 {
    unsafe {
        ffi::clks_tty_write_char((arg0 & 0xff) as i8);
    }
    1
}

pub fn kbd_get_char() -> u64 {
    let mut ch = 0i8;
    let tty_index = unsafe { ffi::clks_exec_current_tty() };
    if unsafe { ffi::clks_keyboard_pop_char_for_tty(tty_index, &mut ch as *mut i8) } == 0 {
        return U64_MAX;
    }
    ch as u8 as u64
}

pub fn fb_info(arg0: u64) -> u64 {
    if arg0 == 0 || unsafe { ffi::clks_fb_ready() } == 0 {
        return 0;
    }
    if !usercopy::user_ptr_writable(arg0, mem::size_of::<FbInfoUser>() as u64) {
        return 0;
    }
    let fb = unsafe { ffi::clks_fb_info() };
    let out = FbInfoUser {
        width: fb.width as u64,
        height: fb.height as u64,
        pitch: fb.pitch as u64,
        bpp: fb.bpp as u64,
    };
    if usercopy::copy_to_user_struct(arg0, &out) { 1 } else { 0 }
}

pub fn fb_clear(arg0: u64) -> u64 {
    if unsafe { ffi::clks_fb_ready() } == 0 {
        return 0;
    }
    unsafe {
        ffi::clks_fb_clear((arg0 & 0xffff_ffff) as u32);
    }
    1
}

pub fn display_info(arg0: u64, arg1: u64) -> u64 {
    if arg1 == 0 || !usercopy::user_ptr_writable(arg1, mem::size_of::<DisplayInfoUser>() as u64) {
        return 0;
    }
    let mut mode = ffi::DisplayMode {
        physical_width: 0,
        physical_height: 0,
        logical_width: 0,
        logical_height: 0,
    };
    if unsafe { ffi::clks_display_mode_get(arg0 as u32, &mut mode as *mut ffi::DisplayMode) } == 0 {
        return 0;
    }
    let out = DisplayInfoUser {
        target: arg0,
        physical_width: mode.physical_width as u64,
        physical_height: mode.physical_height as u64,
        logical_width: mode.logical_width as u64,
        logical_height: mode.logical_height as u64,
    };
    if usercopy::copy_to_user_struct(arg1, &out) { 1 } else { 0 }
}

pub fn display_set_mode(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<DisplaySetModeReq>(arg0) else {
        return 0;
    };
    if req.logical_width > u32::MAX as u64 || req.logical_height > u32::MAX as u64 {
        return 0;
    }
    unsafe {
        if req.target == DISPLAY_TARGET_TTY {
            return if ffi::clks_tty_set_resolution(req.logical_width as u32, req.logical_height as u32) != 0 {
                1
            } else {
                0
            };
        }
        if req.target == DISPLAY_TARGET_WM {
            return if ffi::clks_wm_set_resolution(req.logical_width as u32, req.logical_height as u32) != 0 {
                1
            } else {
                0
            };
        }
    }
    0
}

pub fn tty_grid_info(arg0: u64) -> u64 {
    if arg0 == 0 || !usercopy::user_ptr_writable(arg0, mem::size_of::<TtyGridInfoUser>() as u64) {
        return 0;
    }
    let out = TtyGridInfoUser {
        cols: unsafe { ffi::clks_tty_text_cols() } as u64,
        rows: unsafe { ffi::clks_tty_text_rows() } as u64,
    };
    if usercopy::copy_to_user_struct(arg0, &out) { 1 } else { 0 }
}

pub fn fb_blit(arg0: u64) -> u64 {
    if arg0 == 0 || unsafe { ffi::clks_fb_ready() } == 0 {
        return 0;
    }
    let Some(req) = usercopy::copy_from_user_struct::<FbBlitReq>(arg0) else {
        return 0;
    };
    if req.pixels_ptr == 0 || req.src_width == 0 || req.src_height == 0 || req.scale == 0 {
        return 0;
    }
    if req.src_width > 4096 || req.src_height > 4096 || req.scale > 8 {
        return 0;
    }
    let src_pitch = if req.src_pitch_bytes == 0 {
        req.src_width.saturating_mul(4)
    } else {
        req.src_pitch_bytes
    };
    if src_pitch < req.src_width.saturating_mul(4) {
        return 0;
    }
    let Some(src_bytes) = src_pitch.checked_mul(req.src_height) else {
        return 0;
    };
    if !usercopy::user_ptr_readable(req.pixels_ptr, src_bytes) {
        return 0;
    }
    let fb = unsafe { ffi::clks_fb_info() };
    if req.dst_x >= fb.width as u64 || req.dst_y >= fb.height as u64 {
        return 0;
    }
    if req.scale == 1 {
        unsafe {
            ffi::clks_fb_blit_rgba(
                req.dst_x as i32,
                req.dst_y as i32,
                req.pixels_ptr as *const core::ffi::c_void,
                req.src_width as u32,
                req.src_height as u32,
                src_pitch as u32,
            );
        }
        return 1;
    }
    let mut y = 0u64;
    while y < req.src_height {
        let draw_y = req.dst_y + y * req.scale;
        if draw_y >= fb.height as u64 {
            break;
        }
        let row = (req.pixels_ptr + y * src_pitch) as *const u32;
        let mut x = 0u64;
        while x < req.src_width {
            let draw_x = req.dst_x + x * req.scale;
            if draw_x >= fb.width as u64 {
                break;
            }
            let color = unsafe { *row.add(x as usize) };
            unsafe {
                ffi::clks_fb_fill_rect(draw_x as u32, draw_y as u32, req.scale as u32, req.scale as u32, color);
            }
            x += 1;
        }
        y += 1;
    }
    1
}

pub fn log_journal_count() -> u64 {
    unsafe { ffi::clks_log_journal_count() }
}

pub fn log_journal_read(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    if arg1 == 0 || arg2 == 0 {
        return 0;
    }
    let mut size = arg2;
    if size > JOURNAL_MAX_LEN as u64 {
        size = JOURNAL_MAX_LEN as u64;
    }
    if !usercopy::user_ptr_writable(arg1, size) {
        return 0;
    }
    let mut buf = [0i8; JOURNAL_MAX_LEN];
    let ok = unsafe { ffi::clks_log_journal_read(arg0, buf.as_mut_ptr(), size as usize) };
    if ok == 0 {
        return 0;
    }
    unsafe {
        ptr::copy_nonoverlapping(buf.as_ptr(), arg1 as *mut i8, size as usize);
    }
    1
}

pub fn audio_available() -> u64 {
    if unsafe { ffi::clks_audio_available() } != 0 { 1 } else { 0 }
}

pub fn audio_play_tone(arg0: u64, arg1: u64) -> u64 {
    if unsafe { ffi::clks_audio_play_tone(arg0, arg1) } != 0 { 1 } else { 0 }
}

pub fn audio_stop() -> u64 {
    unsafe {
        ffi::clks_audio_stop();
    }
    1
}
