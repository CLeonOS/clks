use super::consts::{CLKS_FALSE, CLKS_TRUE, USER_TRACE_BUDGET};
use super::{ffi, meta, state};

fn write_hex64(value: u64) {
    let mut nibble = 16usize;
    while nibble > 0 {
        nibble -= 1;
        let current = ((value >> ((nibble * 4) as u64)) & 0x0f) as u8;
        let ch = if current < 10 { b'0' + current } else { b'A' + (current - 10) };
        unsafe {
            ffi::clks_serial_write_char(ch as i8);
        }
    }
}

fn trace_suppressed(id: u64) -> bool {
    (meta::flags(id) & meta::FLAG_TRACE_SUPPRESS) != 0
}

pub fn trace_user_program(id: u64) {
    let user_program_running =
        unsafe { ffi::clks_exec_is_running() != 0 && ffi::clks_exec_current_path_is_user() != 0 };

    if !user_program_running {
        unsafe {
            if state::USER_TRACE_ACTIVE == CLKS_TRUE {
                ffi::clks_serial_write(b"[DEBUG][SYSCALL] USER_TRACE_END\n\0".as_ptr() as *const i8);
            }
            state::USER_TRACE_ACTIVE = CLKS_FALSE;
            state::USER_TRACE_BUDGET = 0;
        }
        return;
    }

    unsafe {
        if state::USER_TRACE_ACTIVE == CLKS_FALSE {
            state::USER_TRACE_ACTIVE = CLKS_TRUE;
            state::USER_TRACE_BUDGET = USER_TRACE_BUDGET;
            ffi::clks_serial_write(b"[DEBUG][SYSCALL] USER_TRACE_BEGIN\n\0".as_ptr() as *const i8);
            ffi::clks_serial_write(b"[DEBUG][SYSCALL] PID: 0X\0".as_ptr() as *const i8);
            write_hex64(ffi::clks_exec_current_pid());
            ffi::clks_serial_write(b"\n\0".as_ptr() as *const i8);
        }

        if state::USER_TRACE_BUDGET > 0 {
            if trace_suppressed(id) {
                return;
            }

            ffi::clks_serial_write(b"[DEBUG][SYSCALL] USER_ID: 0X\0".as_ptr() as *const i8);
            write_hex64(id);
            ffi::clks_serial_write(b"\n\0".as_ptr() as *const i8);
            state::USER_TRACE_BUDGET -= 1;
            if state::USER_TRACE_BUDGET == 0 {
                ffi::clks_serial_write(b"[DEBUG][SYSCALL] USER_TRACE_BUDGET_EXHAUSTED\n\0".as_ptr() as *const i8);
            }
        }
    }
}

pub fn user_call(trace_enabled: bool, pid: u64, id: u64, arg0: u64, arg1: u64, arg2: u64) {
    if !trace_enabled || trace_suppressed(id) {
        return;
    }

    let flags = meta::flags(id);
    if (flags & meta::FLAG_TRACE_RETURN_ONLY) != 0 {
        return;
    }

    unsafe {
        ffi::clks_serial_write(b"[INFO][SYSCALL] CALL PID: 0X\0".as_ptr() as *const i8);
        write_hex64(pid);
        ffi::clks_serial_write(b" ID: 0X\0".as_ptr() as *const i8);
        write_hex64(id);
        ffi::clks_serial_write(b" NAME: \0".as_ptr() as *const i8);
        ffi::clks_serial_write(meta::name(id) as *const i8);
        ffi::clks_serial_write(b" ARG0: 0X\0".as_ptr() as *const i8);
        write_hex64(arg0);
        ffi::clks_serial_write(b" ARG1: 0X\0".as_ptr() as *const i8);
        write_hex64(arg1);
        ffi::clks_serial_write(b" ARG2: 0X\0".as_ptr() as *const i8);
        write_hex64(arg2);
        ffi::clks_serial_write(b"\n\0".as_ptr() as *const i8);
    }
}

pub fn user_return(trace_enabled: bool, pid: u64, id: u64, ret: u64) {
    if !trace_enabled || trace_suppressed(id) {
        return;
    }

    let flags = meta::flags(id);
    if (flags & meta::FLAG_TRACE_SKIP_ZERO_RET) != 0 && ret == 0 {
        return;
    }

    unsafe {
        ffi::clks_serial_write(b"[INFO][SYSCALL] RET  PID: 0X\0".as_ptr() as *const i8);
        write_hex64(pid);
        ffi::clks_serial_write(b" ID: 0X\0".as_ptr() as *const i8);
        write_hex64(id);
        ffi::clks_serial_write(b" NAME: \0".as_ptr() as *const i8);
        ffi::clks_serial_write(meta::name(id) as *const i8);
        ffi::clks_serial_write(b" RET: 0X\0".as_ptr() as *const i8);
        write_hex64(ret);
        ffi::clks_serial_write(b"\n\0".as_ptr() as *const i8);
    }
}
