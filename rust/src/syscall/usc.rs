use core::ptr;

use super::consts::*;
use super::{ffi, meta, state, text};

fn is_dangerous(id: u64) -> bool {
    matches!(
        id,
        FS_MKDIR
            | FS_WRITE
            | FS_APPEND
            | FS_REMOVE
            | EXEC_PATH
            | EXEC_PATHV
            | EXEC_PATHV_IO
            | SPAWN_PATH
            | SPAWN_PATHV
            | PROC_KILL
            | SHUTDOWN
            | RESTART
            | DISK_FORMAT_FAT32
            | DISK_WRITE_SECTOR
            | DISK_FSCK_FAT32
    )
}

fn copy_path(dst: &mut [i8], src: *const i8) {
    if dst.is_empty() {
        return;
    }
    if src.is_null() {
        dst[0] = 0;
        return;
    }
    let mut i = 0usize;
    unsafe {
        while i + 1 < dst.len() {
            let ch = *src.add(i);
            dst[i] = ch;
            if ch == 0 {
                return;
            }
            i += 1;
        }
    }
    dst[i] = 0;
}

fn current_app_path(out_path: &mut [i8]) -> bool {
    if out_path.is_empty() {
        return false;
    }
    out_path[0] = 0;
    let pid = unsafe { ffi::clks_exec_current_pid() };
    if pid == 0 {
        return false;
    }
    let mut snap = ffi::ExecProcSnapshot {
        pid: 0, ppid: 0, state: 0, started_tick: 0, exited_tick: 0, exit_status: 0, runtime_ticks: 0, mem_bytes: 0,
        tty_index: 0, last_signal: 0, last_fault_vector: 0, last_fault_error: 0, last_fault_rip: 0, uid: 0, role: 0,
        path: [0; ffi::EXEC_PROC_PATH_MAX], main_thread_id: 0, thread_state: 0, scheduler_task_id: 0,
        blocked_reason: 0, wake_tick: 0, wait_target_pid: 0, parent_waiting: 0,
    };
    if unsafe { ffi::clks_exec_proc_snapshot(pid, &mut snap as *mut ffi::ExecProcSnapshot) } == 0 || snap.path[0] == 0 {
        return false;
    }
    copy_path(out_path, snap.path.as_ptr());
    true
}

unsafe fn path_eq(table_path: &[i8; ffi::EXEC_PROC_PATH_MAX], path: &[i8; ffi::EXEC_PROC_PATH_MAX]) -> bool {
    let mut i = 0usize;
    while i < ffi::EXEC_PROC_PATH_MAX {
        if table_path[i] != path[i] {
            return false;
        }
        if table_path[i] == 0 {
            return true;
        }
        i += 1;
    }
    true
}

unsafe fn find_in_table(used: *const u32, table: *const [i8; ffi::EXEC_PROC_PATH_MAX], path: &[i8; ffi::EXEC_PROC_PATH_MAX]) -> i32 {
    if path[0] == 0 {
        return -1;
    }
    let mut i = 0usize;
    while i < USC_MAX_ALLOWED_APPS {
        if *used.add(i) == CLKS_TRUE && path_eq(&*table.add(i), path) {
            return i as i32;
        }
        i += 1;
    }
    -1
}

unsafe fn add_to_table(
    used: *mut u32,
    table: *mut [i8; ffi::EXEC_PROC_PATH_MAX],
    path: &[i8; ffi::EXEC_PROC_PATH_MAX],
) -> bool {
    if path[0] == 0 {
        return false;
    }
    if find_in_table(used as *const u32, table as *const [i8; ffi::EXEC_PROC_PATH_MAX], path) >= 0 {
        return true;
    }
    let mut i = 0usize;
    while i < USC_MAX_ALLOWED_APPS {
        if *used.add(i) == CLKS_FALSE {
            *used.add(i) = CLKS_TRUE;
            ptr::copy_nonoverlapping(path.as_ptr(), (*table.add(i)).as_mut_ptr(), ffi::EXEC_PROC_PATH_MAX);
            return true;
        }
        i += 1;
    }
    false
}

fn build_perm_rule_path(out_path: &mut [i8; ffi::EXEC_PROC_PATH_MAX]) -> bool {
    out_path[0] = 0;
    if unsafe { ffi::clks_disk_is_mounted() } == 0 {
        return false;
    }
    let mount = unsafe { ffi::clks_disk_mount_path() };
    if mount.is_null() || unsafe { *mount } == 0 {
        return false;
    }
    let mount_len = unsafe { text::c_strlen(mount) };
    let file_name = USC_PERM_RULE_FILE;
    let file_len = file_name.len() - 1;
    if mount_len >= out_path.len() {
        return false;
    }
    unsafe {
        ptr::copy_nonoverlapping(mount as *const u8, out_path.as_mut_ptr() as *mut u8, mount_len);
    }
    let mut pos = mount_len;
    if pos > 0 && out_path[pos - 1] as u8 != b'/' {
        if pos + 1 >= out_path.len() {
            return false;
        }
        out_path[pos] = b'/' as i8;
        pos += 1;
    }
    if pos + file_len + 1 > out_path.len() {
        return false;
    }
    unsafe {
        ptr::copy_nonoverlapping(file_name.as_ptr(), out_path.as_mut_ptr().add(pos) as *mut u8, file_len);
    }
    out_path[pos + file_len] = 0;
    true
}

fn append_permanent_rule(path: &[i8; ffi::EXEC_PROC_PATH_MAX]) -> bool {
    if path[0] == 0 {
        return false;
    }
    let mut rules_path = [0i8; ffi::EXEC_PROC_PATH_MAX];
    if !build_perm_rule_path(&mut rules_path) {
        return false;
    }
    let len = path.iter().position(|&c| c == 0).unwrap_or(path.len());
    if len == 0 || len + 1 >= ffi::EXEC_PROC_PATH_MAX + 2 {
        return false;
    }
    let mut line = [0i8; ffi::EXEC_PROC_PATH_MAX + 2];
    unsafe {
        ptr::copy_nonoverlapping(path.as_ptr(), line.as_mut_ptr(), len);
    }
    line[len] = b'\n' as i8;
    line[len + 1] = 0;
    unsafe { ffi::clks_disk_append(rules_path.as_ptr(), line.as_ptr() as *const core::ffi::c_void, (len + 1) as u64) != 0 }
}

fn remember_permanent_path(path: &[i8; ffi::EXEC_PROC_PATH_MAX]) -> bool {
    if path[0] == 0 {
        return false;
    }
    load_permanent_rules_if_needed();
    unsafe {
        if find_in_table(
            core::ptr::addr_of!(state::USC_PERMANENT_ALLOWED_USED) as *const u32,
            core::ptr::addr_of!(state::USC_PERMANENT_ALLOWED_PATH) as *const [i8; ffi::EXEC_PROC_PATH_MAX],
            path,
        ) >= 0
        {
            return true;
        }
    }
    if !append_permanent_rule(path) {
        return false;
    }
    unsafe {
        add_to_table(
            core::ptr::addr_of_mut!(state::USC_PERMANENT_ALLOWED_USED) as *mut u32,
            core::ptr::addr_of_mut!(state::USC_PERMANENT_ALLOWED_PATH) as *mut [i8; ffi::EXEC_PROC_PATH_MAX],
            path,
        )
    }
}

fn load_permanent_rules_if_needed() {
    unsafe {
        if state::USC_PERMANENT_LOADED == CLKS_TRUE {
            return;
        }
    }
    let mut rules_path = [0i8; ffi::EXEC_PROC_PATH_MAX];
    if !build_perm_rule_path(&mut rules_path) {
        return;
    }
    unsafe {
        state::USC_PERMANENT_LOADED = CLKS_TRUE;
    }
    let mut data_size = 0u64;
    let data = unsafe { ffi::clks_disk_read_all(rules_path.as_ptr(), &mut data_size as *mut u64) as *const u8 };
    if data.is_null() || data_size == 0 {
        return;
    }
    let mut pos = 0u64;
    while pos < data_size {
        let start = pos;
        while pos < data_size && unsafe { *data.add(pos as usize) } != b'\n' {
            pos += 1;
        }
        let end = pos;
        if pos < data_size {
            pos += 1;
        }
        let mut line = [0i8; ffi::EXEC_PROC_PATH_MAX];
        let mut len = (end - start) as usize;
        while len > 0 {
            let ch = unsafe { *data.add(start as usize + len - 1) };
            if ch == b'\r' || ch == b'\n' || ch == b' ' || ch == b'\t' {
                len -= 1;
            } else {
                break;
            }
        }
        if len == 0 || len >= line.len() {
            continue;
        }
        unsafe {
            ptr::copy_nonoverlapping(data.add(start as usize), line.as_mut_ptr() as *mut u8, len);
        }
        line[len] = 0;
        if line[0] == b'#' as i8 || line[0] == b';' as i8 {
            continue;
        }
        unsafe {
            let _ = add_to_table(
                core::ptr::addr_of_mut!(state::USC_PERMANENT_ALLOWED_USED) as *mut u32,
                core::ptr::addr_of_mut!(state::USC_PERMANENT_ALLOWED_PATH) as *mut [i8; ffi::EXEC_PROC_PATH_MAX],
                &line,
            );
        }
    }
}

fn emit_denied(id: u64, app_path: &[i8; ffi::EXEC_PROC_PATH_MAX]) {
    unsafe {
        ffi::clks_serial_write(b"[WARN][USC] DENIED SYSCALL \0".as_ptr() as *const i8);
        ffi::clks_serial_write(meta::name(id) as *const i8);
        ffi::clks_serial_write(b" APP \0".as_ptr() as *const i8);
        ffi::clks_serial_write(app_path.as_ptr());
        ffi::clks_serial_write(b"\n\0".as_ptr() as *const i8);
    }
}

fn emit_text_line(label: &[u8], value: *const i8) {
    let mut message = [0i8; 320];
    let mut pos = 0usize;
    pos = text::append_bytes(&mut message, pos, label);
    pos = text::append_bytes(&mut message, pos, b": ");
    if !value.is_null() {
        let len = unsafe { text::c_strlen(value) };
        let _ = text::append_c_n(&mut message, pos, value, len);
    }
    unsafe {
        ffi::clks_log(LOG_WARN, b"USC\0".as_ptr() as *const i8, message.as_ptr());
    }
}

fn emit_hex_line(label: &[u8], value: u64) {
    let mut label_buf = [0i8; 48];
    text::copy_const_text(&mut label_buf, label);
    unsafe {
        ffi::clks_log_hex(LOG_WARN, b"USC\0".as_ptr() as *const i8, label_buf.as_ptr(), value);
    }
}

#[derive(Copy, Clone, PartialEq, Eq)]
enum Decision {
    Deny,
    AllowOnce,
    AllowSession,
    AllowPermanent,
}

fn decision_from_key(ch: i8) -> Option<Decision> {
    match ch as u8 {
        b'1' | b'o' | b'O' => Some(Decision::AllowOnce),
        b'2' | b's' | b'S' => Some(Decision::AllowSession),
        b'3' | b'p' | b'P' => Some(Decision::AllowPermanent),
        b'n' | b'N' | b'\n' | b'\r' | 27 => Some(Decision::Deny),
        _ => None,
    }
}

fn sleep_until_input() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        let flags: u64;
        core::arch::asm!("pushfq; pop {}", out(reg) flags, options(nomem, preserves_flags));
        if (flags & (1 << 9)) != 0 {
            core::arch::asm!("hlt", options(nomem, nostack, preserves_flags));
        } else {
            core::arch::asm!("sti; hlt; cli", options(nomem, nostack, preserves_flags));
        }
    }
    #[cfg(not(target_arch = "x86_64"))]
    core::hint::spin_loop();
}

fn prompt_allow(app_path: &[i8; ffi::EXEC_PROC_PATH_MAX], id: u64, arg0: u64, arg1: u64, arg2: u64) -> Decision {
    let name = meta::name(id) as *const i8;
    emit_text_line(b"DANGEROUS_SYSCALL", b"REQUEST DETECTED\0".as_ptr() as *const i8);
    emit_text_line(b"APP", app_path.as_ptr());
    emit_hex_line(b"SYSCALL_ID", id);
    emit_text_line(b"SYSCALL_NAME", name);
    emit_hex_line(b"ARG0", arg0);
    emit_hex_line(b"ARG1", arg1);
    emit_hex_line(b"ARG2", arg2);
    unsafe {
        ffi::clks_serial_write(b"[WARN][USC] 1=once 2=session 3=permanent N=deny\n\0".as_ptr() as *const i8);
    }

    let tty_index = unsafe { ffi::clks_exec_current_tty() };
    loop {
        let mut ch = 0i8;
        if unsafe { ffi::clks_keyboard_pop_char_for_tty(tty_index, &mut ch as *mut i8) } != 0 {
            if let Some(decision) = decision_from_key(ch) {
                return decision;
            }
        }
        sleep_until_input();
    }
}

pub fn check(id: u64, _arg0: u64, _arg1: u64, _arg2: u64) -> bool {
    if !is_dangerous(id) {
        return true;
    }
    if !state::in_user_exec_context() {
        return true;
    }
    if unsafe { ffi::clks_user_privileged_operation_allowed() } != 0 {
        return true;
    }

    let mut app_path = [0i8; ffi::EXEC_PROC_PATH_MAX];
    if !current_app_path(&mut app_path) {
        return false;
    }

    unsafe {
        if find_in_table(
            core::ptr::addr_of!(state::USC_SESSION_ALLOWED_USED) as *const u32,
            core::ptr::addr_of!(state::USC_SESSION_ALLOWED_PATH) as *const [i8; ffi::EXEC_PROC_PATH_MAX],
            &app_path,
        ) >= 0
        {
            return true;
        }
    }

    load_permanent_rules_if_needed();
    unsafe {
        if find_in_table(
            core::ptr::addr_of!(state::USC_PERMANENT_ALLOWED_USED) as *const u32,
            core::ptr::addr_of!(state::USC_PERMANENT_ALLOWED_PATH) as *const [i8; ffi::EXEC_PROC_PATH_MAX],
            &app_path,
        ) >= 0
        {
            return true;
        }
    }

    let decision = prompt_allow(&app_path, id, _arg0, _arg1, _arg2);
    if decision == Decision::AllowOnce {
        emit_text_line(b"ALLOW_ONCE", app_path.as_ptr());
        return true;
    }
    if decision == Decision::AllowSession {
        unsafe {
            let _ = add_to_table(
                core::ptr::addr_of_mut!(state::USC_SESSION_ALLOWED_USED) as *mut u32,
                core::ptr::addr_of_mut!(state::USC_SESSION_ALLOWED_PATH) as *mut [i8; ffi::EXEC_PROC_PATH_MAX],
                &app_path,
            );
        }
        emit_text_line(b"ALLOW_SESSION", app_path.as_ptr());
        return true;
    }
    if decision == Decision::AllowPermanent {
        let permanent = remember_permanent_path(&app_path);
        unsafe {
            let _ = add_to_table(
                core::ptr::addr_of_mut!(state::USC_SESSION_ALLOWED_USED) as *mut u32,
                core::ptr::addr_of_mut!(state::USC_SESSION_ALLOWED_PATH) as *mut [i8; ffi::EXEC_PROC_PATH_MAX],
                &app_path,
            );
        }
        if permanent {
            emit_text_line(b"ALLOW_PERMANENT", app_path.as_ptr());
        } else {
            emit_text_line(b"ALLOW_SESSION_FALLBACK", app_path.as_ptr());
        }
        return true;
    }

    emit_denied(id, &app_path);
    false
}
