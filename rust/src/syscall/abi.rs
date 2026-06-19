use core::ffi::c_void;
use core::ptr;

use super::consts::{CLKS_FALSE, CLKS_TRUE, LOG_INFO, U64_MAX};
use super::frame::SyscallFrame;
use super::{ffi, handlers, state, stats, trace, usc};

#[no_mangle]
pub extern "C" fn clks_syscall_init() {
    unsafe {
        state::READY = CLKS_TRUE;
        state::USER_TRACE_ACTIVE = CLKS_FALSE;
        state::USER_TRACE_BUDGET = 0;
        state::LAST_FRAME = SyscallFrame::kernel_call(0, 0, 0, 0);
        state::LAST_FRAME_VALID = CLKS_FALSE;
        state::SYMBOLS_CHECKED = CLKS_FALSE;
        state::SYMBOLS_DATA = ptr::null();
        state::SYMBOLS_SIZE = 0;
        state::USC_SESSION_ALLOWED_USED = [0; super::consts::USC_MAX_ALLOWED_APPS];
        state::USC_SESSION_ALLOWED_PATH = [[0; ffi::EXEC_PROC_PATH_MAX]; super::consts::USC_MAX_ALLOWED_APPS];
        state::USC_PERMANENT_ALLOWED_USED = [0; super::consts::USC_MAX_ALLOWED_APPS];
        state::USC_PERMANENT_ALLOWED_PATH = [[0; ffi::EXEC_PROC_PATH_MAX]; super::consts::USC_MAX_ALLOWED_APPS];
        state::USC_PERMANENT_LOADED = CLKS_FALSE;
    }
    stats::reset();
    unsafe {
        ffi::clks_log(
            LOG_INFO,
            b"SYSCALL\0".as_ptr() as *const i8,
            b"INT80 FRAMEWORK ONLINE\0".as_ptr() as *const i8,
        );
    }
}

#[no_mangle]
pub unsafe extern "C" fn clks_syscall_dispatch(frame_ptr: *mut c_void) -> u64 {
    if state::READY == CLKS_FALSE || frame_ptr.is_null() {
        return U64_MAX;
    }

    let frame = &mut *(frame_ptr as *mut SyscallFrame);
    state::LAST_FRAME = *frame;
    state::LAST_FRAME_VALID = CLKS_TRUE;

    let id = frame.rax;
    stats::record(id);
    trace::trace_user_program(id);

    let user_trace_enabled = state::in_user_exec_context();
    let user_pid = if user_trace_enabled { ffi::clks_exec_current_pid() } else { 0 };
    trace::user_call(user_trace_enabled, user_pid, id, frame.rbx, frame.rcx, frame.rdx);

    let ret = if !usc::check(id, frame.rbx, frame.rcx, frame.rdx) {
        U64_MAX
    } else {
        handlers::dispatch(frame)
    };

    trace::user_return(user_trace_enabled, user_pid, id, ret);
    ret
}

#[no_mangle]
pub extern "C" fn clks_syscall_invoke_kernel(id: u64, arg0: u64, arg1: u64, arg2: u64) -> u64 {
    let mut frame = SyscallFrame::kernel_call(id, arg0, arg1, arg2);
    unsafe { clks_syscall_dispatch(&mut frame as *mut SyscallFrame as *mut c_void) }
}
