use core::{ffi::c_void, mem, ptr};

use crate::syscall::consts::*;
use crate::syscall::{ffi, frame::SyscallFrame, usercopy};

#[repr(C)]
#[derive(Copy, Clone)]
struct ExecIoReq {
    env_line_ptr: u64,
    stdin_fd: u64,
    stdout_fd: u64,
    stderr_fd: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct MmapReqUser {
    addr_hint: u64,
    length: u64,
    prot: u64,
    flags: u64,
    fd: u64,
    offset: u64,
}

pub fn exec_path(arg0: u64) -> u64 {
    let mut path = [0i8; PATH_MAX];
    let mut status = U64_MAX;
    if !usercopy::copy_user_string(arg0, &mut path) {
        return U64_MAX;
    }
    if unsafe { ffi::clks_exec_run_path(path.as_ptr(), &mut status as *mut u64) } == 0 {
        return U64_MAX;
    }
    status
}

pub fn exec_pathv(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    let mut path = [0i8; PATH_MAX];
    let mut argv_line = [0i8; ARG_LINE_MAX];
    let mut env_line = [0i8; ENV_LINE_MAX];
    let mut status = U64_MAX;
    if !usercopy::copy_user_string(arg0, &mut path)
        || !usercopy::copy_optional_user_string(arg1, &mut argv_line)
        || !usercopy::copy_optional_user_string(arg2, &mut env_line)
    {
        return U64_MAX;
    }
    if unsafe {
        ffi::clks_exec_run_pathv(path.as_ptr(), argv_line.as_ptr(), env_line.as_ptr(), &mut status as *mut u64)
    } == 0
    {
        return U64_MAX;
    }
    status
}

pub fn exec_pathv_io(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    if arg2 == 0 {
        return U64_MAX;
    }
    let mut path = [0i8; PATH_MAX];
    let mut argv_line = [0i8; ARG_LINE_MAX];
    let mut env_line = [0i8; ENV_LINE_MAX];
    let mut status = U64_MAX;
    if !usercopy::copy_user_string(arg0, &mut path) || !usercopy::copy_optional_user_string(arg1, &mut argv_line) {
        return U64_MAX;
    }
    let Some(req) = usercopy::copy_from_user_struct::<ExecIoReq>(arg2) else {
        return U64_MAX;
    };
    if !usercopy::copy_optional_user_string(req.env_line_ptr, &mut env_line) {
        return U64_MAX;
    }
    if unsafe {
        ffi::clks_exec_run_pathv_io(
            path.as_ptr(),
            argv_line.as_ptr(),
            env_line.as_ptr(),
            req.stdin_fd,
            req.stdout_fd,
            req.stderr_fd,
            &mut status as *mut u64,
        )
    } == 0
    {
        return U64_MAX;
    }
    status
}

pub fn mmap(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<MmapReqUser>(arg0) else {
        return 0;
    };
    unsafe { ffi::clks_exec_mmap(req.addr_hint, req.length, req.prot, req.flags, req.fd, req.offset) }
}

pub fn getpid() -> u64 {
    unsafe { ffi::clks_exec_current_pid() }
}

pub fn spawn_path(arg0: u64) -> u64 {
    let mut path = [0i8; PATH_MAX];
    let mut pid = U64_MAX;
    if !usercopy::copy_user_string(arg0, &mut path) {
        return U64_MAX;
    }
    if unsafe { ffi::clks_exec_spawn_path(path.as_ptr(), &mut pid as *mut u64) } == 0 {
        return U64_MAX;
    }
    pid
}

pub fn spawn_pathv(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    let mut path = [0i8; PATH_MAX];
    let mut argv_line = [0i8; ARG_LINE_MAX];
    let mut env_line = [0i8; ENV_LINE_MAX];
    let mut pid = U64_MAX;
    if !usercopy::copy_user_string(arg0, &mut path)
        || !usercopy::copy_optional_user_string(arg1, &mut argv_line)
        || !usercopy::copy_optional_user_string(arg2, &mut env_line)
    {
        return U64_MAX;
    }
    if unsafe { ffi::clks_exec_spawn_pathv(path.as_ptr(), argv_line.as_ptr(), env_line.as_ptr(), &mut pid as *mut u64) }
        == 0
    {
        return U64_MAX;
    }
    pid
}

pub fn waitpid(arg0: u64, arg1: u64) -> u64 {
    let mut status = U64_MAX;
    let wait_ret = unsafe { ffi::clks_exec_wait_pid(arg0, &mut status as *mut u64) };
    if wait_ret == 1 && arg1 != 0 {
        if !usercopy::copy_to_user_struct(arg1, &status) {
            return U64_MAX;
        }
    }
    wait_ret
}

pub fn proc_argc() -> u64 {
    unsafe { ffi::clks_exec_current_argc() }
}

pub fn proc_argv(arg0: u64, arg1: u64, mut arg2: u64) -> u64 {
    if arg1 == 0 || arg2 == 0 {
        return 0;
    }
    if arg2 > ITEM_MAX {
        arg2 = ITEM_MAX;
    }
    if !usercopy::user_ptr_writable(arg1, arg2) {
        return 0;
    }
    if unsafe { ffi::clks_exec_copy_current_argv(arg0, arg1 as *mut i8, arg2 as usize) } != 0 { 1 } else { 0 }
}

pub fn proc_envc() -> u64 {
    unsafe { ffi::clks_exec_current_envc() }
}

pub fn proc_env(arg0: u64, arg1: u64, mut arg2: u64) -> u64 {
    if arg1 == 0 || arg2 == 0 {
        return 0;
    }
    if arg2 > ITEM_MAX {
        arg2 = ITEM_MAX;
    }
    if !usercopy::user_ptr_writable(arg1, arg2) {
        return 0;
    }
    if unsafe { ffi::clks_exec_copy_current_env(arg0, arg1 as *mut i8, arg2 as usize) } != 0 { 1 } else { 0 }
}

pub fn proc_last_signal() -> u64 {
    unsafe { ffi::clks_exec_current_signal() }
}
pub fn proc_fault_vector() -> u64 {
    unsafe { ffi::clks_exec_current_fault_vector() }
}
pub fn proc_fault_error() -> u64 {
    unsafe { ffi::clks_exec_current_fault_error() }
}
pub fn proc_fault_rip() -> u64 {
    unsafe { ffi::clks_exec_current_fault_rip() }
}
pub fn proc_count() -> u64 {
    unsafe { ffi::clks_exec_proc_count() }
}

pub fn proc_pid_at(arg0: u64, arg1: u64) -> u64 {
    let mut pid = 0u64;
    if arg1 == 0 || !usercopy::user_ptr_writable(arg1, mem::size_of::<u64>() as u64) {
        return 0;
    }
    if unsafe { ffi::clks_exec_proc_pid_at(arg0, &mut pid as *mut u64) } == 0 {
        return 0;
    }
    usercopy::copy_to_user_struct(arg1, &pid) as u64
}

pub fn proc_snapshot(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    if arg1 == 0 || arg2 == 0 || !usercopy::user_ptr_writable(arg1, arg2) {
        return 0;
    }
    unsafe {
        ptr::write_bytes(arg1 as *mut u8, 0, arg2 as usize);
        if ffi::clks_exec_proc_snapshot_copy(arg0, arg1 as *mut c_void, arg2) == 0 {
            return 0;
        }
    }
    1
}

pub fn proc_kill(arg0: u64, arg1: u64) -> u64 {
    if arg0 != unsafe { ffi::clks_exec_current_pid() } && unsafe { ffi::clks_user_privileged_operation_allowed() } == 0 {
        return U64_MAX;
    }
    unsafe { ffi::clks_exec_proc_kill(arg0, arg1) }
}

pub fn exit(arg0: u64) -> u64 {
    if unsafe { ffi::clks_exec_request_exit(arg0) } != 0 { 1 } else { 0 }
}

pub fn sleep_ticks(frame: *mut SyscallFrame, arg0: u64) -> u64 {
    let before = unsafe { ffi::clks_interrupts_timer_ticks() };
    if unsafe { ffi::clks_wm_ready() } != 0 {
        unsafe { ffi::clks_wm_tick(before) };
    }
    if unsafe { ffi::clks_exec_suspend_current_from_syscall(frame as *mut c_void, arg0) } != 0 {
        return arg0;
    }
    let slept = unsafe { ffi::clks_exec_sleep_ticks(arg0) };
    if unsafe { ffi::clks_wm_ready() } != 0 {
        unsafe { ffi::clks_wm_tick(ffi::clks_interrupts_timer_ticks()) };
    }
    slept
}

fn ms_to_ticks(ms: u64) -> u64 {
    if ms == 0 {
        return 0;
    }
    let mut hz = unsafe { ffi::clks_interrupts_timer_hz() } as u64;
    if hz == 0 {
        hz = 100;
    }
    let whole = ms / 1000;
    let rem = ms % 1000;
    let Some(mut ticks) = whole.checked_mul(hz) else {
        return U64_MAX;
    };
    if rem != 0 {
        let partial = ((rem.saturating_mul(hz)) + 999) / 1000;
        let Some(sum) = ticks.checked_add(partial) else {
            return U64_MAX;
        };
        ticks = sum;
    }
    if ticks == 0 { 1 } else { ticks }
}

fn ticks_to_ms(ticks: u64) -> u64 {
    if ticks == 0 {
        return 0;
    }
    let mut hz = unsafe { ffi::clks_interrupts_timer_hz() } as u64;
    if hz == 0 {
        hz = 100;
    }
    (ticks / hz) * 1000 + (((ticks % hz) * 1000) / hz)
}

pub fn sleep_ms(frame: *mut SyscallFrame, ms: u64) -> u64 {
    ticks_to_ms(sleep_ticks(frame, ms_to_ticks(ms)))
}

pub fn yield_now(frame: *mut SyscallFrame) -> u64 {
    if unsafe { ffi::clks_wm_ready() } != 0 {
        unsafe { ffi::clks_wm_tick(ffi::clks_interrupts_timer_ticks()) };
    }
    let mut tick = unsafe { ffi::clks_interrupts_timer_ticks() };
    if unsafe { ffi::clks_exec_suspend_current_from_syscall(frame as *mut c_void, tick) } != 0 {
        return tick;
    }
    tick = unsafe { ffi::clks_exec_yield() };
    if unsafe { ffi::clks_wm_ready() } != 0 {
        unsafe { ffi::clks_wm_tick(tick) };
    }
    tick
}

pub fn shutdown() -> u64 {
    if unsafe { ffi::clks_user_privileged_operation_allowed() } == 0 {
        return 0;
    }
    unsafe {
        ffi::clks_log(LOG_WARN, b"SYSCALL\0".as_ptr() as *const i8, b"SHUTDOWN REQUESTED BY USERLAND\0".as_ptr() as *const i8);
        ffi::clks_serial_write(b"[WARN][SYSCALL] SHUTDOWN REQUESTED\n\0".as_ptr() as *const i8);
        #[cfg(target_arch = "x86_64")]
        core::arch::asm!("out dx, ax", in("dx") 0x604u16, in("ax") 0x2000u16, options(nomem, nostack, preserves_flags));
    }
    halt_forever();
}

pub fn restart() -> u64 {
    if unsafe { ffi::clks_user_privileged_operation_allowed() } == 0 {
        return 0;
    }
    unsafe {
        ffi::clks_log(LOG_WARN, b"SYSCALL\0".as_ptr() as *const i8, b"RESTART REQUESTED BY USERLAND\0".as_ptr() as *const i8);
        ffi::clks_serial_write(b"[WARN][SYSCALL] RESTART REQUESTED\n\0".as_ptr() as *const i8);
        #[cfg(target_arch = "x86_64")]
        core::arch::asm!("out dx, al", in("dx") 0x64u16, in("al") 0xFEu8, options(nomem, nostack, preserves_flags));
    }
    halt_forever();
}

fn halt_forever() -> ! {
    loop {
        #[cfg(target_arch = "x86_64")]
        unsafe {
            core::arch::asm!("hlt", options(nomem, nostack, preserves_flags));
        }
        #[cfg(not(target_arch = "x86_64"))]
        core::hint::spin_loop();
    }
}
