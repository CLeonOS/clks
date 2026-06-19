use core::{ffi::c_void, ptr};

use crate::syscall::consts::*;
use crate::syscall::{ffi, text, usercopy};

static mut FS_IO_BOUNCE: [u8; FS_IO_CHUNK_LEN] = [0; FS_IO_CHUNK_LEN];

fn fs_bounce_mut_ptr() -> *mut u8 {
    core::ptr::addr_of_mut!(FS_IO_BOUNCE) as *mut u8
}

fn fs_bounce_ptr() -> *const u8 {
    core::ptr::addr_of!(FS_IO_BOUNCE) as *const u8
}

fn procfs_is_root(path: &[i8]) -> bool { text::buf_eq(path, b"/proc") }
fn fs_is_root(path: &[i8]) -> bool { text::buf_eq(path, b"/") }
fn procfs_is_self(path: &[i8]) -> bool { text::buf_eq(path, b"/proc/self") }
fn procfs_is_list(path: &[i8]) -> bool { text::buf_eq(path, b"/proc/list") }

fn fs_has_real_proc_dir() -> bool {
    let mut info = ffi::FsNodeInfo { node_type: 0, size: 0 };
    (unsafe { ffi::clks_fs_stat(b"/proc\0".as_ptr() as *const i8, &mut info as *mut ffi::FsNodeInfo) } != 0)
        && (info.node_type as u64) == FS_NODE_DIR
}

fn procfs_parse_pid(path: &[i8]) -> Option<u64> {
    let prefix = b"/proc/";
    if !text::buf_starts_with(path, prefix) {
        return None;
    }
    let mut part = [0i8; 32];
    let mut i = 0usize;
    let mut src = prefix.len();
    while src < path.len() && path[src] != 0 {
        if i + 1 >= part.len() {
            return None;
        }
        let ch = path[src] as u8;
        if !(b'0'..=b'9').contains(&ch) {
            return None;
        }
        part[i] = path[src];
        i += 1;
        src += 1;
    }
    if i == 0 {
        return None;
    }
    let pid = text::parse_u64_dec(&part)?;
    if pid == 0 { None } else { Some(pid) }
}

fn proc_state_name(state: u64) -> &'static [u8] {
    match state {
        1 => b"PENDING",
        2 => b"RUNNING",
        4 => b"STOPPED",
        3 => b"EXITED",
        _ => b"UNUSED",
    }
}

fn procfs_snapshot_for_path(path: &[i8]) -> Option<ffi::ExecProcSnapshot> {
    let pid = if procfs_is_self(path) {
        unsafe { ffi::clks_exec_current_pid() }
    } else {
        procfs_parse_pid(path)?
    };
    if pid == 0 {
        return None;
    }
    let mut snap = ffi::ExecProcSnapshot {
        pid: 0, ppid: 0, state: 0, started_tick: 0, exited_tick: 0, exit_status: 0, runtime_ticks: 0, mem_bytes: 0,
        tty_index: 0, last_signal: 0, last_fault_vector: 0, last_fault_error: 0, last_fault_rip: 0, uid: 0, role: 0,
        path: [0; ffi::EXEC_PROC_PATH_MAX], main_thread_id: 0, thread_state: 0, scheduler_task_id: 0,
        blocked_reason: 0, wake_tick: 0, wait_target_pid: 0, parent_waiting: 0,
    };
    if unsafe { ffi::clks_exec_proc_snapshot(pid, &mut snap as *mut ffi::ExecProcSnapshot) } == 0 {
        None
    } else {
        Some(snap)
    }
}

fn render_snapshot(out: &mut [i8], snap: &ffi::ExecProcSnapshot) -> usize {
    let mut pos = 0usize;
    pos = text::append_bytes(out, pos, b"pid=");
    pos = text::append_u64_dec(out, pos, snap.pid);
    pos = text::append_char(out, pos, b'\n');
    pos = text::append_bytes(out, pos, b"ppid=");
    pos = text::append_u64_dec(out, pos, snap.ppid);
    pos = text::append_char(out, pos, b'\n');
    pos = text::append_bytes(out, pos, b"state=");
    pos = text::append_bytes(out, pos, proc_state_name(snap.state));
    pos = text::append_char(out, pos, b'\n');
    pos = text::append_bytes(out, pos, b"state_id=");
    pos = text::append_u64_dec(out, pos, snap.state);
    pos = text::append_char(out, pos, b'\n');
    pos = text::append_bytes(out, pos, b"tty=");
    pos = text::append_u64_dec(out, pos, snap.tty_index);
    pos = text::append_char(out, pos, b'\n');
    pos = text::append_bytes(out, pos, b"runtime_ticks=");
    pos = text::append_u64_dec(out, pos, snap.runtime_ticks);
    pos = text::append_char(out, pos, b'\n');
    pos = text::append_bytes(out, pos, b"mem_bytes=");
    pos = text::append_u64_dec(out, pos, snap.mem_bytes);
    pos = text::append_char(out, pos, b'\n');
    pos = text::append_bytes(out, pos, b"exit_status=");
    pos = text::append_u64_hex(out, pos, snap.exit_status);
    pos = text::append_char(out, pos, b'\n');
    pos = text::append_bytes(out, pos, b"last_signal=");
    pos = text::append_u64_dec(out, pos, snap.last_signal);
    pos = text::append_char(out, pos, b'\n');
    pos = text::append_bytes(out, pos, b"last_fault_vector=");
    pos = text::append_u64_dec(out, pos, snap.last_fault_vector);
    pos = text::append_char(out, pos, b'\n');
    pos = text::append_bytes(out, pos, b"last_fault_error=");
    pos = text::append_u64_hex(out, pos, snap.last_fault_error);
    pos = text::append_char(out, pos, b'\n');
    pos = text::append_bytes(out, pos, b"last_fault_rip=");
    pos = text::append_u64_hex(out, pos, snap.last_fault_rip);
    pos = text::append_char(out, pos, b'\n');
    pos = text::append_bytes(out, pos, b"path=");
    let len = snap.path.iter().position(|&c| c == 0).unwrap_or(snap.path.len());
    pos = text::append_c_n(out, pos, snap.path.as_ptr(), len);
    text::append_char(out, pos, b'\n')
}

fn render_list(out: &mut [i8]) -> usize {
    let mut pos = text::append_bytes(out, 0, b"pid state tty runtime mem path\n");
    let count = unsafe { ffi::clks_exec_proc_count() };
    let mut i = 0u64;
    while i < count {
        let mut pid = 0u64;
        if unsafe { ffi::clks_exec_proc_pid_at(i, &mut pid as *mut u64) } != 0 && pid != 0 {
            if let Some(snap) = procfs_snapshot_by_pid(pid) {
                pos = text::append_u64_dec(out, pos, snap.pid);
                pos = text::append_char(out, pos, b' ');
                pos = text::append_bytes(out, pos, proc_state_name(snap.state));
                pos = text::append_char(out, pos, b' ');
                pos = text::append_u64_dec(out, pos, snap.tty_index);
                pos = text::append_char(out, pos, b' ');
                pos = text::append_u64_dec(out, pos, snap.runtime_ticks);
                pos = text::append_char(out, pos, b' ');
                pos = text::append_u64_dec(out, pos, snap.mem_bytes);
                pos = text::append_char(out, pos, b' ');
                let len = snap.path.iter().position(|&c| c == 0).unwrap_or(snap.path.len());
                pos = text::append_c_n(out, pos, snap.path.as_ptr(), len);
                pos = text::append_char(out, pos, b'\n');
            }
        }
        i += 1;
    }
    pos
}

fn procfs_snapshot_by_pid(pid: u64) -> Option<ffi::ExecProcSnapshot> {
    let mut snap = ffi::ExecProcSnapshot {
        pid: 0, ppid: 0, state: 0, started_tick: 0, exited_tick: 0, exit_status: 0, runtime_ticks: 0, mem_bytes: 0,
        tty_index: 0, last_signal: 0, last_fault_vector: 0, last_fault_error: 0, last_fault_rip: 0, uid: 0, role: 0,
        path: [0; ffi::EXEC_PROC_PATH_MAX], main_thread_id: 0, thread_state: 0, scheduler_task_id: 0,
        blocked_reason: 0, wake_tick: 0, wait_target_pid: 0, parent_waiting: 0,
    };
    if unsafe { ffi::clks_exec_proc_snapshot(pid, &mut snap as *mut ffi::ExecProcSnapshot) } != 0 { Some(snap) } else { None }
}

fn procfs_render_file(path: &[i8], out: &mut [i8]) -> Option<usize> {
    if procfs_is_list(path) {
        return Some(render_list(out));
    }
    procfs_snapshot_for_path(path).map(|snap| render_snapshot(out, &snap))
}

pub fn child_count(arg0: u64) -> u64 {
    let mut path = [0i8; PATH_MAX];
    if !usercopy::copy_user_string(arg0, &mut path) {
        return U64_MAX;
    }
    if unsafe { ffi::clks_user_path_read_allowed(path.as_ptr()) } == 0 {
        return U64_MAX;
    }
    if procfs_is_root(&path) {
        return 2 + unsafe { ffi::clks_exec_proc_count() };
    }
    let base_count = unsafe { ffi::clks_fs_count_children(path.as_ptr()) };
    if base_count == U64_MAX {
        return U64_MAX;
    }
    if fs_is_root(&path) && !fs_has_real_proc_dir() {
        return base_count + 1;
    }
    base_count
}

pub fn get_child_name(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    if arg2 == 0 || !usercopy::user_ptr_writable(arg2, NAME_MAX as u64) {
        return 0;
    }
    let mut path = [0i8; PATH_MAX];
    if !usercopy::copy_user_string(arg0, &mut path) {
        return 0;
    }
    if unsafe { ffi::clks_user_path_read_allowed(path.as_ptr()) } == 0 {
        return 0;
    }
    unsafe { ptr::write_bytes(arg2 as *mut u8, 0, NAME_MAX) };
    if procfs_is_root(&path) {
        if arg1 == 0 {
            unsafe { ptr::copy_nonoverlapping(b"self\0".as_ptr(), arg2 as *mut u8, 5) };
            return 1;
        }
        if arg1 == 1 {
            unsafe { ptr::copy_nonoverlapping(b"list\0".as_ptr(), arg2 as *mut u8, 5) };
            return 1;
        }
        let mut pid = 0u64;
        if unsafe { ffi::clks_exec_proc_pid_at(arg1 - 2, &mut pid as *mut u64) } == 0 || pid == 0 {
            return 0;
        }
        let mut pid_text = [0i8; 32];
        let len = text::append_u64_dec(&mut pid_text, 0, pid);
        if len + 1 > NAME_MAX {
            return 0;
        }
        unsafe { ptr::copy_nonoverlapping(pid_text.as_ptr() as *const u8, arg2 as *mut u8, len + 1) };
        return 1;
    }
    if fs_is_root(&path) && !fs_has_real_proc_dir() {
        if arg1 == 0 {
            unsafe { ptr::copy_nonoverlapping(b"proc\0".as_ptr(), arg2 as *mut u8, 5) };
            return 1;
        }
        if unsafe { ffi::clks_fs_get_child_name(path.as_ptr(), arg1 - 1, arg2 as *mut i8, NAME_MAX) } == 0 {
            return 0;
        }
        return 1;
    }
    if unsafe { ffi::clks_fs_get_child_name(path.as_ptr(), arg1, arg2 as *mut i8, NAME_MAX) } != 0 { 1 } else { 0 }
}

pub fn read(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    if arg1 == 0 || arg2 == 0 || !usercopy::user_ptr_writable(arg1, arg2) {
        return 0;
    }
    let mut path = [0i8; PATH_MAX];
    if !usercopy::copy_user_string(arg0, &mut path) {
        return 0;
    }
    if unsafe { ffi::clks_user_path_read_allowed(path.as_ptr()) } == 0 {
        return 0;
    }
    if procfs_is_list(&path) || procfs_is_self(&path) || procfs_parse_pid(&path).is_some() {
        let mut proc_text = [0i8; PROCFS_TEXT_MAX];
        let Some(proc_len) = procfs_render_file(&path, &mut proc_text) else {
            return 0;
        };
        let copy_len = core::cmp::min(proc_len as u64, arg2);
        if copy_len == 0 {
            return 0;
        }
        unsafe { ptr::copy_nonoverlapping(proc_text.as_ptr() as *const u8, arg1 as *mut u8, copy_len as usize) };
        return copy_len;
    }
    let mut file_size = 0u64;
    let data = unsafe { ffi::clks_fs_read_all(path.as_ptr(), &mut file_size as *mut u64) };
    if data.is_null() || file_size == 0 {
        return 0;
    }
    let copy_len = core::cmp::min(file_size, arg2);
    unsafe { ptr::copy_nonoverlapping(data as *const u8, arg1 as *mut u8, copy_len as usize) };
    copy_len
}

pub fn stat_type(arg0: u64) -> u64 {
    let mut path = [0i8; PATH_MAX];
    if !usercopy::copy_user_string(arg0, &mut path) {
        return U64_MAX;
    }
    if unsafe { ffi::clks_user_path_read_allowed(path.as_ptr()) } == 0 {
        return U64_MAX;
    }
    if procfs_is_root(&path) {
        return FS_NODE_DIR;
    }
    if procfs_is_list(&path) || procfs_is_self(&path) || procfs_snapshot_for_path(&path).is_some() {
        return FS_NODE_FILE;
    }
    let mut info = ffi::FsNodeInfo { node_type: 0, size: 0 };
    if unsafe { ffi::clks_fs_stat(path.as_ptr(), &mut info as *mut ffi::FsNodeInfo) } == 0 {
        return U64_MAX;
    }
    info.node_type as u64
}

pub fn stat_size(arg0: u64) -> u64 {
    let mut path = [0i8; PATH_MAX];
    if !usercopy::copy_user_string(arg0, &mut path) {
        return U64_MAX;
    }
    if unsafe { ffi::clks_user_path_read_allowed(path.as_ptr()) } == 0 {
        return U64_MAX;
    }
    if procfs_is_root(&path) {
        return 0;
    }
    let mut proc_text = [0i8; PROCFS_TEXT_MAX];
    if let Some(proc_len) = procfs_render_file(&path, &mut proc_text) {
        return proc_len as u64;
    }
    let mut info = ffi::FsNodeInfo { node_type: 0, size: 0 };
    if unsafe { ffi::clks_fs_stat(path.as_ptr(), &mut info as *mut ffi::FsNodeInfo) } == 0 {
        return U64_MAX;
    }
    info.size
}

pub fn mkdir(arg0: u64) -> u64 {
    let mut path = [0i8; PATH_MAX];
    if !usercopy::copy_user_string(arg0, &mut path) || unsafe { ffi::clks_user_path_write_allowed(path.as_ptr()) } == 0 {
        return 0;
    }
    if unsafe { ffi::clks_fs_mkdir(path.as_ptr()) } != 0 { 1 } else { 0 }
}

fn write_common(arg0: u64, arg1: u64, arg2: u64, append_mode: bool) -> u64 {
    let mut path = [0i8; PATH_MAX];
    if !usercopy::copy_user_string(arg0, &mut path) || unsafe { ffi::clks_user_path_write_allowed(path.as_ptr()) } == 0 {
        return 0;
    }
    if arg2 == 0 {
        let ok = unsafe {
            if append_mode {
                ffi::clks_fs_append(path.as_ptr(), core::ptr::null(), 0)
            } else {
                ffi::clks_fs_write_all(path.as_ptr(), core::ptr::null(), 0)
            }
        };
        return if ok != 0 { 1 } else { 0 };
    }
    if arg1 == 0 {
        return 0;
    }
    if !append_mode && arg2 <= 1024 * 1024 {
        let whole = unsafe { ffi::clks_kmalloc(arg2 as usize) };
        if !whole.is_null() {
            let mut copied = 0u64;
            while copied < arg2 {
                let chunk_len = core::cmp::min(arg2 - copied, FS_IO_CHUNK_LEN as u64);
                let src = arg1.wrapping_add(copied);
                if src < arg1 || !usercopy::user_ptr_readable(src, chunk_len) {
                    unsafe { ffi::clks_kfree(whole) };
                    return 0;
                }
                unsafe { ptr::copy_nonoverlapping(src as *const u8, (whole as *mut u8).add(copied as usize), chunk_len as usize) };
                copied += chunk_len;
            }
            let ok = unsafe { ffi::clks_fs_write_all(path.as_ptr(), whole as *const c_void, arg2) };
            unsafe { ffi::clks_kfree(whole) };
            return if ok != 0 { arg2 } else { 0 };
        }
    }
    let mut checked = 0u64;
    while checked < arg2 {
        let chunk_len = core::cmp::min(arg2 - checked, FS_IO_CHUNK_LEN as u64);
        let src = arg1.wrapping_add(checked);
        if src < arg1 || !usercopy::user_ptr_readable(src, chunk_len) {
            return 0;
        }
        checked += chunk_len;
    }
    let mut written = 0u64;
    while written < arg2 {
        let chunk_len = core::cmp::min(arg2 - written, FS_IO_CHUNK_LEN as u64);
        unsafe {
            ptr::copy_nonoverlapping((arg1 + written) as *const u8, fs_bounce_mut_ptr(), chunk_len as usize);
            let ok = if append_mode || written > 0 {
                ffi::clks_fs_append(path.as_ptr(), fs_bounce_ptr() as *const c_void, chunk_len)
            } else {
                ffi::clks_fs_write_all(path.as_ptr(), fs_bounce_ptr() as *const c_void, chunk_len)
            };
            if ok == 0 {
                return 0;
            }
        }
        written += chunk_len;
    }
    arg2
}

pub fn write(arg0: u64, arg1: u64, arg2: u64) -> u64 { write_common(arg0, arg1, arg2, false) }
pub fn append(arg0: u64, arg1: u64, arg2: u64) -> u64 { write_common(arg0, arg1, arg2, true) }

pub fn remove(arg0: u64) -> u64 {
    let mut path = [0i8; PATH_MAX];
    if !usercopy::copy_user_string(arg0, &mut path) || unsafe { ffi::clks_user_path_write_allowed(path.as_ptr()) } == 0 {
        return 0;
    }
    if unsafe { ffi::clks_fs_remove(path.as_ptr()) } != 0 { 1 } else { 0 }
}
