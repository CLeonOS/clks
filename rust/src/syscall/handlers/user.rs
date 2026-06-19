use core::mem;

use crate::syscall::{ffi, usercopy};
use crate::syscall::consts::U64_MAX;

#[repr(C)]
#[derive(Copy, Clone)]
struct UserInfoUser {
    uid: u64,
    role: u64,
    logged_in: u64,
    disk_login_required: u64,
    name: [i8; ffi::USER_NAME_MAX],
    home: [i8; ffi::USER_HOME_MAX],
}

#[repr(C)]
#[derive(Copy, Clone)]
struct UserLoginReq {
    name_ptr: u64,
    password_ptr: u64,
    out_info_ptr: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct UserAddReq {
    name_ptr: u64,
    password_ptr: u64,
    role: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct UserPasswdReq {
    name_ptr: u64,
    old_password_ptr: u64,
    new_password_ptr: u64,
}

fn export_info(src: &ffi::UserPublicInfo) -> UserInfoUser {
    let mut dst = UserInfoUser {
        uid: src.uid,
        role: src.role,
        logged_in: src.logged_in,
        disk_login_required: src.disk_login_required,
        name: [0; ffi::USER_NAME_MAX],
        home: [0; ffi::USER_HOME_MAX],
    };
    dst.name.copy_from_slice(&src.name);
    dst.home.copy_from_slice(&src.home);
    dst
}

fn copy_info_to_user(dst_addr: u64, info: &ffi::UserPublicInfo) -> u64 {
    if dst_addr == 0 || !usercopy::user_ptr_writable(dst_addr, mem::size_of::<UserInfoUser>() as u64) {
        return 0;
    }
    let out = export_info(info);
    if usercopy::copy_to_user_struct(dst_addr, &out) { 1 } else { 0 }
}

pub fn current(arg0: u64) -> u64 {
    let mut info = ffi::UserPublicInfo {
        uid: 0, role: 0, logged_in: 0, disk_login_required: 0, name: [0; ffi::USER_NAME_MAX], home: [0; ffi::USER_HOME_MAX],
    };
    if unsafe { ffi::clks_user_current_info(&mut info as *mut ffi::UserPublicInfo) } == 0 {
        return 0;
    }
    copy_info_to_user(arg0, &info)
}

pub fn login(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<UserLoginReq>(arg0) else {
        return 0;
    };
    let mut name = [0i8; ffi::USER_NAME_MAX];
    let mut password = [0i8; 96];
    let mut info = ffi::UserPublicInfo {
        uid: 0, role: 0, logged_in: 0, disk_login_required: 0, name: [0; ffi::USER_NAME_MAX], home: [0; ffi::USER_HOME_MAX],
    };
    if !usercopy::copy_user_string(req.name_ptr, &mut name) || !usercopy::copy_user_string(req.password_ptr, &mut password) {
        return 0;
    }
    if unsafe { ffi::clks_user_login(name.as_ptr(), password.as_ptr(), &mut info as *mut ffi::UserPublicInfo) } == 0 {
        return 0;
    }
    if req.out_info_ptr != 0 {
        let _ = copy_info_to_user(req.out_info_ptr, &info);
    }
    1
}

pub fn logout() -> u64 {
    unsafe { ffi::clks_user_logout() };
    1
}

pub fn count() -> u64 {
    if unsafe { ffi::clks_user_current_is_admin() } == 0 {
        return U64_MAX;
    }
    unsafe { ffi::clks_user_count() }
}

pub fn at(arg0: u64, arg1: u64) -> u64 {
    if unsafe { ffi::clks_user_current_is_admin() } == 0 {
        return 0;
    }
    let mut info = ffi::UserPublicInfo {
        uid: 0, role: 0, logged_in: 0, disk_login_required: 0, name: [0; ffi::USER_NAME_MAX], home: [0; ffi::USER_HOME_MAX],
    };
    if unsafe { ffi::clks_user_at(arg0, &mut info as *mut ffi::UserPublicInfo) } == 0 {
        return 0;
    }
    copy_info_to_user(arg1, &info)
}

pub fn add(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<UserAddReq>(arg0) else {
        return 0;
    };
    let mut name = [0i8; ffi::USER_NAME_MAX];
    let mut password = [0i8; 96];
    if !usercopy::copy_user_string(req.name_ptr, &mut name) || !usercopy::copy_user_string(req.password_ptr, &mut password) {
        return 0;
    }
    if unsafe { ffi::clks_user_create(name.as_ptr(), password.as_ptr(), req.role) } != 0 { 1 } else { 0 }
}

pub fn passwd(arg0: u64) -> u64 {
    let Some(req) = usercopy::copy_from_user_struct::<UserPasswdReq>(arg0) else {
        return 0;
    };
    let mut name = [0i8; ffi::USER_NAME_MAX];
    let mut old_password = [0i8; 96];
    let mut new_password = [0i8; 96];
    if !usercopy::copy_user_string(req.name_ptr, &mut name)
        || !usercopy::copy_optional_user_string(req.old_password_ptr, &mut old_password)
        || !usercopy::copy_user_string(req.new_password_ptr, &mut new_password)
    {
        return 0;
    }
    if unsafe { ffi::clks_user_change_password(name.as_ptr(), old_password.as_ptr(), new_password.as_ptr()) } != 0 {
        1
    } else {
        0
    }
}

pub fn set_role(arg0: u64, arg1: u64) -> u64 {
    let mut name = [0i8; ffi::USER_NAME_MAX];
    if !usercopy::copy_user_string(arg0, &mut name) {
        return 0;
    }
    if unsafe { ffi::clks_user_set_role(name.as_ptr(), arg1) } != 0 { 1 } else { 0 }
}

pub fn remove(arg0: u64) -> u64 {
    let mut name = [0i8; ffi::USER_NAME_MAX];
    if !usercopy::copy_user_string(arg0, &mut name) {
        return 0;
    }
    if unsafe { ffi::clks_user_remove(name.as_ptr()) } != 0 { 1 } else { 0 }
}

pub fn is_admin() -> u64 {
    if unsafe { ffi::clks_user_current_is_admin() } != 0 { 1 } else { 0 }
}
