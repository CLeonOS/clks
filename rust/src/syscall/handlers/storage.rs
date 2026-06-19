use core::{ffi::c_void, mem, ptr};

use crate::syscall::consts::*;
use crate::syscall::{ffi, text, usercopy};

const VERSION: &[u8] = b"26.5.2-indev\0";

#[repr(C)]
#[derive(Copy, Clone)]
pub struct SysinfoUser {
    kernel_name: [i8; ffi::SYSINFO_TEXT_MAX],
    kernel_version: [i8; ffi::SYSINFO_TEXT_MAX],
    arch: [i8; ffi::SYSINFO_TEXT_MAX],
    build_date: [i8; ffi::SYSINFO_TEXT_MAX],
    build_time: [i8; ffi::SYSINFO_TEXT_MAX],
    boot_mode: [i8; ffi::SYSINFO_BOOT_MODE_MAX],
    uptime_ms: u64,
    timer_ticks: u64,
    timer_hz: u64,
    managed_pages: u64,
    free_pages: u64,
    used_pages: u64,
    dropped_pages: u64,
    heap_total_bytes: u64,
    heap_used_bytes: u64,
    heap_free_bytes: u64,
    fs_nodes: u64,
    task_count: u64,
    service_count: u64,
    service_ready_count: u64,
    runnable_tasks: u64,
    sleeping_tasks: u64,
    blocked_tasks: u64,
    scheduler_yields: u64,
    scheduler_wakeups: u64,
}

pub fn kernel_version(arg0: u64, arg1: u64) -> u64 {
    usercopy::copy_text_to_user(arg0, arg1, VERSION.as_ptr() as *const i8, VERSION.len() - 1)
}

pub fn sysinfo(arg0: u64, arg1: u64) -> u64 {
    if arg0 == 0 || arg1 < mem::size_of::<SysinfoUser>() as u64 {
        return 0;
    }
    if !usercopy::user_ptr_writable(arg0, mem::size_of::<SysinfoUser>() as u64) {
        return 0;
    }

    let pmm = unsafe { ffi::clks_pmm_get_stats() };
    let heap = unsafe { ffi::clks_heap_get_stats() };
    let sched = unsafe { ffi::clks_scheduler_get_stats() };
    let hz = unsafe { ffi::clks_interrupts_timer_hz() as u64 };
    let ticks = unsafe { ffi::clks_interrupts_timer_ticks() };
    let mut info = SysinfoUser {
        kernel_name: [0; ffi::SYSINFO_TEXT_MAX],
        kernel_version: [0; ffi::SYSINFO_TEXT_MAX],
        arch: [0; ffi::SYSINFO_TEXT_MAX],
        build_date: [0; ffi::SYSINFO_TEXT_MAX],
        build_time: [0; ffi::SYSINFO_TEXT_MAX],
        boot_mode: [0; ffi::SYSINFO_BOOT_MODE_MAX],
        uptime_ms: if hz != 0 { ticks.saturating_mul(1000) / hz } else { 0 },
        timer_ticks: ticks,
        timer_hz: hz,
        managed_pages: pmm.managed_pages,
        free_pages: pmm.free_pages,
        used_pages: pmm.used_pages,
        dropped_pages: pmm.dropped_pages,
        heap_total_bytes: heap.total_bytes as u64,
        heap_used_bytes: heap.used_bytes as u64,
        heap_free_bytes: heap.free_bytes as u64,
        fs_nodes: unsafe { ffi::clks_fs_node_count() },
        task_count: sched.task_count as u64,
        service_count: unsafe { ffi::clks_service_count() },
        service_ready_count: unsafe { ffi::clks_service_ready_count() },
        runnable_tasks: sched.runnable_count as u64,
        sleeping_tasks: sched.sleeping_count as u64,
        blocked_tasks: sched.blocked_count as u64,
        scheduler_yields: sched.yield_count,
        scheduler_wakeups: sched.wakeup_count,
    };
    text::copy_const_text(&mut info.kernel_name, b"CLeonKernelSystem");
    text::copy_const_text(&mut info.kernel_version, VERSION);
    text::copy_const_text(&mut info.arch, b"x86_64");
    text::copy_const_text(&mut info.build_date, b"rust");
    text::copy_const_text(&mut info.build_time, b"rust");
    let disk_boot = unsafe {
        ffi::clks_disk_is_mounted() != 0
            && !ffi::clks_disk_mount_path().is_null()
            && text::c_str_eq(ffi::clks_disk_mount_path(), b"/")
    };
    text::copy_const_text(&mut info.boot_mode, if disk_boot { b"disk" } else { b"iso" });
    if usercopy::copy_to_user_struct(arg0, &info) { 1 } else { 0 }
}

pub fn locale_get(arg0: u64, arg1: u64) -> u64 {
    let locale = unsafe { ffi::clks_locale_current() };
    let len = unsafe { text::c_strlen(locale) };
    usercopy::copy_text_to_user(arg0, arg1, locale, len)
}

pub fn locale_set(arg0: u64) -> u64 {
    if unsafe { ffi::clks_user_privileged_operation_allowed() } == 0 {
        return 0;
    }
    let mut locale = [0i8; 32];
    if !usercopy::copy_user_string(arg0, &mut locale) {
        return 0;
    }
    if unsafe { ffi::clks_locale_is_valid(locale.as_ptr()) } == 0 {
        return 0;
    }
    if unsafe { ffi::clks_locale_set(locale.as_ptr(), CLKS_TRUE) } != 0 {
        return 1;
    }
    unsafe {
        ffi::clks_locale_set(locale.as_ptr(), CLKS_FALSE);
    }
    2
}

pub fn boot_cmdline(arg0: u64, arg1: u64) -> u64 {
    let cmdline = unsafe { ffi::clks_boot_get_cmdline() };
    let len = unsafe { text::c_strlen(cmdline) };
    usercopy::copy_text_to_user(arg0, arg1, cmdline, len)
}

pub fn disk_present() -> u64 { if unsafe { ffi::clks_disk_present() } != 0 { 1 } else { 0 } }
pub fn disk_size_bytes() -> u64 { unsafe { ffi::clks_disk_size_bytes() } }
pub fn disk_sector_count() -> u64 { unsafe { ffi::clks_disk_sector_count() } }
pub fn disk_formatted() -> u64 { if unsafe { ffi::clks_disk_is_formatted_fat32() } != 0 { 1 } else { 0 } }

pub fn disk_format_fat32(arg0: u64) -> u64 {
    if unsafe { ffi::clks_user_privileged_operation_allowed() } == 0 {
        return 0;
    }
    let mut label = [0i8; 16];
    if !usercopy::copy_optional_user_string(arg0, &mut label) {
        return 0;
    }
    let ptr = if label[0] != 0 { label.as_ptr() } else { core::ptr::null() };
    if unsafe { ffi::clks_disk_format_fat32(ptr) } != 0 { 1 } else { 0 }
}

pub fn disk_mount(arg0: u64) -> u64 {
    if unsafe { ffi::clks_user_privileged_operation_allowed() } == 0 {
        return 0;
    }
    let mut path = [0i8; PATH_MAX];
    if !usercopy::copy_user_string(arg0, &mut path) {
        return 0;
    }
    if unsafe { ffi::clks_disk_mount(path.as_ptr()) } != 0 { 1 } else { 0 }
}

pub fn disk_mounted() -> u64 { if unsafe { ffi::clks_disk_is_mounted() } != 0 { 1 } else { 0 } }

pub fn disk_mount_path(arg0: u64, arg1: u64) -> u64 {
    let mount_path = unsafe { ffi::clks_disk_mount_path() };
    if mount_path.is_null() || unsafe { *mount_path } == 0 {
        return 0;
    }
    usercopy::copy_text_to_user(arg0, arg1, mount_path, unsafe { text::c_strlen(mount_path) })
}

pub fn disk_read_sector(arg0: u64, arg1: u64) -> u64 {
    if arg1 == 0 || !usercopy::user_ptr_writable(arg1, DISK_SECTOR_BYTES as u64) {
        return 0;
    }
    let mut sector = [0u8; DISK_SECTOR_BYTES];
    if unsafe { ffi::clks_disk_read_sector(arg0, sector.as_mut_ptr() as *mut c_void) } == 0 {
        return 0;
    }
    unsafe {
        ptr::copy_nonoverlapping(sector.as_ptr(), arg1 as *mut u8, DISK_SECTOR_BYTES);
    }
    1
}

pub fn disk_write_sector(arg0: u64, arg1: u64) -> u64 {
    if unsafe { ffi::clks_user_privileged_operation_allowed() } == 0 {
        return 0;
    }
    if arg1 == 0 || !usercopy::user_ptr_readable(arg1, DISK_SECTOR_BYTES as u64) {
        return 0;
    }
    let mut sector = [0u8; DISK_SECTOR_BYTES];
    unsafe {
        ptr::copy_nonoverlapping(arg1 as *const u8, sector.as_mut_ptr(), DISK_SECTOR_BYTES);
    }
    if unsafe { ffi::clks_disk_write_sector(arg0, sector.as_ptr() as *const c_void) } != 0 { 1 } else { 0 }
}

pub fn disk_fsck_fat32(arg0: u64, arg1: u64) -> u64 {
    if arg1 == 0 {
        return 0;
    }
    if (arg0 & DISK_FSCK_FLAG_FIX) != 0 && unsafe { ffi::clks_user_privileged_operation_allowed() } == 0 {
        return 0;
    }
    if !usercopy::user_ptr_writable(arg1, mem::size_of::<ffi::DiskFsckResult>() as u64) {
        return 0;
    }
    let mut result = ffi::DiskFsckResult {
        status: 0,
        checked_clusters: 0,
        free_clusters: 0,
        used_clusters: 0,
        bad_entries: 0,
        loops: 0,
        size_mismatches: 0,
        orphan_clusters: 0,
        fixed_entries: 0,
        fixed_orphans: 0,
    };
    if unsafe { ffi::clks_disk_fsck_fat32(arg0, &mut result as *mut ffi::DiskFsckResult) } == 0 {
        return 0;
    }
    if usercopy::copy_to_user_struct(arg1, &result) { 1 } else { 0 }
}
