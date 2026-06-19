use core::mem;

use crate::syscall::consts::*;
use crate::syscall::{ffi, state, usercopy};

#[repr(C)]
#[derive(Copy, Clone)]
struct MouseStateUser {
    x: u64,
    y: u64,
    buttons: u64,
    packet_count: u64,
    ready: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct WmCreateReq {
    x: u64,
    y: u64,
    width: u64,
    height: u64,
    flags: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct WmPresentReq {
    window_id: u64,
    pixels_ptr: u64,
    src_width: u64,
    src_height: u64,
    src_pitch_bytes: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct WmMoveReq {
    window_id: u64,
    x: u64,
    y: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct WmResizeReq {
    window_id: u64,
    width: u64,
    height: u64,
}

pub fn mouse_state(arg0: u64) -> u64 {
    if arg0 == 0 || !usercopy::user_ptr_writable(arg0, mem::size_of::<MouseStateUser>() as u64) {
        return 0;
    }
    let mut state = ffi::MouseState {
        x: 0,
        y: 0,
        buttons: 0,
        packet_count: 0,
        ready: 0,
    };
    unsafe { ffi::clks_mouse_snapshot(&mut state as *mut ffi::MouseState) };
    let out = MouseStateUser {
        x: state.x as u64,
        y: state.y as u64,
        buttons: state.buttons as u64,
        packet_count: state.packet_count,
        ready: if state.ready != 0 { 1 } else { 0 },
    };
    if usercopy::copy_to_user_struct(arg0, &out) { 1 } else { 0 }
}

fn u64_to_i32(raw: u64) -> Option<i32> {
    let value = raw as i64;
    if value < i32::MIN as i64 || value > i32::MAX as i64 {
        None
    } else {
        Some(value as i32)
    }
}

fn owner_pid() -> u64 {
    if state::in_user_exec_context() {
        unsafe { ffi::clks_exec_current_pid() }
    } else {
        0
    }
}

pub fn wm_create(arg0: u64) -> u64 {
    if arg0 == 0 || unsafe { ffi::clks_wm_ready() } == 0 {
        return 0;
    }
    let Some(req) = usercopy::copy_from_user_struct::<WmCreateReq>(arg0) else {
        return 0;
    };
    if req.width > u32::MAX as u64 || req.height > u32::MAX as u64 {
        return 0;
    }
    let (Some(x), Some(y)) = (u64_to_i32(req.x), u64_to_i32(req.y)) else {
        return 0;
    };
    unsafe { ffi::clks_wm_create(owner_pid(), x, y, req.width as u32, req.height as u32, req.flags) }
}

pub fn wm_destroy(arg0: u64) -> u64 {
    if arg0 == 0 || unsafe { ffi::clks_wm_ready() } == 0 {
        return 0;
    }
    if unsafe { ffi::clks_wm_destroy(owner_pid(), arg0) } != 0 { 1 } else { 0 }
}

pub fn wm_present(arg0: u64) -> u64 {
    if arg0 == 0 || unsafe { ffi::clks_wm_ready() } == 0 {
        return 0;
    }
    let Some(req) = usercopy::copy_from_user_struct::<WmPresentReq>(arg0) else {
        return 0;
    };
    if req.window_id == 0 || req.pixels_ptr == 0 {
        return 0;
    }
    if req.src_width > u32::MAX as u64 || req.src_height > u32::MAX as u64 || req.src_pitch_bytes > u32::MAX as u64 {
        return 0;
    }
    if req.src_height == 0 || req.src_pitch_bytes == 0 {
        return 0;
    }
    let Some(src_bytes) = req.src_height.checked_mul(req.src_pitch_bytes) else {
        return 0;
    };
    if !usercopy::user_ptr_readable(req.pixels_ptr, src_bytes) {
        return 0;
    }
    if unsafe {
        ffi::clks_wm_present(
            owner_pid(),
            req.window_id,
            req.pixels_ptr as *const core::ffi::c_void,
            req.src_width as u32,
            req.src_height as u32,
            req.src_pitch_bytes as u32,
        )
    } != 0
    {
        1
    } else {
        0
    }
}

pub fn wm_poll_event(arg0: u64, arg1: u64) -> u64 {
    if arg0 == 0 || arg1 == 0 || unsafe { ffi::clks_wm_ready() } == 0 {
        return 0;
    }
    if !usercopy::user_ptr_writable(arg1, mem::size_of::<ffi::WmEvent>() as u64) {
        return 0;
    }
    if unsafe { ffi::clks_wm_poll_event(owner_pid(), arg0, arg1 as *mut ffi::WmEvent) } != 0 { 1 } else { 0 }
}

pub fn wm_move(arg0: u64) -> u64 {
    if arg0 == 0 || unsafe { ffi::clks_wm_ready() } == 0 {
        return 0;
    }
    let Some(req) = usercopy::copy_from_user_struct::<WmMoveReq>(arg0) else {
        return 0;
    };
    if req.window_id == 0 {
        return 0;
    }
    let (Some(x), Some(y)) = (u64_to_i32(req.x), u64_to_i32(req.y)) else {
        return 0;
    };
    if unsafe { ffi::clks_wm_move(owner_pid(), req.window_id, x, y) } != 0 { 1 } else { 0 }
}

pub fn wm_set_focus(arg0: u64) -> u64 {
    if arg0 == 0 || unsafe { ffi::clks_wm_ready() } == 0 {
        return 0;
    }
    if unsafe { ffi::clks_wm_set_focus(owner_pid(), arg0) } != 0 { 1 } else { 0 }
}
pub fn wm_set_flags(arg0: u64, arg1: u64) -> u64 {
    if arg0 == 0 || unsafe { ffi::clks_wm_ready() } == 0 {
        return 0;
    }
    if unsafe { ffi::clks_wm_set_flags(owner_pid(), arg0, arg1) } != 0 { 1 } else { 0 }
}
pub fn wm_resize(arg0: u64) -> u64 {
    if arg0 == 0 || unsafe { ffi::clks_wm_ready() } == 0 {
        return 0;
    }
    let Some(req) = usercopy::copy_from_user_struct::<WmResizeReq>(arg0) else {
        return 0;
    };
    if req.window_id == 0 || req.width > u32::MAX as u64 || req.height > u32::MAX as u64 {
        return 0;
    }
    if unsafe { ffi::clks_wm_resize(owner_pid(), req.window_id, req.width as u32, req.height as u32) } != 0 {
        1
    } else {
        0
    }
}
pub fn wm_count() -> u64 {
    if unsafe { ffi::clks_wm_ready() } == 0 { 0 } else { unsafe { ffi::clks_wm_window_count() } }
}
pub fn wm_id_at(arg0: u64, arg1: u64) -> u64 {
    let mut window_id = 0u64;
    if arg1 == 0 || unsafe { ffi::clks_wm_ready() } == 0 {
        return 0;
    }
    if !usercopy::user_ptr_writable(arg1, mem::size_of::<u64>() as u64) {
        return 0;
    }
    if unsafe { ffi::clks_wm_window_id_at(arg0, &mut window_id as *mut u64) } == 0 {
        return 0;
    }
    if usercopy::copy_to_user_struct(arg1, &window_id) { 1 } else { 0 }
}
pub fn wm_snapshot(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    if arg0 == 0 || arg1 == 0 || arg2 < mem::size_of::<ffi::WmSnapshot>() as u64 || unsafe { ffi::clks_wm_ready() } == 0 {
        return 0;
    }
    if !usercopy::user_ptr_writable(arg1, mem::size_of::<ffi::WmSnapshot>() as u64) {
        return 0;
    }
    let mut snap = ffi::WmSnapshot {
        window_id: 0, owner_pid: 0, flags: 0, x: 0, y: 0, width: 0, height: 0, focused: 0, presented: 0, event_count: 0,
    };
    if unsafe { ffi::clks_wm_snapshot(arg0, &mut snap as *mut ffi::WmSnapshot) } == 0 {
        return 0;
    }
    if usercopy::copy_to_user_struct(arg1, &snap) { 1 } else { 0 }
}

pub fn inputm_count() -> u64 { unsafe { ffi::clks_inputm_count() } }
pub fn inputm_info(arg0: u64, arg1: u64, arg2: u64) -> u64 {
    if arg1 == 0 || arg2 < mem::size_of::<ffi::InputmInfo>() as u64 {
        return 0;
    }
    if !usercopy::user_ptr_writable(arg1, mem::size_of::<ffi::InputmInfo>() as u64) {
        return 0;
    }
    let mut info = ffi::InputmInfo {
        name: [0; ffi::INPUTM_NAME_MAX], path: [0; ffi::INPUTM_PATH_MAX], rule_path: [0; ffi::INPUTM_PATH_MAX],
        label: [0; ffi::INPUTM_LABEL_MAX], flags: 0, active: 0,
    };
    if unsafe { ffi::clks_inputm_info_at(arg0, &mut info as *mut ffi::InputmInfo) } == 0 {
        return 0;
    }
    if usercopy::copy_to_user_struct(arg1, &info) { 1 } else { 0 }
}
pub fn inputm_current() -> u64 { unsafe { ffi::clks_inputm_current() } }
pub fn inputm_select(arg0: u64) -> u64 { if unsafe { ffi::clks_inputm_select(arg0) } != 0 { 1 } else { 0 } }
pub fn inputm_register(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<ffi::InputmRegisterReq>(arg0) else {
        return U64_MAX;
    };
    let mut name = [0i8; ffi::INPUTM_NAME_MAX];
    let mut path = [0i8; ffi::INPUTM_PATH_MAX];
    if !usercopy::copy_user_string(req.name_ptr, &mut name) || !usercopy::copy_optional_user_string(req.path_ptr, &mut path) {
        return U64_MAX;
    }
    unsafe { ffi::clks_inputm_register(name.as_ptr(), path.as_ptr(), req.flags) }
}
pub fn inputm_register_rule(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<ffi::InputmRuleRegisterReq>(arg0) else {
        return U64_MAX;
    };
    let mut name = [0i8; ffi::INPUTM_NAME_MAX];
    let mut path = [0i8; ffi::INPUTM_PATH_MAX];
    let mut rule_path = [0i8; ffi::INPUTM_PATH_MAX];
    let mut label = [0i8; ffi::INPUTM_LABEL_MAX];
    if !usercopy::copy_user_string(req.name_ptr, &mut name)
        || !usercopy::copy_optional_user_string(req.path_ptr, &mut path)
        || !usercopy::copy_user_string(req.rule_path_ptr, &mut rule_path)
        || !usercopy::copy_optional_user_string(req.label_ptr, &mut label)
    {
        return U64_MAX;
    }
    unsafe { ffi::clks_inputm_register_rule(name.as_ptr(), path.as_ptr(), rule_path.as_ptr(), label.as_ptr(), req.flags) }
}
pub fn tty_status_set(arg0: u64) -> u64 {
    if arg0 == 0 {
        unsafe { ffi::clks_inputm_status_set(b"\0".as_ptr() as *const i8) };
        return 1;
    }
    let mut text = [0i8; ffi::INPUTM_STATUS_MAX];
    if !usercopy::copy_user_string(arg0, &mut text) {
        return 0;
    }
    unsafe { ffi::clks_inputm_status_set(text.as_ptr()) };
    1
}
