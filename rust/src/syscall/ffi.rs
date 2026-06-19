use core::ffi::{c_char, c_void};

pub const DRIVER_NAME_MAX: usize = 32;
pub const DRIVER_PATH_MAX: usize = 192;
pub const EXEC_PROC_PATH_MAX: usize = 192;
pub const USER_NAME_MAX: usize = 32;
pub const USER_HOME_MAX: usize = 96;
pub const SYSINFO_TEXT_MAX: usize = 32;
pub const SYSINFO_BOOT_MODE_MAX: usize = 16;
pub const INPUTM_NAME_MAX: usize = 32;
pub const INPUTM_PATH_MAX: usize = 192;
pub const INPUTM_LABEL_MAX: usize = 16;
pub const INPUTM_STATUS_MAX: usize = 384;

#[repr(C)]
#[derive(Copy, Clone)]
pub struct SchedulerStats {
    pub task_count: u32,
    pub current_task_id: u32,
    pub runnable_count: u32,
    pub sleeping_count: u32,
    pub blocked_count: u32,
    pub total_timer_ticks: u64,
    pub context_switch_count: u64,
    pub yield_count: u64,
    pub wakeup_count: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct FsNodeInfo {
    pub node_type: u32,
    pub size: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct PmmStats {
    pub managed_pages: u64,
    pub free_pages: u64,
    pub used_pages: u64,
    pub dropped_pages: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct HeapStats {
    pub total_bytes: usize,
    pub used_bytes: usize,
    pub free_bytes: usize,
    pub alloc_count: u64,
    pub free_count: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct FramebufferInfo {
    pub width: u32,
    pub height: u32,
    pub pitch: u32,
    pub bpp: u16,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct DisplayMode {
    pub physical_width: u32,
    pub physical_height: u32,
    pub logical_width: u32,
    pub logical_height: u32,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct DiskFsckResult {
    pub status: u64,
    pub checked_clusters: u64,
    pub free_clusters: u64,
    pub used_clusters: u64,
    pub bad_entries: u64,
    pub loops: u64,
    pub size_mismatches: u64,
    pub orphan_clusters: u64,
    pub fixed_entries: u64,
    pub fixed_orphans: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct ExecProcSnapshot {
    pub pid: u64,
    pub ppid: u64,
    pub state: u64,
    pub started_tick: u64,
    pub exited_tick: u64,
    pub exit_status: u64,
    pub runtime_ticks: u64,
    pub mem_bytes: u64,
    pub tty_index: u64,
    pub last_signal: u64,
    pub last_fault_vector: u64,
    pub last_fault_error: u64,
    pub last_fault_rip: u64,
    pub uid: u64,
    pub role: u64,
    pub path: [c_char; EXEC_PROC_PATH_MAX],
    pub main_thread_id: u64,
    pub thread_state: u64,
    pub scheduler_task_id: u64,
    pub blocked_reason: u64,
    pub wake_tick: u64,
    pub wait_target_pid: u64,
    pub parent_waiting: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct DriverInfo {
    pub name: [c_char; DRIVER_NAME_MAX],
    pub path: [c_char; DRIVER_PATH_MAX],
    pub kind: u32,
    pub state: u32,
    pub driver_class: u32,
    pub from_elf: u32,
    pub image_size: u64,
    pub elf_entry: u64,
    pub load_id: u64,
    pub owner_pid: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct MouseState {
    pub x: i32,
    pub y: i32,
    pub buttons: u8,
    pub packet_count: u64,
    pub ready: u32,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct UserPublicInfo {
    pub uid: u64,
    pub role: u64,
    pub logged_in: u64,
    pub disk_login_required: u64,
    pub name: [c_char; USER_NAME_MAX],
    pub home: [c_char; USER_HOME_MAX],
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct WmEvent {
    pub event_type: u64,
    pub arg0: u64,
    pub arg1: u64,
    pub arg2: u64,
    pub arg3: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct WmSnapshot {
    pub window_id: u64,
    pub owner_pid: u64,
    pub flags: u64,
    pub x: u64,
    pub y: u64,
    pub width: u64,
    pub height: u64,
    pub focused: u64,
    pub presented: u64,
    pub event_count: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct InputmInfo {
    pub name: [c_char; INPUTM_NAME_MAX],
    pub path: [c_char; INPUTM_PATH_MAX],
    pub rule_path: [c_char; INPUTM_PATH_MAX],
    pub label: [c_char; INPUTM_LABEL_MAX],
    pub flags: u64,
    pub active: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct InputmRegisterReq {
    pub name_ptr: u64,
    pub path_ptr: u64,
    pub flags: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct InputmRuleRegisterReq {
    pub name_ptr: u64,
    pub path_ptr: u64,
    pub rule_path_ptr: u64,
    pub label_ptr: u64,
    pub flags: u64,
}

extern "C" {
    pub fn clks_log(level: u32, tag: *const c_char, message: *const c_char);
    pub fn clks_log_hex(level: u32, tag: *const c_char, label: *const c_char, value: u64);
    pub fn clks_log_journal_count() -> u64;
    pub fn clks_log_journal_read(index_from_oldest: u64, out_line: *mut c_char, out_line_size: usize) -> u32;

    pub fn clks_serial_write(text: *const c_char);
    pub fn clks_serial_write_char(ch: c_char);

    pub fn clks_interrupts_timer_ticks() -> u64;
    pub fn clks_interrupts_timer_hz() -> u32;
    pub fn clks_scheduler_get_stats() -> SchedulerStats;
    pub fn clks_service_count() -> u64;
    pub fn clks_service_ready_count() -> u64;

    pub fn clks_exec_is_running() -> u32;
    pub fn clks_exec_current_path_is_user() -> u32;
    pub fn clks_exec_current_user_ptr_readable(addr: u64, size: u64) -> u32;
    pub fn clks_exec_current_user_ptr_writable(addr: u64, size: u64) -> u32;
    pub fn clks_exec_current_pid() -> u64;
    pub fn clks_exec_current_tty() -> u32;
    pub fn clks_exec_request_count() -> u64;
    pub fn clks_exec_success_count() -> u64;
    pub fn clks_exec_run_path(path: *const c_char, out_status: *mut u64) -> u32;
    pub fn clks_exec_run_pathv(path: *const c_char, argv_line: *const c_char, env_line: *const c_char, out_status: *mut u64) -> u32;
    pub fn clks_exec_run_pathv_io(path: *const c_char, argv_line: *const c_char, env_line: *const c_char, stdin_fd: u64, stdout_fd: u64, stderr_fd: u64, out_status: *mut u64) -> u32;
    pub fn clks_exec_spawn_path(path: *const c_char, out_pid: *mut u64) -> u32;
    pub fn clks_exec_spawn_pathv(path: *const c_char, argv_line: *const c_char, env_line: *const c_char, out_pid: *mut u64) -> u32;
    pub fn clks_exec_wait_pid(pid: u64, out_status: *mut u64) -> u64;
    pub fn clks_exec_request_exit(status: u64) -> u32;
    pub fn clks_exec_current_argc() -> u64;
    pub fn clks_exec_copy_current_argv(index: u64, out_value: *mut c_char, out_size: usize) -> u32;
    pub fn clks_exec_current_envc() -> u64;
    pub fn clks_exec_copy_current_env(index: u64, out_value: *mut c_char, out_size: usize) -> u32;
    pub fn clks_exec_current_signal() -> u64;
    pub fn clks_exec_current_fault_vector() -> u64;
    pub fn clks_exec_current_fault_error() -> u64;
    pub fn clks_exec_current_fault_rip() -> u64;
    pub fn clks_exec_proc_count() -> u64;
    pub fn clks_exec_proc_pid_at(index: u64, out_pid: *mut u64) -> u32;
    pub fn clks_exec_proc_snapshot(pid: u64, out_snapshot: *mut ExecProcSnapshot) -> u32;
    pub fn clks_exec_proc_snapshot_copy(pid: u64, out_snapshot: *mut c_void, out_size: u64) -> u32;
    pub fn clks_exec_proc_kill(pid: u64, signal: u64) -> u64;
    pub fn clks_exec_suspend_current_from_syscall(frame_ptr: *mut c_void, syscall_ret: u64) -> u32;
    pub fn clks_exec_sleep_ticks(ticks: u64) -> u64;
    pub fn clks_exec_yield() -> u64;
    pub fn clks_exec_fd_open(path: *const c_char, flags: u64, mode: u64) -> u64;
    pub fn clks_exec_fd_open_pty() -> u64;
    pub fn clks_exec_fd_read(fd: u64, out_buffer: *mut c_void, size: u64) -> u64;
    pub fn clks_exec_fd_write(fd: u64, buffer: *const c_void, size: u64) -> u64;
    pub fn clks_exec_fd_close(fd: u64) -> u64;
    pub fn clks_exec_fd_dup(fd: u64) -> u64;
    pub fn clks_exec_dl_open(path: *const c_char) -> u64;
    pub fn clks_exec_dl_close(handle: u64) -> u64;
    pub fn clks_exec_dl_sym(handle: u64, symbol: *const c_char) -> u64;
    pub fn clks_exec_user_heap_alloc(size: u64) -> u64;
    pub fn clks_exec_vm_alloc(size: u64, flags: u64) -> u64;
    pub fn clks_exec_vm_free(addr: u64, size: u64) -> u64;
    pub fn clks_exec_mmap(addr_hint: u64, size: u64, prot: u64, flags: u64, fd: u64, offset: u64) -> u64;

    pub fn clks_userland_shell_ready() -> u32;
    pub fn clks_userland_shell_exec_requested() -> u32;
    pub fn clks_userland_launch_attempts() -> u64;
    pub fn clks_userland_launch_success() -> u64;
    pub fn clks_userland_launch_failures() -> u64;

    pub fn clks_tty_count() -> u32;
    pub fn clks_tty_active() -> u32;
    pub fn clks_tty_switch(tty_index: u32);
    pub fn clks_tty_write(text: *const c_char);
    pub fn clks_tty_write_char(ch: c_char);
    pub fn clks_tty_text_cols() -> u32;
    pub fn clks_tty_text_rows() -> u32;
    pub fn clks_tty_set_resolution(width: u32, height: u32) -> u32;

    pub fn clks_keyboard_pop_char_for_tty(tty_index: u32, out_ch: *mut c_char) -> u32;
    pub fn clks_keyboard_buffered_count() -> u64;
    pub fn clks_keyboard_push_count() -> u64;
    pub fn clks_keyboard_pop_count() -> u64;
    pub fn clks_keyboard_drop_count() -> u64;
    pub fn clks_keyboard_hotkey_switch_count() -> u64;

    pub fn clks_fb_ready() -> u32;
    pub fn clks_fb_info() -> FramebufferInfo;
    pub fn clks_fb_clear(rgb: u32);
    pub fn clks_fb_fill_rect(x: u32, y: u32, width: u32, height: u32, rgb: u32);
    pub fn clks_fb_blit_rgba(dst_x: i32, dst_y: i32, src_pixels: *const c_void, src_width: u32, src_height: u32, src_pitch_bytes: u32);
    pub fn clks_display_mode_get(target: u32, out_mode: *mut DisplayMode) -> u32;

    pub fn clks_wm_ready() -> u32;
    pub fn clks_wm_tick(tick: u64);
    pub fn clks_wm_set_resolution(width: u32, height: u32) -> u32;
    pub fn clks_wm_window_count() -> u64;
    pub fn clks_wm_window_id_at(index: u64, out_window_id: *mut u64) -> u32;
    pub fn clks_wm_snapshot(window_id: u64, out_snapshot: *mut WmSnapshot) -> u32;
    pub fn clks_wm_create(owner_pid: u64, x: i32, y: i32, width: u32, height: u32, flags: u64) -> u64;
    pub fn clks_wm_destroy(owner_pid: u64, window_id: u64) -> u32;
    pub fn clks_wm_present(owner_pid: u64, window_id: u64, pixels: *const c_void, src_width: u32, src_height: u32, src_pitch_bytes: u32) -> u32;
    pub fn clks_wm_poll_event(owner_pid: u64, window_id: u64, out_event: *mut WmEvent) -> u32;
    pub fn clks_wm_move(owner_pid: u64, window_id: u64, x: i32, y: i32) -> u32;
    pub fn clks_wm_set_focus(owner_pid: u64, window_id: u64) -> u32;
    pub fn clks_wm_set_flags(owner_pid: u64, window_id: u64, flags: u64) -> u32;
    pub fn clks_wm_resize(owner_pid: u64, window_id: u64, width: u32, height: u32) -> u32;

    pub fn clks_fs_node_count() -> u64;
    pub fn clks_fs_is_ready() -> u32;
    pub fn clks_fs_stat(path: *const c_char, out_info: *mut FsNodeInfo) -> u32;
    pub fn clks_fs_read_all(path: *const c_char, out_size: *mut u64) -> *const c_void;
    pub fn clks_fs_count_children(dir_path: *const c_char) -> u64;
    pub fn clks_fs_get_child_name(dir_path: *const c_char, index: u64, out_name: *mut c_char, out_name_size: usize) -> u32;
    pub fn clks_fs_mkdir(path: *const c_char) -> u32;
    pub fn clks_fs_write_all(path: *const c_char, data: *const c_void, size: u64) -> u32;
    pub fn clks_fs_append(path: *const c_char, data: *const c_void, size: u64) -> u32;
    pub fn clks_fs_remove(path: *const c_char) -> u32;

    pub fn clks_disk_present() -> u32;
    pub fn clks_disk_size_bytes() -> u64;
    pub fn clks_disk_sector_count() -> u64;
    pub fn clks_disk_is_formatted_fat32() -> u32;
    pub fn clks_disk_format_fat32(label: *const c_char) -> u32;
    pub fn clks_disk_mount(mount_path: *const c_char) -> u32;
    pub fn clks_disk_is_mounted() -> u32;
    pub fn clks_disk_mount_path() -> *const c_char;
    pub fn clks_disk_read_sector(lba: u64, out_sector: *mut c_void) -> u32;
    pub fn clks_disk_write_sector(lba: u64, sector_data: *const c_void) -> u32;
    pub fn clks_disk_fsck_fat32(flags: u64, out_result: *mut DiskFsckResult) -> u32;
    pub fn clks_disk_read_all(path: *const c_char, out_size: *mut u64) -> *const c_void;
    pub fn clks_disk_append(path: *const c_char, data: *const c_void, size: u64) -> u32;

    pub fn clks_net_available() -> u32;
    pub fn clks_net_ipv4_addr_be() -> u32;
    pub fn clks_net_ipv4_netmask_be() -> u32;
    pub fn clks_net_ipv4_gateway_be() -> u32;
    pub fn clks_net_ipv4_dns_be() -> u32;
    pub fn clks_net_ping_ipv4(dst_ipv4_be: u32, poll_budget: u64) -> u32;
    pub fn clks_net_udp_send(dst_ipv4_be: u32, dst_port: u16, src_port: u16, payload: *const c_void, payload_len: u64) -> u64;
    pub fn clks_net_udp_recv(out_payload: *mut c_void, payload_capacity: u64, out_src_ipv4_be: *mut u32, out_src_port: *mut u16, out_dst_port: *mut u16) -> u64;
    pub fn clks_net_tcp_connect(dst_ipv4_be: u32, dst_port: u16, src_port: u16, poll_budget: u64) -> u32;
    pub fn clks_net_tcp_listen(port: u16) -> u32;
    pub fn clks_net_tcp_accept(poll_budget: u64) -> u32;
    pub fn clks_net_tcp_send(payload: *const c_void, payload_len: u64, poll_budget: u64) -> u64;
    pub fn clks_net_tcp_recv(out_payload: *mut c_void, payload_capacity: u64, poll_budget: u64) -> u64;
    pub fn clks_net_tcp_close(poll_budget: u64) -> u32;
    pub fn clks_net_tcp_last_error() -> u64;

    pub fn clks_mouse_snapshot(out_state: *mut MouseState);
    pub fn clks_inputm_count() -> u64;
    pub fn clks_inputm_info_at(index: u64, out_info: *mut InputmInfo) -> u32;
    pub fn clks_inputm_current() -> u64;
    pub fn clks_inputm_select(index: u64) -> u32;
    pub fn clks_inputm_register(name: *const c_char, path: *const c_char, flags: u64) -> u64;
    pub fn clks_inputm_register_rule(name: *const c_char, path: *const c_char, rule_path: *const c_char, label: *const c_char, flags: u64) -> u64;
    pub fn clks_inputm_status_set(text: *const c_char);

    pub fn clks_driver_count() -> u64;
    pub fn clks_driver_get(index: u64, out_info: *mut DriverInfo) -> u32;
    pub fn clks_driver_load_path(path: *const c_char) -> u64;
    pub fn clks_driver_reload_elf_dir() -> u64;
    pub fn clks_driver_unload(name_or_path: *const c_char) -> u32;

    pub fn clks_audio_available() -> u32;
    pub fn clks_audio_play_tone(hz: u64, ticks: u64) -> u32;
    pub fn clks_audio_stop();

    pub fn clks_user_current_info(out_info: *mut UserPublicInfo) -> u32;
    pub fn clks_user_current_is_admin() -> u32;
    pub fn clks_user_login(name: *const c_char, password: *const c_char, out_info: *mut UserPublicInfo) -> u32;
    pub fn clks_user_logout();
    pub fn clks_user_count() -> u64;
    pub fn clks_user_at(index: u64, out_info: *mut UserPublicInfo) -> u32;
    pub fn clks_user_create(name: *const c_char, password: *const c_char, role: u64) -> u32;
    pub fn clks_user_change_password(name: *const c_char, old_password: *const c_char, new_password: *const c_char) -> u32;
    pub fn clks_user_set_role(name: *const c_char, role: u64) -> u32;
    pub fn clks_user_remove(name: *const c_char) -> u32;
    pub fn clks_user_path_read_allowed(path: *const c_char) -> u32;
    pub fn clks_user_path_write_allowed(path: *const c_char) -> u32;
    pub fn clks_user_privileged_operation_allowed() -> u32;

    pub fn clks_pmm_get_stats() -> PmmStats;
    pub fn clks_heap_get_stats() -> HeapStats;
    pub fn clks_kmalloc(size: usize) -> *mut c_void;
    pub fn clks_kfree(ptr: *mut c_void);
    pub fn clks_locale_current() -> *const c_char;
    pub fn clks_locale_is_valid(locale: *const c_char) -> u32;
    pub fn clks_locale_set(locale: *const c_char, persist: u32) -> u32;
    pub fn clks_boot_get_cmdline() -> *const c_char;
}
