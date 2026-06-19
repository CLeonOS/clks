use super::consts::{CLKS_FALSE, USC_MAX_ALLOWED_APPS};
use super::frame::SyscallFrame;
use super::ffi;

pub static mut READY: u32 = CLKS_FALSE;
pub static mut USER_TRACE_ACTIVE: u32 = CLKS_FALSE;
pub static mut USER_TRACE_BUDGET: u64 = 0;
pub static mut LAST_FRAME: SyscallFrame = SyscallFrame::kernel_call(0, 0, 0, 0);
pub static mut LAST_FRAME_VALID: u32 = CLKS_FALSE;
pub static mut SYMBOLS_CHECKED: u32 = CLKS_FALSE;
pub static mut SYMBOLS_DATA: *const i8 = core::ptr::null();
pub static mut SYMBOLS_SIZE: u64 = 0;

pub static mut USC_SESSION_ALLOWED_USED: [u32; USC_MAX_ALLOWED_APPS] = [0; USC_MAX_ALLOWED_APPS];
pub static mut USC_SESSION_ALLOWED_PATH: [[i8; ffi::EXEC_PROC_PATH_MAX]; USC_MAX_ALLOWED_APPS] =
    [[0; ffi::EXEC_PROC_PATH_MAX]; USC_MAX_ALLOWED_APPS];
pub static mut USC_PERMANENT_ALLOWED_USED: [u32; USC_MAX_ALLOWED_APPS] = [0; USC_MAX_ALLOWED_APPS];
pub static mut USC_PERMANENT_ALLOWED_PATH: [[i8; ffi::EXEC_PROC_PATH_MAX]; USC_MAX_ALLOWED_APPS] =
    [[0; ffi::EXEC_PROC_PATH_MAX]; USC_MAX_ALLOWED_APPS];
pub static mut USC_PERMANENT_LOADED: u32 = CLKS_FALSE;

pub fn in_user_exec_context() -> bool {
    unsafe { ffi::clks_exec_is_running() != 0 && ffi::clks_exec_current_path_is_user() != 0 }
}
